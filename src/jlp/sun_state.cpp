#include "sun_state.h"

#include <Arduino.h>
#include <strings.h>

#include "esp_log.h"
#include "sensesp.h"
#include "sensesp/signalk/signalk_value_listener.h"
#include "sensesp/system/lambda_consumer.h"

static const char* TAG = "jlp.sun";

namespace jlp {

void SunState::set_mode(const char* mode) {
  if (!mode || !*mode) return;
  DayNight next;
  if (strcasecmp(mode, "day") == 0) {
    next = DayNight::Day;
  } else if (strcasecmp(mode, "night") == 0) {
    next = DayNight::Night;
  } else {
    // derived-data only ever publishes "day" or "night" on
    // environment.mode (the richer phase string lives on
    // environment.sun); ignore anything unexpected rather than
    // guessing.
    ESP_LOGW(TAG, "unexpected environment.mode: %s", mode);
    return;
  }
  if (state_ == next) return;
  state_ = next;
  ESP_LOGI(TAG, "mode=%s", mode);
}

void SunState::hook_sk_ws() {
  // A single exact path (environment.mode), so a plain per-path
  // SKValueListener is the right fit — it filters upstream in SensESP
  // and joins the next subscribe frame automatically. (Wildcard
  // families like notifications.* use SKPrefixListener instead.)
  constexpr int kListenDelayMs = 1000;
  auto* mode = new sensesp::SKValueListener<String>("environment.mode",
                                                    kListenDelayMs);
  mode->connect_to(std::make_shared<sensesp::LambdaConsumer<String>>(
      [](String m) {
        // Fires on the SensESP main task; set_mode is plain data
        // assignment (no LVGL calls), safe to run here directly.
        std::string s(m.c_str());
        sun_state().set_mode(s.c_str());
      }));

  ESP_LOGI(TAG, "subscribed to environment.mode");
}

SunState& sun_state() {
  static SunState s;
  return s;
}

}  // namespace jlp
