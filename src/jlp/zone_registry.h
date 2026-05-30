#pragma once

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

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

// Per-path zone list, fetched once from the SK server's metadata at
// the path /signalk/v1/api/vessels/self/<dot.path>/meta. Cached for
// the device lifetime; reboot to re-fetch.
class ZoneRegistry {
 public:
  // Returns the zone matching `display_value`, or nullptr if no zones
  // for this path or value falls outside all zones.
  const Zone* match(const std::string& path, float display_value) const;

  // Schedule a fetch for `path` against the configured SK server. No-op
  // if already fetched or fetch in progress. Safe from any task.
  void fetch_async(const std::string& path);

  // Set the SK host/port to fetch from. Called once at boot.
  void set_sk_server(const std::string& host, uint16_t port);

 private:
  struct Entry {
    std::vector<Zone> zones;
    bool loaded = false;
  };

  static void fetch_task(void* arg);

  mutable std::unordered_map<std::string, Entry> map_;
  std::string sk_host_;
  uint16_t sk_port_ = 0;
};

ZoneRegistry& zones();

// RGB hex for each state, matching SignalK convention.
uint32_t color_for_state(ZoneState s);

}  // namespace jlp
