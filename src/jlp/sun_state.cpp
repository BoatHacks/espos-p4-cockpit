#include "sun_state.h"

#include <Arduino.h>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "sensesp.h"
#include "sensesp/signalk/signalk_value_listener.h"
#include "sensesp/system/lambda_consumer.h"

static const char* TAG = "jlp.sun";

namespace jlp {

namespace {

// "2026-06-13T18:34:12.000Z" → Unix seconds (UTC). Returns 0 on parse
// fail, a non-Z (non-UTC) suffix, or any mktime overflow / DST quirk;
// SK spec says these are UTC.
time_t parse_iso_utc(const char* s) {
  if (!s || !*s) return 0;
  struct tm t = {};
  // strptime returns a pointer to the first unconsumed char so we can
  // verify the suffix. SK always sends 'Z' (optionally preceded by
  // fractional seconds we don't care about) — reject anything that
  // doesn't end with Z so we never silently interpret a local-time
  // string as UTC.
  const char* tail = strptime(s, "%Y-%m-%dT%H:%M:%S", &t);
  if (!tail) return 0;
  // Skip an optional fractional-seconds run (".123", ".000" etc.).
  if (*tail == '.') {
    ++tail;
    while (*tail >= '0' && *tail <= '9') ++tail;
  }
  if (*tail != 'Z' || *(tail + 1) != '\0') return 0;
  // timegm() isn't part of POSIX; use the SYSV portable substitute:
  // set TZ=UTC0 around mktime() so the offset is zero.
  char* old_tz = getenv("TZ");
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t out = mktime(&t);
  if (old_tz) setenv("TZ", old_tz, 1);
  else        unsetenv("TZ");
  tzset();
  // mktime returns -1 on overflow / DST-ambiguity / invalid date;
  // normalise to 0 so callers and has_data()'s `> 0` check don't
  // need to special-case the sentinel.
  if (out == (time_t)-1) return 0;
  return out;
}

// ±30 min twilight buffer either side. Gives some dusk margin before
// the dimmer engages and pre-dawn warning before it disarms.
constexpr time_t kTwilightBufferSec = 30 * 60;

}  // namespace

void SunState::set_sunset_iso(const char* iso) {
  time_t t = parse_iso_utc(iso);
  if (t == 0) {
    ESP_LOGW(TAG, "sunset parse failed: %s", iso ? iso : "(null)");
    return;
  }
  if (sunset_unix_ == t) return;
  sunset_unix_ = t;
  ESP_LOGI(TAG, "sunset=%lld (%s)", (long long)t, iso);
}

void SunState::set_sunrise_iso(const char* iso) {
  time_t t = parse_iso_utc(iso);
  if (t == 0) {
    ESP_LOGW(TAG, "sunrise parse failed: %s", iso ? iso : "(null)");
    return;
  }
  if (sunrise_unix_ == t) return;
  sunrise_unix_ = t;
  ESP_LOGI(TAG, "sunrise=%lld (%s)", (long long)t, iso);
}

DayNight SunState::classify() const {
  if (!has_data()) return DayNight::Unknown;
  const time_t now = time(nullptr);
  // "Night" runs from `sunset + buffer` to `sunrise - buffer`. SK
  // publishes the NEXT occurrence of each, so the comparison only
  // needs to look at the relative ordering of (now, sunrise, sunset)
  // to know which side of the day/night line we're on:
  //
  //   - if next event is sunrise → we're currently in night
  //     (the sun has already set; the buffer is applied to both sides)
  //   - if next event is sunset → we're currently in day
  const time_t night_start = sunset_unix_ + kTwilightBufferSec;
  const time_t day_start   = sunrise_unix_ + kTwilightBufferSec;
  // Whichever is sooner is the next transition. Inverse tells us
  // which mode we're in now.
  if (sunrise_unix_ < sunset_unix_) {
    // Sunrise comes before sunset in the sequence — meaning the next
    // sunset hasn't happened yet, so we're in day until then.
    if (now >= sunset_unix_ + kTwilightBufferSec) return DayNight::Night;
    if (now <  sunrise_unix_ - kTwilightBufferSec) return DayNight::Night;
    return DayNight::Day;
  }
  // sunrise > sunset — sunrise is the next event, so we're in night.
  if (now < night_start) return DayNight::Day;  // pre-buffer dusk
  if (now >= day_start)  return DayNight::Day;  // shouldn't happen if
                                                // data is fresh, but
                                                // be safe
  return DayNight::Night;
}

void SunState::hook_sk_ws() {
  // Use per-path SKValueListener instead of the global on_value hook:
  //
  // - on_value is a single-slot setter — calling it after
  //   notifications_registry already set its own callback would
  //   replace that callback and break wake-on-escalation. SKListener
  //   composes cleanly with everything else.
  // - on_value fires for every WS delta (hundreds per second on a
  //   busy boat). The previous implementation walked every delta to
  //   strcmp the path and allocated a std::string for each
  //   string-valued one, which kept the WS task busy enough to
  //   starve event_loop and time out POST /layout. Per-path
  //   listeners do the filtering upstream in SensESP.
  // - SensESP includes the listener's path in its next subscribe
  //   frame automatically, so we don't need a separate connect-state
  //   hook to send a manual subscribe envelope.
  constexpr int kListenDelayMs = 1000;
  auto* sunset = new sensesp::SKValueListener<String>(
      "environment.sun.sunsetTime", kListenDelayMs);
  sunset->connect_to(std::make_shared<sensesp::LambdaConsumer<String>>(
      [](String iso) {
        // Listener fires on the SensESP main task; SunState::set_*
        // is plain data assignment with no LVGL calls, safe to run
        // here directly. Copy the String to std::string first so the
        // capture doesn't outlive the listener's transient.
        std::string s(iso.c_str());
        sun_state().set_sunset_iso(s.c_str());
      }));

  auto* sunrise = new sensesp::SKValueListener<String>(
      "environment.sun.sunriseTime", kListenDelayMs);
  sunrise->connect_to(std::make_shared<sensesp::LambdaConsumer<String>>(
      [](String iso) {
        std::string s(iso.c_str());
        sun_state().set_sunrise_iso(s.c_str());
      }));

  ESP_LOGI(TAG, "subscribed to environment.sun.sunsetTime + sunriseTime");
}

SunState& sun_state() {
  static SunState s;
  return s;
}

}  // namespace jlp
