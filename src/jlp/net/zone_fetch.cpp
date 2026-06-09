#include "zone_fetch.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include "sensesp.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../zone_registry.h"

static const char* TAG = "jlp.zonefetch";

namespace jlp {

namespace {

constexpr size_t kMaxMetaBytes = 8 * 1024;

// Replace "." with "/" so SK's REST endpoint accepts the path. SK
// rejects dotted paths in URL segments — `tanks.fuel.1.currentLevel`
// → `tanks/fuel/1/currentLevel`.
std::string slashify(const std::string& path) {
  std::string out;
  out.reserve(path.size());
  for (char c : path) out.push_back(c == '.' ? '/' : c);
  return out;
}

struct FetchArgs {
  std::string host;
  uint16_t port;
  std::vector<std::string> paths;
};

void fetch_one(esp_http_client_handle_t client, const std::string& url,
               const std::string& path) {
  esp_http_client_set_url(client, url.c_str());
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGD(TAG, "%s: open failed (%s)", path.c_str(),
             esp_err_to_name(err));
    return;
  }
  int content_length = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGD(TAG, "%s: HTTP %d", path.c_str(), status);
    esp_http_client_close(client);
    return;
  }
  size_t cap = (content_length > 0 && (size_t)content_length < kMaxMetaBytes)
                   ? (size_t)content_length
                   : kMaxMetaBytes;
  std::string body;
  body.resize(cap);
  int total = 0;
  while (total < (int)cap) {
    int n = esp_http_client_read(client, &body[total], cap - total);
    if (n <= 0) break;
    total += n;
  }
  body.resize(total);
  esp_http_client_close(client);
  if (total == 0) return;

  // Parse + push on event_loop (LVGL / registry single-writer).
  auto doc_owned = std::make_shared<JsonDocument>();
  DeserializationError de = deserializeJson(*doc_owned, body);
  if (de) {
    ESP_LOGW(TAG, "%s: parse failed (%s)", path.c_str(), de.c_str());
    return;
  }
  // Only push if there's actually a zones array or description — saves
  // map churn on paths with no useful metadata.
  if (doc_owned->as<JsonObjectConst>()["zones"].isNull() &&
      doc_owned->as<JsonObjectConst>()["description"].isNull()) {
    return;
  }
  std::string p = path;
  sensesp::event_loop()->onDelay(0, [p, doc_owned]() mutable {
    zones().apply_meta(p, doc_owned->as<JsonObjectConst>());
  });
}

void fetch_task(void* arg) {
  auto* a = static_cast<FetchArgs*>(arg);

  // One http client reused across paths — esp_http_client supports
  // setting a new URL on the existing handle, which avoids the per-
  // request TCP handshake.
  esp_http_client_config_t cfg = {};
  cfg.url = "http://placeholder";  // will be overridden per-path
  cfg.timeout_ms = 3000;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGW(TAG, "http client init failed");
    delete a;
    vTaskDelete(NULL);
    return;
  }

  const std::string base =
      "http://" + a->host + ":" + std::to_string(a->port) +
      "/signalk/v1/api/vessels/self/";
  for (const auto& path : a->paths) {
    std::string url = base + slashify(path) + "/meta";
    fetch_one(client, url, path);
  }
  esp_http_client_cleanup(client);
  ESP_LOGI(TAG, "fetched meta for %u paths",
           (unsigned)a->paths.size());

  delete a;
  vTaskDelete(NULL);
}

}  // namespace

void zone_fetch_for_paths(const std::string& sk_host, uint16_t sk_port,
                          const std::vector<std::string>& paths) {
  if (paths.empty()) return;
  auto* a = new FetchArgs{sk_host, sk_port, paths};
  if (xTaskCreate(fetch_task, "jlp_zonefetch", 8192, a, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to spawn zone fetch task");
    delete a;
  }
}

}  // namespace jlp
