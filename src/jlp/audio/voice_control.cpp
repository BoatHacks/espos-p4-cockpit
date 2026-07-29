#include "voice_control.h"

#include "sensesp_wyoming_satellite/wyoming_satellite.h"

namespace jlp {

bool VoiceControl::available() const {
  return sat_ && sat_->running() && sat_->client_connected();
}

void VoiceControl::trigger_ptt() {
  if (sat_) sat_->trigger_ptt();
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

VoiceControl& voice() {
  static VoiceControl v;
  return v;
}

}  // namespace jlp
