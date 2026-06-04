#include "notifications_registry.h"

#include <Arduino.h>
#include <algorithm>

#include "esp_log.h"
#include "sensesp.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp_app.h"

static const char* TAG = "jlp.notifs";

namespace jlp {

void NotificationsRegistry::apply(const std::string& path_after_prefix,
                                  const JsonVariantConst& value) {
  // Treat a null value (path cleared) as removal.
  if (value.isNull()) {
    // A cleared path re-arms any local ack: if it fires again it
    // should pop the overlay anew.
    acked_.erase(path_after_prefix);
    if (map_.erase(path_after_prefix) > 0) {
      ESP_LOGI(TAG, "cleared %s", path_after_prefix.c_str());
      fire_observers();
    }
    return;
  }
  Notification n;
  n.path = path_after_prefix;
  n.message = value["message"] | "";
  n.state = parse_not_state(value["state"] | "normal");

  // Drop nominal/normal — they're "cleared" states in the SK convention.
  if (n.state == NotState::Nominal || n.state == NotState::Normal) {
    acked_.erase(path_after_prefix);  // cleared -> re-arm
    if (map_.erase(path_after_prefix) > 0) {
      ESP_LOGI(TAG, "cleared %s (state=%s)", path_after_prefix.c_str(),
               not_state_name(n.state));
      fire_observers();
    }
    return;
  }

  // If a previously-acked notification escalates above the level it
  // was acknowledged at, re-arm it so the overlay pops again.
  auto ack_it = acked_.find(path_after_prefix);
  if (ack_it != acked_.end() && n.state > ack_it->second) {
    ESP_LOGI(TAG, "%s escalated %s -> %s, re-arming",
             path_after_prefix.c_str(), not_state_name(ack_it->second),
             not_state_name(n.state));
    acked_.erase(ack_it);
  }

  auto it = map_.find(path_after_prefix);
  if (it != map_.end() && it->second.state == n.state &&
      it->second.message == n.message) {
    // No change.
    return;
  }
  map_[path_after_prefix] = n;
  ESP_LOGI(TAG, "%s = %s \"%s\"", path_after_prefix.c_str(),
           not_state_name(n.state), n.message.c_str());
  fire_observers();
}

const Notification* NotificationsRegistry::most_severe() const {
  const Notification* best = nullptr;
  for (const auto& kv : map_) {
    if (acked_.count(kv.first)) continue;  // locally acknowledged
    if (!best || kv.second.state > best->state) best = &kv.second;
  }
  return best;
}

std::vector<Notification> NotificationsRegistry::snapshot() const {
  std::vector<Notification> out;
  out.reserve(map_.size());
  for (const auto& kv : map_) {
    if (acked_.count(kv.first)) continue;  // locally acknowledged
    out.push_back(kv.second);
  }
  std::sort(out.begin(), out.end(),
            [](const Notification& a, const Notification& b) {
              return a.state > b.state;  // descending by severity
            });
  return out;
}

void NotificationsRegistry::acknowledge(const std::string& path_after_prefix) {
  auto it = map_.find(path_after_prefix);
  if (it == map_.end()) {
    // Nothing tracked under this path; nothing to ack.
    return;
  }
  acked_[path_after_prefix] = it->second.state;
  ESP_LOGI(TAG, "acked %s (state=%s)", path_after_prefix.c_str(),
           not_state_name(it->second.state));
  fire_observers();
}

bool NotificationsRegistry::is_acknowledged(
    const std::string& path_after_prefix) const {
  return acked_.count(path_after_prefix) > 0;
}

void NotificationsRegistry::fire_observers() {
  // Snapshot first because callbacks might (legitimately) call
  // off_change() during the fire loop.
  std::vector<Slot> snapshot = observers_;
  for (const auto& s : snapshot) s.cb();
}

void NotificationsRegistry::hook_sk_ws() {
  auto app = sensesp::SensESPApp::get();
  if (!app) {
    ESP_LOGW(TAG, "no SensESPApp yet — hook_sk_ws must be called after builder");
    return;
  }
  auto ws = app->get_ws_client();
  if (!ws) {
    ESP_LOGW(TAG, "no WS client — hook_sk_ws skipped");
    return;
  }
  ws->on_value([](const String& path, const JsonVariantConst& value) {
    // Filter: we only care about notifications.* paths. Strip the
    // prefix so registry keys are compact.
    const char* p = path.c_str();
    constexpr const char* kPrefix = "notifications.";
    constexpr size_t kPrefixLen = 14;  // strlen("notifications.")
    if (strncmp(p, kPrefix, kPrefixLen) != 0) return;
    std::string suffix(p + kPrefixLen);

    // Snapshot the value before the WS task moves on. JsonVariantConst
    // is a view into the parent doc; copy into a local JsonDocument so
    // the event_loop callback has a stable reference.
    JsonDocument doc;
    doc.set(value);
    sensesp::event_loop()->onDelay(0, [suffix, doc = std::move(doc)]() {
      notifications().apply(suffix, doc.as<JsonVariantConst>());
    });
  });
  ESP_LOGI(TAG, "subscribed to SK WS value callback (notifications.*)");
}

NotificationsRegistry& notifications() {
  static NotificationsRegistry r;
  return r;
}

}  // namespace jlp
