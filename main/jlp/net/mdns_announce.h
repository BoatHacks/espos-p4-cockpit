#pragma once

#include <stdint.h>

namespace jlp {

// Adds _signalk-player._tcp service on the layout API port. Deferred
// via event_loop()->onDelay so SensESP's MDNS.begin runs first.
void mdns_announce_start(uint16_t api_port);

}  // namespace jlp
