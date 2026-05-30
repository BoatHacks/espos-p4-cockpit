#include "zone_registry.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <memory>
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensesp.h"

static const char* TAG = "jlp.zones";

namespace jlp {

namespace {

ZoneState parse_state(const char* s) {
  if (!s) return ZoneState::Normal;
  if (!strcmp(s, "nominal")) return ZoneState::Nominal;
  if (!strcmp(s, "normal")) return ZoneState::Normal;
  if (!strcmp(s, "alert")) return ZoneState::Alert;
  if (!strcmp(s, "warn")) return ZoneState::Warn;
  if (!strcmp(s, "alarm")) return ZoneState::Alarm;
  if (!strcmp(s, "emergency")) return ZoneState::Emergency;
  return ZoneState::Normal;
}

}  // namespace

uint32_t color_for_state(ZoneState s) {
  switch (s) {
    case ZoneState::Nominal:
    case ZoneState::Normal:    return 0x3fb950;  // green
    case ZoneState::Alert:     return 0x58a6ff;  // blue (accent)
    case ZoneState::Warn:      return 0xd29922;  // yellow
    case ZoneState::Alarm:     return 0xdb6d28;  // orange
    case ZoneState::Emergency: return 0xf85149;  // red
  }
  return 0x58a6ff;
}

void ZoneRegistry::set_sk_server(const std::string& host, uint16_t port) {
  sk_host_ = host;
  sk_port_ = port;
}

const Zone* ZoneRegistry::match(const std::string& path,
                                float display_value) const {
  auto it = map_.find(path);
  if (it == map_.end() || !it->second.loaded) return nullptr;
  for (const Zone& z : it->second.zones) {
    if (display_value >= z.lower && display_value < z.upper) return &z;
  }
  return nullptr;
}

struct FetchArgs {
  std::string url;
  std::string path;
};

void ZoneRegistry::fetch_task(void* arg) {
  auto* a = static_cast<FetchArgs*>(arg);

  esp_http_client_config_t cfg = {};
  cfg.url = a->url.c_str();
  cfg.timeout_ms = 5000;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGW(TAG, "http client init failed for %s", a->path.c_str());
    delete a;
    vTaskDelete(NULL);
    return;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "%s: meta unreachable (%s); zones disabled for this path",
             a->path.c_str(), esp_err_to_name(err));
    esp_http_client_cleanup(client);
    delete a;
    vTaskDelete(NULL);
    return;
  }

  esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    ESP_LOGI(TAG, "%s: meta returned %d", a->path.c_str(), status);
    esp_http_client_cleanup(client);
    delete a;
    vTaskDelete(NULL);
    return;
  }

  constexpr size_t kCap = 4096;
  auto* body = new std::string();
  body->resize(kCap);
  int total = 0;
  while (total < (int)kCap) {
    int n = esp_http_client_read(client, &(*body)[total], kCap - total);
    if (n <= 0) break;
    total += n;
  }
  body->resize(total);
  esp_http_client_cleanup(client);

  JsonDocument doc;
  if (deserializeJson(doc, *body)) {
    ESP_LOGW(TAG, "%s: meta parse failed", a->path.c_str());
    delete body;
    delete a;
    vTaskDelete(NULL);
    return;
  }

  std::vector<Zone> zones;
  for (JsonObjectConst z : doc["zones"].as<JsonArrayConst>()) {
    zones.push_back({z["lower"] | 0.f, z["upper"] | 0.f,
                     parse_state(z["state"] | "normal")});
  }

  ESP_LOGI(TAG, "%s: %u zones loaded", a->path.c_str(),
           (unsigned)zones.size());

  // Marshal the registry write back onto event_loop so reads from the
  // main task don't race.
  std::string path_owned = a->path;
  auto zones_shared = std::make_shared<std::vector<Zone>>(std::move(zones));
  sensesp::event_loop()->onDelay(0, [path_owned, zones_shared]() {
    auto& r = ::jlp::zones();
    r.map_[path_owned] = Entry{*zones_shared, true};
  });

  delete body;
  delete a;
  vTaskDelete(NULL);
}

void ZoneRegistry::fetch_async(const std::string& path) {
  if (sk_host_.empty() || sk_port_ == 0) return;
  auto it = map_.find(path);
  if (it != map_.end()) return;  // already loaded or in-flight
  map_[path] = Entry{};  // reserve

  std::string slashed = path;
  for (char& c : slashed)
    if (c == '.') c = '/';
  auto* a = new FetchArgs{
      "http://" + sk_host_ + ":" + std::to_string(sk_port_) +
          "/signalk/v1/api/vessels/self/" + slashed + "/meta",
      path};
  if (xTaskCreate(fetch_task, "jlp_zones", 8192, a, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to spawn zone fetch task for %s", path.c_str());
    delete a;
  }
}

ZoneRegistry& zones() {
  static ZoneRegistry r;
  return r;
}

}  // namespace jlp
