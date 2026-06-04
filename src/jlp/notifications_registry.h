#pragma once

#include <stdint.h>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <ArduinoJson.h>

namespace jlp {

/** SK notification state, ordered by severity. Used by the alert
 *  overlay's `min_state` filter and by the list widget's optional
 *  per-row state colouring. */
enum class NotState : uint8_t {
  Nominal,    // 0 — least severe, "all good"
  Normal,
  Alert,
  Warn,
  Alarm,
  Emergency   // 5 — most severe
};

inline NotState parse_not_state(const char* s) {
  if (!s) return NotState::Normal;
  if (!strcmp(s, "nominal"))   return NotState::Nominal;
  if (!strcmp(s, "normal"))    return NotState::Normal;
  if (!strcmp(s, "alert"))     return NotState::Alert;
  if (!strcmp(s, "warn"))      return NotState::Warn;
  if (!strcmp(s, "alarm"))     return NotState::Alarm;
  if (!strcmp(s, "emergency")) return NotState::Emergency;
  return NotState::Normal;
}

inline const char* not_state_name(NotState s) {
  switch (s) {
    case NotState::Nominal:   return "nominal";
    case NotState::Normal:    return "normal";
    case NotState::Alert:     return "alert";
    case NotState::Warn:      return "warn";
    case NotState::Alarm:     return "alarm";
    case NotState::Emergency: return "emergency";
  }
  return "normal";
}

/** A single notification snapshot. `path` is the part after
 *  "notifications." for compact display; the full SK path is
 *  reconstructed when needed for PUTs. */
struct Notification {
  std::string path;     // e.g. "mob.<uuid>" (after "notifications.")
  std::string message;
  NotState state;
};

/** Tracks the live set of `notifications.*` paths broadcast by SK.
 *  Fires on_change(...) whenever the set or any entry's state
 *  changes; consumers (alert overlay, list widget) re-query the
 *  registry on each change. */
class NotificationsRegistry {
 public:
  /** Wire into the SK WS client's value callback at boot. Idempotent. */
  void hook_sk_ws();

  /** Manually feed a notification delta. Normally not called by user
   *  code — the WS callback does this. */
  void apply(const std::string& path_after_prefix,
             const JsonVariantConst& value);

  /** Most severe currently-pending notification, or nullptr if all
   *  are normal/nominal/cleared. Acknowledged notifications are
   *  skipped (see acknowledge()). */
  const Notification* most_severe() const;

  /** All currently-tracked notifications, sorted by severity
   *  descending. Acknowledged notifications are excluded. */
  std::vector<Notification> snapshot() const;

  /** Locally acknowledge a notification by path. The notification
   *  stays in the registry (the bus condition is still live) but is
   *  suppressed from most_severe()/snapshot() — so the alert overlay
   *  dismisses and the device stays usable, regardless of whether the
   *  alarm clears on the N2K bus. Fires on_change().
   *
   *  The ack auto-clears (the notification re-arms) when the path
   *  goes to normal/nominal, OR when its state escalates above the
   *  level it was acked at (e.g. alarm -> emergency re-pops). */
  void acknowledge(const std::string& path_after_prefix);

  /** True if `path` is currently acknowledged. */
  bool is_acknowledged(const std::string& path_after_prefix) const;

  /** Register a change observer. Returns an opaque token; the caller
   *  must call `off_change(token)` before any captured pointer is
   *  destroyed. Fires on the event_loop task. */
  using Observer = std::function<void()>;
  using ObserverToken = uint32_t;
  ObserverToken on_change(Observer cb) {
    ObserverToken tok = next_token_++;
    observers_.push_back({tok, std::move(cb)});
    return tok;
  }
  void off_change(ObserverToken token) {
    for (auto it = observers_.begin(); it != observers_.end(); ++it) {
      if (it->token == token) {
        observers_.erase(it);
        return;
      }
    }
  }

 private:
  void fire_observers();

  struct Slot {
    ObserverToken token;
    Observer cb;
  };
  std::unordered_map<std::string, Notification> map_;
  // Locally-acknowledged paths -> the severity they were acked at.
  // Used to suppress the overlay while the bus keeps re-asserting,
  // and to re-arm if the condition later escalates.
  std::unordered_map<std::string, NotState> acked_;
  std::vector<Slot> observers_;
  ObserverToken next_token_ = 1;
};

NotificationsRegistry& notifications();

}  // namespace jlp
