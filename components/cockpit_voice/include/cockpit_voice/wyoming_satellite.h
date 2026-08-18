/* SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
 *
 * Wyoming voice satellite — the panel as a microphone/speaker for the
 * boat's voice pipeline (signalk-wyoming), plus wake-word detection.
 *
 * Phase 1 of the espOS port ships the INTERFACE with an inert
 * implementation: every widget, HTTP endpoint and control that talks to
 * the satellite compiles and behaves as "voice not available". Phase 2
 * ports the real satellite (TCP server :10700, mic streaming, WakeNet /
 * network wake) from sensesp-wyoming-satellite behind this same class.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cockpit_hal/audio_driver.h"

namespace cockpit_voice {

struct WyomingSatelliteConfig {
  uint16_t port = 10700;
  const char* name = "cockpit";
  uint32_t snd_rate = 22050;
  int mic_stream_gain = 3;
  bool on_device_wake = true;
  bool awake_cue = true;
  int wake_input_gain = 1;
  float wake_threshold = 0.0f;
  std::string wake_host;
  uint16_t wake_port = 10400;
  std::vector<std::string> wake_words;
  float wake_gain = 6.0f;
};

enum class SatState { Disconnected, Idle, Listening, Speaking };

class WyomingSatellite {
 public:
  WyomingSatellite(cockpit_hal::AudioDriver* audio, const WyomingSatelliteConfig& cfg)
      : audio_(audio), config_(cfg) {}

  void start();
  void stop();
  void set_ptt_held(bool held);
  void trigger_ptt() { set_ptt_held(true); }

  bool running() const { return running_.load(); }
  bool client_connected() const { return client_connected_.load(); }
  SatState state() const { return state_.load(); }

  bool wake_enabled() const { return false; }
  bool wake_on_device() const { return config_.on_device_wake; }
  bool wake_connected() const { return false; }
  bool wake_capturing() const { return false; }
  uint32_t wake_chunks() const { return 0; }
  uint32_t wake_detections() const { return 0; }
  const char* wake_word() const { return config_.wake_words.empty() ? "" : config_.wake_words[0].c_str(); }
  uint16_t wake_peak() { return 0; }
  size_t wake_pcm_snapshot(int16_t* /*out*/, size_t /*max_samples*/) { return 0; }
  uint32_t wake_pcm_age_ms() const { return 0; }
  void wake_pcm_clear() {}
  cockpit_hal::AudioDriver* audio() const { return audio_; }
  bool probe_mic_levels(cockpit_hal::AudioDriver::MicLevels& out) { return audio_ && audio_->probe_mic_channels(out); }

  using TranscriptFn = void (*)(void* ctx, const char* text);
  void set_transcript_cb(TranscriptFn cb, void* ctx) { transcript_cb_ = cb; transcript_ctx_ = ctx; }
  void set_mic_muted_fn(std::function<bool()> fn) { mic_muted_fn_ = std::move(fn); }

 private:
  cockpit_hal::AudioDriver* audio_;
  WyomingSatelliteConfig config_;
  std::atomic<bool> running_{false};
  std::atomic<bool> client_connected_{false};
  std::atomic<SatState> state_{SatState::Disconnected};
  TranscriptFn transcript_cb_ = nullptr;
  void* transcript_ctx_ = nullptr;
  std::function<bool()> mic_muted_fn_;
};

}  // namespace cockpit_voice
