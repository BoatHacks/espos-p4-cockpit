#include "zone_registry.h"

#include <Arduino.h>
#include "esp_log.h"
#include "sensesp.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp_app.h"

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

const Zone* ZoneRegistry::match(const std::string& path,
                                float display_value) const {
  auto it = map_.find(path);
  if (it == map_.end()) return nullptr;
  for (const Zone& z : it->second) {
    if (display_value >= z.lower && display_value < z.upper) return &z;
  }
  return nullptr;
}

void ZoneRegistry::apply_meta(const std::string& path,
                              const JsonObjectConst& meta) {
  JsonArrayConst zarr = meta["zones"];
  if (zarr.isNull() || zarr.size() == 0) return;
  std::vector<Zone> zs;
  zs.reserve(zarr.size());
  for (JsonObjectConst z : zarr) {
    zs.push_back({z["lower"] | 0.f, z["upper"] | 0.f,
                  parse_state(z["state"] | "normal")});
  }
  map_[path] = std::move(zs);
  ESP_LOGI(TAG, "%s: %u zones loaded from WS meta", path.c_str(),
           (unsigned)map_[path].size());
}

void ZoneRegistry::hook_sk_ws() {
  // The WS callback fires on the WS task — marshal registry writes
  // onto event_loop so reads from match() on the main task are
  // race-free.
  auto app = sensesp::SensESPApp::get();
  if (!app) {
    ESP_LOGW(TAG, "no SensESPApp yet — hook_sk_ws() must be called after the builder");
    return;
  }
  auto ws = app->get_ws_client();
  if (!ws) {
    ESP_LOGW(TAG, "no WS client yet — hook_sk_ws skipped");
    return;
  }
  if (!ws) return;
  ws->on_meta([](const String& path, const JsonObject& meta) {
    // Copy out before the WS task moves on. ArduinoJson v7
    // JsonObjects are views into the parent doc; we need to snapshot.
    std::string path_owned(path.c_str());
    JsonDocument doc;
    doc.set(meta);
    sensesp::event_loop()->onDelay(0, [path_owned, doc = std::move(doc)]() {
      zones().apply_meta(path_owned, doc.as<JsonObjectConst>());
    });
  });
  ESP_LOGI(TAG, "subscribed to SK WS meta callback");
}

ZoneRegistry& zones() {
  static ZoneRegistry r;
  return r;
}

}  // namespace jlp
