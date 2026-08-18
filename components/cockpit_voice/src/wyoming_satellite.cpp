/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution */
#include "cockpit_voice/wyoming_satellite.h"

#include "esp_log.h"

static const char* TAG = "voice";

namespace cockpit_voice {

void WyomingSatellite::start() {
  ESP_LOGW(TAG, "voice satellite not ported yet (phase 2): staying disconnected");
}
void WyomingSatellite::stop() {}
void WyomingSatellite::set_ptt_held(bool) {}

}  // namespace cockpit_voice
