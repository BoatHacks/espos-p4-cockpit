#pragma once

#include <stdint.h>

namespace jlp {

// Starts the layout-player HTTP API on `port`. Currently exposes
// POST /layout. Future endpoints (GET /hello, /healthz) land here.
void http_api_start(uint16_t port);

}  // namespace jlp
