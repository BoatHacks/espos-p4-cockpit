#pragma once

#include <cstdint>
#include <string>

namespace jlp {

// The SK server the JLP HTTP calls (layout fetch, zone/value REST fetch)
// and the connection-lost banner should target. Resolved from SensESP's
// SKWSClient — i.e. whatever the SensESP config web UI is set to (or an
// mDNS-discovered server) — so the whole panel follows one setting
// instead of a compile-time constant.
//
// Falls back to the compiled default until SensESP has resolved an
// address (get_server_address() is empty until the first WS connect).
struct SkServer {
  std::string host;
  uint16_t port;
};

// Compile-time default used before the SensESP config resolves.
void sk_server_set_default(const char* host, uint16_t port);

// Best-effort current server: SensESP's resolved address if available,
// else the compiled default.
SkServer sk_server();

}  // namespace jlp
