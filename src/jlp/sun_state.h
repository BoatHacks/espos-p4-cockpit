#pragma once

#include <stdint.h>
#include <time.h>

namespace jlp {

/** Day / night classifier driven by SignalK `environment.sun.sunsetTime`
 *  and `sunriseTime` (ISO-8601 UTC strings, refreshed per SK spec at
 *  least daily). Used by the idle dimmer so the panel only goes dark
 *  at night.
 *
 *  Fail-safe behaviour: when SK has never delivered either path, or
 *  when the system clock isn't synced yet, classify as Day so the
 *  helm stays bright. The user's question if-it-dims-while-I-need-it
 *  is much worse than the symmetric we-don't-save-power-while-no-time.
 *
 *  ±30 min civil-twilight buffer either side of sunset / sunrise:
 *  "night" starts 30 min after sunset and ends 30 min before sunrise.
 *  Gives the operator some dusk margin before the helm dims and
 *  some pre-dawn warning before it re-arms its idle timer.
 */
enum class DayNight : uint8_t { Unknown, Day, Night };

class SunState {
 public:
  /** Subscribe to environment.sun.sunsetTime + sunriseTime over the
   *  SK WS. Idempotent — safe to call once at boot only. */
  void hook_sk_ws();

  /** Best-effort current state. Returns Unknown when either timestamp
   *  is missing or `time(nullptr)` returns 0 (SNTP not yet synced). */
  DayNight classify() const;

  /** True iff we have a usable time source AND both sunset / sunrise
   *  timestamps. Mainly for diagnostics. */
  bool has_data() const {
    return sunset_unix_ > 0 && sunrise_unix_ > 0 && time(nullptr) > 1000000000;
  }

  // Setters used by the SK listeners. Exposed only because the listener
  // callback fires on the WS task and we marshal onto event_loop via
  // onDelay(0, ...) — the marshalled lambda calls these.
  void set_sunset_iso(const char* iso);
  void set_sunrise_iso(const char* iso);

 private:
  // Unix seconds (UTC). 0 means "not yet received".
  time_t sunset_unix_ = 0;
  time_t sunrise_unix_ = 0;
};

SunState& sun_state();

}  // namespace jlp
