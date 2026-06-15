#pragma once

#include <stdint.h>

namespace jlp {

/** Day / night classifier driven by SignalK `environment.mode`
 *  ("day" / "night"), as published by signalk-derived-data's suncalc.
 *  Used by the idle dimmer so the panel only goes dark at night.
 *
 *  derived-data already applies civil-twilight phases (it reports
 *  `mode: "night"` only once it's past dusk, and back to "day" at
 *  dawn), so the firmware no longer computes its own twilight buffer.
 *
 *  Fail-safe behaviour: until SK has delivered a value, classify as
 *  Day so the helm stays bright. Dimming while the operator needs the
 *  display is far worse than not saving power before the first delta.
 */
enum class DayNight : uint8_t { Unknown, Day, Night };

class SunState {
 public:
  /** Subscribe to environment.mode over the SK WS. Idempotent — safe
   *  to call once at boot only. */
  void hook_sk_ws();

  /** Best-effort current state. Returns Unknown until the first
   *  environment.mode delta arrives. */
  DayNight classify() const { return state_; }

  /** True once SK has delivered a usable environment.mode value.
   *  Mainly for diagnostics. */
  bool has_data() const { return state_ != DayNight::Unknown; }

  /** Setter used by the SK listener. Accepts "day" / "night"
   *  (case-insensitive); anything else leaves the state unchanged. */
  void set_mode(const char* mode);

 private:
  DayNight state_ = DayNight::Unknown;
};

SunState& sun_state();

}  // namespace jlp
