#include "sk_server.h"

#include "espos_sk.h"

namespace jlp {

namespace {
std::string g_default_host;
uint16_t g_default_port = 0;
}  // namespace

void sk_server_set_default(const char* host, uint16_t port) {
  g_default_host = host ? host : "";
  g_default_port = port;
}

// The server espOS selected (manual sk.server_host, pinned sk.server_self,
// or the discovered master) — the same one the token and the stream use,
// so REST fetches and the connection-lost banner follow one setting.
SkServer sk_server() {
  espos_sk_server_t srv;
  if (espos_sk_get_server(&srv) == ESP_OK && srv.host[0] && srv.port > 0) {
    return {std::string(srv.host), srv.port};
  }
  return {g_default_host, g_default_port};
}

}  // namespace jlp
