#pragma once

namespace jlp {

// Compiled-in fallback layout. Loaded when LittleFS has no saved
// layout (first boot, factory reset). Subsequent boots prefer the
// LittleFS copy (added in step 6).
inline constexpr const char* kDefaultLayoutJson = R"json(
{
  "schema": 1,
  "name": "default",
  "screens": [
    {
      "id": "main",
      "title": "Main",
      "widgets": [
        {
          "type": "label", "id": "depth",
          "label": "depth",
          "bind": "environment.depth.belowTransducer",
          "display": { "unit": "m", "scale": 1.0, "offset": 0.0, "decimals": 1 },
          "x": 20, "y": 20, "w": 280, "h": 80
        },
        {
          "type": "toggle", "id": "bank0_0",
          "label": "bank 0.0",
          "bind": "electrical.switches.bank.0.0.state",
          "x": 20, "y": 120, "w": 200, "h": 100
        }
      ]
    }
  ]
}
)json";

}  // namespace jlp
