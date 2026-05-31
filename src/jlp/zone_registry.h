#pragma once

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <ArduinoJson.h>

namespace jlp {

enum class ZoneState : uint8_t {
  Nominal,   // green
  Normal,    // green (alias)
  Alert,     // blue
  Warn,      // yellow
  Alarm,     // orange
  Emergency  // red
};

struct Zone {
  float lower;     // display-space (apply widget scale+offset first)
  float upper;
  ZoneState state;
};

// Per-path zone list, populated from meta deltas pushed in-stream by
// signalk-server when the WS subscription includes sendMeta=all
// (default in SensESP since PR #965). No HTTP polling — zones arrive
// alongside value deltas and update automatically if SK's metadata
// changes.
class ZoneRegistry {
 public:
  // Wire into the SK WS client's meta callback at boot. Idempotent.
  void hook_sk_ws();

  // Returns the zone matching `display_value` for `path`, or nullptr
  // if no zones for this path or value falls outside all zones.
  const Zone* match(const std::string& path, float display_value) const;

  // Manually feed a meta object (e.g. from a test). Normally not
  // called by user code — the WS callback does this.
  void apply_meta(const std::string& path, const JsonObjectConst& meta);

 private:
  std::unordered_map<std::string, std::vector<Zone>> map_;
};

ZoneRegistry& zones();

// RGB hex for each state, matching SignalK convention.
uint32_t color_for_state(ZoneState s);

}  // namespace jlp
