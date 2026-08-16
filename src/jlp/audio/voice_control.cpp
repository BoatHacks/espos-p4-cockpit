#include "voice_control.h"

#include "sensesp_cockpit_display/hal/audio_driver.h"
#include "sensesp_wyoming_satellite/wyoming_satellite.h"

#include "jlp/layout/store.h"

namespace jlp {

// NVS keys for the persisted panel-local audio state. Short: NVS keys cap
// at 15 chars.
static constexpr const char* kSpeakerMutedKey = "spk_muted";
static constexpr const char* kMicMutedKey = "mic_muted";
static constexpr const char* kVolumeKey = "volume";

void VoiceControl::init(sensesp_wyoming::WyomingSatellite* sat,
                        sensesp_cockpit_display::AudioDriver* audio) {
  sat_ = sat;
  audio_ = audio;
  // Restore the persisted mute state. Defaults to unmuted on a fresh
  // device, so a first boot behaves as before; thereafter the helm keeps
  // whatever the last person set instead of re-arming audio by surprise.
  speaker_muted_ = store_flag_get(kSpeakerMutedKey, false);
  mic_muted_.store(store_flag_get(kMicMutedKey, false));
  volume_ = store_u8_get(kVolumeKey, volume_);
  if (volume_ > 100) volume_ = 100;  // guard a corrupted//out-of-range read
  if (audio_) {
    audio_->set_enabled(!speaker_muted_);
    // Push the restored level at the codec, otherwise the slider shows the
    // stored value while the amp still runs at the driver default.
    audio_->set_volume(volume_);
  }
}

bool VoiceControl::available() const {
  return sat_ && sat_->running() && sat_->client_connected();
}

void VoiceControl::set_ptt_held(bool held) {
  // A muted mic ignores a press so the privacy switch is honoured.
  if (held && mic_muted_) return;
  if (sat_) sat_->set_ptt_held(held);
}

int VoiceControl::state_code() const {
  if (!sat_) return 0;
  switch (sat_->state()) {
    case sensesp_wyoming::SatState::Idle:
      return 1;
    case sensesp_wyoming::SatState::Listening:
      return 2;
    case sensesp_wyoming::SatState::Speaking:
      return 3;
    case sensesp_wyoming::SatState::Disconnected:
    default:
      return 0;
  }
}

void VoiceControl::set_speaker_muted(bool muted) {
  speaker_muted_ = muted;
  // set_enabled(false) holds the amp disabled so a quiet helm stays quiet;
  // set_enabled(true) re-arms it. Also mute the chime so a mute is total.
  if (audio_) audio_->set_enabled(!muted);
  // Persist: a panel that unmutes itself on every power cycle overrides
  // whoever silenced it, and a helm that beeps after a reboot is exactly
  // the surprise a mute switch exists to prevent.
  store_flag_set(kSpeakerMutedKey, muted);
}

void VoiceControl::set_volume(uint8_t pct, bool persist) {
  if (pct > 100) pct = 100;
  volume_ = pct;
  if (audio_) audio_->set_volume(pct);
  // Persisted like the mute flags: a helm turned down before a reboot should
  // not come back at the driver default. Only on release, though — see the
  // header: a drag emits a value change per pixel.
  if (persist) store_u8_set(kVolumeKey, pct);
}

void VoiceControl::set_mic_muted(bool muted) {
  // Publish the muted state FIRST, then purge, so any concurrent /mic_probe
  // sees "muted" and the satellite's own probe kill switch (set by
  // wake_pcm_clear) blocks the snapshot regardless of timing — the two gates
  // together close the check-then-read window.
  mic_muted_.store(muted);
  if (muted && sat_) {
    sat_->set_ptt_held(false);
    sat_->wake_pcm_clear();  // sets the lock-free probe-disable kill switch
  }
  // Persist for the same reason as the speaker — a privacy switch that
  // silently re-opens the mic at boot is worse than one that never existed.
  store_flag_set(kMicMutedKey, muted);
}

VoiceControl& voice() {
  static VoiceControl v;
  return v;
}

}  // namespace jlp
