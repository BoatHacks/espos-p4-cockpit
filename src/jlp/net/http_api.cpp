#include "http_api.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "sensesp.h"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "../layout/layout_manager.h"

static const char* TAG = "jlp.http";

namespace jlp {

namespace {

constexpr size_t kMaxBodyBytes = 64 * 1024;

esp_err_t layout_post(httpd_req_t* req) {
  if (req->content_len == 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"empty body\"}");
    return ESP_OK;
  }
  if ((size_t)req->content_len > kMaxBodyBytes) {
    httpd_resp_set_status(req, "413 Payload Too Large");
    httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"body too large\"}");
    return ESP_OK;
  }

  std::string body;
  body.resize(req->content_len);
  int total = 0;
  while (total < req->content_len) {
    int n = httpd_req_recv(req, &body[total], req->content_len - total);
    if (n <= 0) {
      if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"recv failed\"}");
      return ESP_FAIL;
    }
    total += n;
  }

  // Hop onto the event_loop task to do the swap, then wait for the
  // result. The handler is design-time only — brief blocking is fine.
  auto result = std::make_shared<ApplyResult>();
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  if (!done) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"semaphore\"}");
    return ESP_FAIL;
  }

  sensesp::event_loop()->onDelay(0, [body, result, done]() mutable {
    *result = layout_manager().apply(body, ApplySource::PostLayout);
    xSemaphoreGive(done);
  });

  if (xSemaphoreTake(done, pdMS_TO_TICKS(10000)) != pdTRUE) {
    vSemaphoreDelete(done);
    httpd_resp_set_status(req, "504 Gateway Timeout");
    httpd_resp_sendstr(req,
                       "{\"ok\":false,\"err\":\"apply timed out\"}");
    return ESP_OK;
  }
  vSemaphoreDelete(done);

  httpd_resp_set_type(req, "application/json");
  if (!result->ok) {
    httpd_resp_set_status(req, "400 Bad Request");
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"err\":\"%s\"}",
             result->err.c_str());
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
  }

  char buf[384];
  if (!result->warning.empty()) {
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"name\":\"%s\",\"screens\":%u,\"widgets\":%u,"
             "\"warning\":\"%s\"}",
             result->name.c_str(), result->screens, result->widgets,
             result->warning.c_str());
  } else {
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"name\":\"%s\",\"screens\":%u,\"widgets\":%u}",
             result->name.c_str(), result->screens, result->widgets);
  }
  httpd_resp_sendstr(req, buf);
  return ESP_OK;
}

esp_err_t healthz_get(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"ok\":true}");
  return ESP_OK;
}

// --- screenshot endpoint -------------------------------------------------
//
// GET /screenshot returns a 16-bit BMP of the current LVGL active screen.
// BMP because browsers decode it natively (no firmware-side PNG/JPEG
// dep) and it's tiny to encode — just a 70-byte header + raw pixels.
// Cost on the wire: width * height * 2 + 70 bytes (~1.2 MB at 1024x600).
//
// Slow over esp_hosted SDIO (~6s). Acceptable for a manual debug
// trigger; if we ever want continuous we'd switch to JPEG via P4's
// hardware encoder.

// Builds a 70-byte BMP header for a width*height image stored as
// 16-bit RGB565 with bit masks. BMP rows are bottom-up.
static void bmp_header_rgb565(uint8_t out[70], uint32_t w, uint32_t h) {
  const uint32_t row_bytes = w * 2;
  const uint32_t pixel_bytes = row_bytes * h;
  const uint32_t file_size = 70 + pixel_bytes;
  const uint32_t pixel_offset = 70;

  auto put32 = [&](size_t at, uint32_t v) {
    out[at] = v & 0xff; out[at+1] = (v>>8)&0xff;
    out[at+2] = (v>>16)&0xff; out[at+3] = (v>>24)&0xff;
  };
  auto put16 = [&](size_t at, uint16_t v) {
    out[at] = v & 0xff; out[at+1] = (v>>8)&0xff;
  };
  std::memset(out, 0, 70);
  out[0]='B'; out[1]='M';
  put32(2, file_size);
  put32(10, pixel_offset);
  put32(14, 56);          // BITMAPV3INFOHEADER
  put32(18, w);
  put32(22, h);           // positive: bottom-up — we'll write rows reversed
  put16(26, 1);           // planes
  put16(28, 16);          // bpp
  put32(30, 3);           // BI_BITFIELDS
  put32(34, pixel_bytes);
  put32(38, 2835);        // x ppm (72dpi)
  put32(42, 2835);        // y ppm
  put32(54, 0xF800);      // R mask (top 5 bits of high byte)
  put32(58, 0x07E0);      // G mask
  put32(62, 0x001F);      // B mask
  put32(66, 0x00000000);  // A mask (unused)
}

struct ScreenshotResult {
  bool ok = false;
  std::string err;
  uint32_t w = 0;
  uint32_t h = 0;
  // Owned pixel buffer (RGB565, top-down as returned by lv_snapshot_take).
  // The endpoint copies rows from this into the HTTP response bottom-up
  // to honour BMP row order.
  std::vector<uint8_t> pixels;
};

// Runs on the event_loop task. Takes a snapshot of the active screen.
// LVGL's internal allocator can't give us a 1.2MB RGB565 buffer
// (it's not PSRAM-aware in this configuration), so we allocate the
// pixel store ourselves in PSRAM and wrap it via lv_draw_buf_init.
static void take_screenshot(ScreenshotResult* out) {
  lv_obj_t* scr = lv_screen_active();
  if (!scr) { out->err = "no active screen"; return; }
  const int32_t w = LV_HOR_RES;
  const int32_t h = LV_VER_RES;
  const uint32_t stride = w * 2;
  const uint32_t size = stride * h;
  uint8_t* pixels = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (!pixels) {
    out->err = "PSRAM alloc failed";
    return;
  }
  lv_draw_buf_t buf;
  if (lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_RGB565, stride, pixels,
                       size) != LV_RESULT_OK) {
    heap_caps_free(pixels);
    out->err = "lv_draw_buf_init failed";
    return;
  }
  if (lv_snapshot_take_to_draw_buf(scr, LV_COLOR_FORMAT_RGB565, &buf) !=
      LV_RESULT_OK) {
    heap_caps_free(pixels);
    out->err = "lv_snapshot_take_to_draw_buf failed";
    return;
  }
  out->w = w;
  out->h = h;
  // Move out of the lock-held LVGL world before the slow HTTP send.
  out->pixels.assign(pixels, pixels + size);
  heap_caps_free(pixels);
  out->ok = true;
}

esp_err_t screenshot_get(httpd_req_t* req) {
  auto result = std::make_shared<ScreenshotResult>();
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  if (!done) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "semaphore alloc failed");
    return ESP_FAIL;
  }
  sensesp::event_loop()->onDelay(0, [result, done]() {
    take_screenshot(result.get());
    xSemaphoreGive(done);
  });
  if (xSemaphoreTake(done, pdMS_TO_TICKS(5000)) != pdTRUE) {
    vSemaphoreDelete(done);
    httpd_resp_set_status(req, "504 Gateway Timeout");
    httpd_resp_sendstr(req, "snapshot timeout");
    return ESP_OK;
  }
  vSemaphoreDelete(done);

  if (!result->ok) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, result->err.c_str());
    return ESP_OK;
  }

  const uint32_t w = result->w, h = result->h;
  const uint32_t row_bytes = w * 2;
  uint8_t header[70];
  bmp_header_rgb565(header, w, h);

  httpd_resp_set_type(req, "image/bmp");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  if (httpd_resp_send_chunk(req, (const char*)header, sizeof(header)) !=
      ESP_OK) {
    return ESP_FAIL;
  }
  // BMP rows are bottom-up; LVGL gives top-down. Send last row first.
  for (int32_t y = (int32_t)h - 1; y >= 0; --y) {
    const char* row =
        reinterpret_cast<const char*>(&result->pixels[y * row_bytes]);
    if (httpd_resp_send_chunk(req, row, row_bytes) != ESP_OK) {
      return ESP_FAIL;
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);  // end chunked transfer
  ESP_LOGI(TAG, "screenshot served %ux%u (%u bytes)", (unsigned)w,
           (unsigned)h, (unsigned)(70 + row_bytes * h));
  return ESP_OK;
}

esp_err_t hello_get(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  const std::string& name = layout_manager().active_name();
  ApplySource src = layout_manager().active_source();
  const char* src_str = "boot";
  switch (src) {
    case ApplySource::BootStore:    src_str = "littlefs";    break;
    case ApplySource::BootDefault:  src_str = "default";     break;
    case ApplySource::BootFetched:  src_str = "applicationData"; break;
    case ApplySource::PostLayout:   src_str = "post";        break;
    case ApplySource::Boot: default: src_str = "boot";       break;
  }
  // Single buffer; widget catalog is small enough to inline.
  char buf[1024];
  snprintf(buf, sizeof(buf),
      "{"
        "\"schema\":1,"
        "\"name\":\"%s\","
        "\"hostname\":\"p4-cockpit\","
        "\"firmware\":\"p4-cockpit-jlp-0.1.0\","
        "\"display\":{\"w\":1024,\"h\":600},"
        "\"widgets\":{"
          "\"label\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"display\"]},"
          "\"toggle\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\"]},"
          "\"arc\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"display\",\"min\",\"max\",\"start_angle\",\"end_angle\"]},"
          "\"bar\":{\"fields\":[\"x\",\"y\",\"w\",\"h\",\"label\",\"bind\",\"display\",\"min\",\"max\",\"vertical\"]}"
        "},"
        "\"active_layout_name\":\"%s\","
        "\"layout_source\":\"%s\""
      "}",
      name.c_str(), name.c_str(), src_str);
  httpd_resp_sendstr(req, buf);
  return ESP_OK;
}

}  // namespace

void http_api_start(uint16_t port) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.stack_size = 8192;
  config.ctrl_port = 32768 + port;  // unique per httpd instance
  config.recv_wait_timeout = 120;
  config.send_wait_timeout = 30;
  config.lru_purge_enable = true;
  config.max_uri_handlers = 5;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "failed to start jlp api on port %u", port);
    return;
  }

  httpd_uri_t layout_uri = {
      .uri = "/layout",
      .method = HTTP_POST,
      .handler = layout_post,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &layout_uri);

  httpd_uri_t hz_uri = {
      .uri = "/healthz",
      .method = HTTP_GET,
      .handler = healthz_get,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &hz_uri);

  httpd_uri_t hello_uri = {
      .uri = "/hello",
      .method = HTTP_GET,
      .handler = hello_get,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &hello_uri);

  httpd_uri_t screenshot_uri = {
      .uri = "/screenshot",
      .method = HTTP_GET,
      .handler = screenshot_get,
      .user_ctx = nullptr,
  };
  httpd_register_uri_handler(server, &screenshot_uri);

  ESP_LOGI(
      TAG,
      "jlp api on :%u (POST /layout, GET /hello, /healthz, /screenshot)",
      port);
}

}  // namespace jlp
