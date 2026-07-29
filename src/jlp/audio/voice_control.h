#pragma once

// Thin process-lifetime handle to the Wyoming voice satellite so JLP
// widgets (the mic button) can trigger push-to-talk and read voice state
// without each widget depending on the satellite type. Mirrors chime().

#include <cstdint>

namespace sensesp_wyoming {
class WyomingSatellite;
}

namespace jlp {

class VoiceControl {
 public:
  void init(sensesp_wyoming::WyomingSatellite* sat) { sat_ = sat; }

  // True if a satellite is wired and an orchestrator is connected.
  bool available() const;

  // Begin a push-to-talk utterance. No-op if unavailable / not armed.
  void trigger_ptt();

  // 0 disconnected, 1 idle, 2 listening, 3 speaking — for a UI indicator.
  int state_code() const;

 private:
  sensesp_wyoming::WyomingSatellite* sat_ = nullptr;
};

VoiceControl& voice();

}  // namespace jlp
