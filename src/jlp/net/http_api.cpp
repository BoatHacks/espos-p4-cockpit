#include "http_api.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sensesp.h"
#include <memory>
#include <string>

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
  config.max_uri_handlers = 4;

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

  ESP_LOGI(TAG, "jlp api on :%u (POST /layout, GET /hello, GET /healthz)",
           port);
}

}  // namespace jlp
