#pragma once

#include <string>
#include <vector>

namespace jlp {

// Fetch SK meta for each path via HTTP GET /signalk/v1/api/<path>/meta
// (the same endpoint the designer uses) and feed it to zones() so the
// firmware learns zones + descriptions for paths that SK never sends
// over WS meta deltas. Async — runs on a fetch task so the event_loop
// doesn't block; results marshal back via onDelay(0).
//
// Why HTTP and not WS: SensESP opens its WS with `subscribe=none`,
// which suppresses SK's automatic meta-on-subscribe broadcast for
// most paths. Sending a wildcard subscribe to force meta delivery
// overflows the WS receive queue ("Dropping oldest received update
// (queue full)"). The REST endpoint is per-path, low-traffic, and
// only needs to run once per path per session.
void zone_fetch_for_paths(const std::string& sk_host, uint16_t sk_port,
                          const std::vector<std::string>& paths);

}  // namespace jlp
