#include "notifications_registry.h"

#include <Arduino.h>
#include <algorithm>

#include "esp_log.h"
#include "sensesp.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp/system/lambda_consumer.h"
#include "sensesp_app.h"

static const char* TAG = "jlp.notifs";

namespace jlp {

void NotificationsRegistry::apply(const std::string& path_after_prefix,
                                  const JsonVariantConst& value) {
  // Default: not an escalation. Each branch that fire_observers()
  // sets this true only when a new notification appeared at >= Alert
  // or an existing one rose to a more severe state. Reset at the top
  // so a previous escalation doesn't bleed into a later non-event
  // (the flag is read by main.cpp's wake hook only).
  last_change_was_escalation_ = false;

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

  // Cleared (nominal/normal): re-arm any local ack so a future
  // alert state pops the overlay anew. Keep the entry in the map
  // so a list widget with include_cleared=true can still show it;
  // most_severe() and the default snapshot() skip these by their
  // own filters (severity threshold + the include_cleared param).
  if (n.state == NotState::Nominal || n.state == NotState::Normal) {
    acked_.erase(path_after_prefix);  // cleared -> re-arm
    auto it = map_.find(path_after_prefix);
    if (it != map_.end() && it->second.state == n.state &&
        it->second.message == n.message) {
      return;  // no change
    }
    map_[path_after_prefix] = n;
    ESP_LOGI(TAG, "cleared %s (state=%s)", path_after_prefix.c_str(),
             not_state_name(n.state));
    fire_observers();
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
  // Escalation = either a brand-new path firing at >= Alert, or an
  // existing path going from a less-severe state to a more-severe
  // one. Same-or-lower-severity updates (or pure message edits at
  // the same level) don't qualify — the operator already saw the
  // higher state on the previous fire.
  const bool was_new = it == map_.end();
  const NotState prev_state = was_new ? NotState::Normal : it->second.state;
  last_change_was_escalation_ = n.state > prev_state;
  map_[path_after_prefix] = n;
  ESP_LOGI(TAG, "%s = %s \"%s\"%s", path_after_prefix.c_str(),
           not_state_name(n.state), n.message.c_str(),
           last_change_was_escalation_ ? " (escalation)" : "");
  fire_observers();
}

const Notification* NotificationsRegistry::most_severe() const {
  const Notification* best = nullptr;
  for (const auto& kv : map_) {
    if (acked_.count(kv.first)) continue;  // locally acknowledged
    if (kv.second.state == NotState::Nominal ||
        kv.second.state == NotState::Normal) {
      continue;  // cleared — kept in the map for list views, not "pending"
    }
    if (!best || kv.second.state > best->state) best = &kv.second;
  }
  return best;
}

std::vector<Notification> NotificationsRegistry::snapshot(
    bool include_cleared) const {
  std::vector<Notification> out;
  out.reserve(map_.size());
  for (const auto& kv : map_) {
    // Ack suppresses the alert-overlay popup, NOT the list. The
    // condition is still live on the bus, and the operator needs to
    // see it (red row + alarm message) to remain aware of it. Only
    // most_severe() (the overlay's driver) excludes acked paths.
    if (!include_cleared &&
        (kv.second.state == NotState::Nominal ||
         kv.second.state == NotState::Normal)) {
      continue;
    }
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

void NotificationsRegistry::drain_pending() {
  // Take the buffer under the lock as quickly as possible, then run
  // apply() on each item outside the lock so the WS callback can
  // keep appending. apply() fires observers, which on the event_loop
  // task is allowed to be slow (it runs LVGL through the alert
  // overlay etc.) — must not block the WS task on it.
  std::vector<Pending> batch;
  if (xSemaphoreTake(pending_mutex_, 0) == pdTRUE) {
    batch.swap(pending_);
    xSemaphoreGive(pending_mutex_);
  } else {
    return;  // WS is appending; pick up next tick
  }
  for (auto& p : batch) {
    apply(p.suffix, p.doc.as<JsonVariantConst>());
  }
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

  // Static mutex so we don't need a destructor / leak a heap one.
  pending_mutex_ = xSemaphoreCreateMutexStatic(&pending_mutex_buffer_);

  // WS task: append to the pending buffer. NO per-delta event_loop
  // onDelay — that was wedging event_loop's queue when SK delivered
  // 50+ notifications.* values inside a single ms (typical post-
  // reconnect storm). Single mutex take per delta; cheap.
  ws->on_value([](const String& path, const JsonVariantConst& value) {
    const char* p = path.c_str();
    constexpr const char* kPrefix = "notifications.";
    constexpr size_t kPrefixLen = 14;
    if (strncmp(p, kPrefix, kPrefixLen) != 0) return;
    Pending entry;
    entry.suffix.assign(p + kPrefixLen);
    entry.doc.set(value);
    auto& reg = notifications();
    if (!reg.pending_mutex_) return;  // hook_sk_ws not finished yet
    if (xSemaphoreTake(reg.pending_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
      reg.pending_.push_back(std::move(entry));
      xSemaphoreGive(reg.pending_mutex_);
    }
    // On lock contention we drop the delta. The 10 ms timeout is
    // generous compared to the drain critical section (microseconds);
    // a miss here means the consumer was wedged for >10 ms which is
    // a separate problem worth surfacing as a missed notification.
  });

  // event_loop drains the buffer at 50 ms cadence. Bounded queue
  // pressure regardless of WS rate; up to 50 ms of latency before
  // an alarm reaches the overlay, which is well below human
  // perception for a helm display.
  sensesp::event_loop()->onRepeat(50, []() {
    notifications().drain_pending();
  });

  // SensESP opens the WS with `subscribe=none`; the per-listener
  // subscribe machinery only adds paths that have an SKListener
  // registered. We don't (and can't, dynamically) register one per
  // notification path — they appear and disappear at runtime. Send
  // an explicit wildcard subscribe so the server streams every
  // notifications.* value delta to us.
  //
  // Wait until the WS is connected; SensESP fires the connection
  // state into its ValueProducer, so subscribe via the event loop
  // each time we (re)connect.
  ws->connect_to(std::make_shared<sensesp::LambdaConsumer<sensesp::SKWSConnectionState>>(
      [](sensesp::SKWSConnectionState state) {
        if (state != sensesp::SKWSConnectionState::kSKWSConnected) return;
        auto a = sensesp::SensESPApp::get();
        if (!a) return;
        auto w = a->get_ws_client();
        if (!w) return;
        String sub =
            R"({"context":"vessels.self","subscribe":[)"
            R"({"path":"notifications.*","format":"delta","policy":"instant"})"
            R"(]})";
        w->sendTXT(sub);
        ESP_LOGI(TAG, "sent notifications.* subscribe frame");
      }));

  ESP_LOGI(TAG, "subscribed to SK WS value callback (notifications.*, batched)");
}

NotificationsRegistry& notifications() {
  static NotificationsRegistry r;
  return r;
}

}  // namespace jlp
