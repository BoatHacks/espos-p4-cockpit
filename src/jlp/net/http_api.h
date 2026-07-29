#pragma once

#include <stdint.h>

namespace sensesp_wyoming {
class WyomingSatellite;
}

namespace jlp {

// Starts the layout-player HTTP API on `port`. Currently exposes
// POST /layout. Future endpoints (GET /hello, /healthz) land here.
void http_api_start(uint16_t port);

// Register the Wyoming voice satellite so /hello can report its status.
// Null (the default) reports "unavailable".
void http_api_set_wyoming(sensesp_wyoming::WyomingSatellite* sat);

}  // namespace jlp
