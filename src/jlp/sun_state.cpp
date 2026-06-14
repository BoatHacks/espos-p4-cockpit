#include "sun_state.h"

#include <Arduino.h>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "sensesp.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp/system/lambda_consumer.h"
#include "sensesp_app.h"

static const char* TAG = "jlp.sun";

namespace jlp {

namespace {

// "2026-06-13T18:34:12.000Z" → Unix seconds (UTC). Returns 0 on parse
// fail or a non-Z (non-UTC) suffix; SK spec says these are UTC.
time_t parse_iso_utc(const char* s) {
  if (!s || !*s) return 0;
  struct tm t = {};
  // strptime is fine on ESP-IDF; consumes through the seconds field.
  // Anything after (fractional sec, "Z") is ignored — we don't need
  // sub-second precision for sunrise / sunset.
  if (!strptime(s, "%Y-%m-%dT%H:%M:%S", &t)) return 0;
  // timegm() isn't part of POSIX; use the SYSV portable substitute:
  // set TZ=UTC0 around mktime() so the offset is zero.
  char* old_tz = getenv("TZ");
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t out = mktime(&t);
  if (old_tz) setenv("TZ", old_tz, 1);
  else        unsetenv("TZ");
  tzset();
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
    // Two paths of interest, both ISO-8601 UTC strings.
    const char* p = path.c_str();
    if (!value.is<const char*>()) return;
    const char* iso = value.as<const char*>();
    // Copy the small string before crossing tasks; the value view
    // points into the WS-task-owned parent doc.
    std::string iso_owned(iso ? iso : "");
    if (strcmp(p, "environment.sun.sunsetTime") == 0) {
      sensesp::event_loop()->onDelay(0, [iso_owned]() {
        sun_state().set_sunset_iso(iso_owned.c_str());
      });
    } else if (strcmp(p, "environment.sun.sunriseTime") == 0) {
      sensesp::event_loop()->onDelay(0, [iso_owned]() {
        sun_state().set_sunrise_iso(iso_owned.c_str());
      });
    }
  });

  // SensESP opens the WS with subscribe=none. Add an explicit
  // subscribe for the two sun paths each time the WS (re)connects;
  // policy:"instant" so we get the value immediately on connect, then
  // again whenever the SK side recomputes (typically once a day, or
  // when position changes).
  ws->connect_to(new sensesp::LambdaConsumer<sensesp::SKWSConnectionState>(
      [](sensesp::SKWSConnectionState state) {
        if (state != sensesp::SKWSConnectionState::kSKWSConnected) return;
        auto a = sensesp::SensESPApp::get();
        if (!a) return;
        auto w = a->get_ws_client();
        if (!w) return;
        String sub =
            R"({"context":"vessels.self","subscribe":[)"
            R"({"path":"environment.sun.sunsetTime","format":"delta","policy":"instant"},)"
            R"({"path":"environment.sun.sunriseTime","format":"delta","policy":"instant"})"
            R"(]})";
        w->sendTXT(sub);
        ESP_LOGI(TAG, "sent environment.sun.* subscribe frame");
      }));

  ESP_LOGI(TAG, "subscribed to SK WS value callback (environment.sun.*)");
}

SunState& sun_state() {
  static SunState s;
  return s;
}

}  // namespace jlp
