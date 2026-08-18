/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
 *
 * AudioDriver that does nothing: keeps the chime/voice controls compiling
 * and honest ("no audio") until the ES7210/ES8311 HAL is ported (phase 2).
 */
#pragma once

#include "cockpit_hal/audio_driver.h"

namespace cockpit_hal {

class NullAudio : public AudioDriver {
 public:
  void init() override {}
  bool ready() const override { return false; }
  uint32_t sample_rate() const override { return 22050; }
  void play_pcm(const int16_t*, size_t) override {}
};

}  // namespace cockpit_hal
