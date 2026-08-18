#include "layout_fetch.h"

#include "cockpit_hal/ui.h"
#include "esp_http_client.h"
#include "espos_sk.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../layout/layout_manager.h"

static const char* TAG = "jlp.fetch";

namespace jlp {

namespace {

constexpr size_t kMaxBodyBytes = 64 * 1024;

// Pulled into its own task so the blocking esp_http_client_perform
// doesn't stall the event_loop. The fetched body is handed back to
// LayoutManager via a one-shot onDelay(0) so the apply runs on the
// event_loop task (LVGL single-writer).
struct FetchArgs {
  std::string host;
  uint16_t port;
  std::string token;   // espOS access token ("" on an open server)
};

void fetch_task(void* arg) {
  auto* a = static_cast<FetchArgs*>(arg);
  std::string url =
      "http://" + a->host + ":" + std::to_string(a->port) +
      "/signalk/v1/applicationData/global/p4-cockpit/1/layout.json";

  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 5000;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGW(TAG, "http client init failed");
    delete a;
    vTaskDelete(NULL);
    return;
  }
  if (!a->token.empty()) {
    std::string auth = "Bearer " + a->token;
    esp_http_client_set_header(client, "Authorization", auth.c_str());
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "applicationData unreachable (%s); keeping current layout",
             esp_err_to_name(err));
    esp_http_client_cleanup(client);
    delete a;
    vTaskDelete(NULL);
    return;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGI(TAG, "applicationData returned %d; keeping current layout",
             status);
    esp_http_client_cleanup(client);
    delete a;
    vTaskDelete(NULL);
    return;
  }

  // content_length is -1 if chunked; we still cap the read.
  size_t cap = (content_length > 0 && (size_t)content_length < kMaxBodyBytes)
                   ? content_length
                   : kMaxBodyBytes;
  auto* body = new std::string();
  body->resize(cap);
  int total = 0;
  while (total < (int)cap) {
    int n = esp_http_client_read(client, &(*body)[total], cap - total);
    if (n <= 0) break;
    total += n;
  }
  body->resize(total);
  esp_http_client_cleanup(client);

  ESP_LOGI(TAG, "fetched %d bytes from applicationData", total);
  cockpit_hal::ui::post([body]() mutable {
    auto r = layout_manager().apply(*body, ApplySource::BootFetched);
    if (!r.ok) {
      ESP_LOGW(TAG, "fetched layout rejected: %s", r.err.c_str());
    }
    delete body;
  });

  delete a;
  vTaskDelete(NULL);
}

}  // namespace

void layout_fetch_async_apply(const std::string& sk_host, uint16_t sk_port) {
  // Defer kickoff so the WiFi connect has time to complete after setup.
  cockpit_hal::ui::after(5000, [sk_host, sk_port]() {
    char tok[512] = "";
    (void)espos_sk_get_token(tok, sizeof(tok));
    auto* a = new FetchArgs{sk_host, sk_port, tok};
    if (xTaskCreate(fetch_task, "jlp_fetch", 8192, a, 4, NULL) != pdPASS) {
      ESP_LOGE(TAG, "failed to spawn fetch task");
      delete a;
    }
  });
}

}  // namespace jlp
