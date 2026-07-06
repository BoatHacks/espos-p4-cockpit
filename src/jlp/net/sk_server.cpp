#include "sk_server.h"

#include <ArduinoJson.h>

#include "sensesp_app.h"

namespace jlp {

namespace {
std::string g_default_host;
uint16_t g_default_port = 0;
}  // namespace

void sk_server_set_default(const char* host, uint16_t port) {
  g_default_host = host ? host : "";
  g_default_port = port;
}

SkServer sk_server() {
  auto app = sensesp::SensESPApp::get();
  if (app) {
    auto ws = app->get_ws_client();
    if (ws) {
      // Prefer the CONFIGURED server (SensESP config web UI / persisted
      // value), which SKWSClient::to_json() reports from
      // conf_server_address_ — available at boot after load(), unlike
      // get_server_address() which is empty until the WS connects. This
      // way the banner shows the server the panel is *trying* to reach
      // even while it can't connect, and the layout/zone fetches target
      // the configured server too.
      JsonDocument doc;
      JsonObject obj = doc.to<JsonObject>();
      if (ws->to_json(obj)) {
        const char* addr = obj["sk_address"] | (const char*)nullptr;
        uint16_t port = obj["sk_port"] | 0;
        if (addr && *addr && port > 0) return {std::string(addr), port};
      }
      // mDNS mode (no configured address): fall back to the resolved
      // address once the WS has discovered + connected.
      std::string host(ws->get_server_address().c_str());
      uint16_t rport = ws->get_server_port();
      if (!host.empty() && rport > 0) return {host, rport};
    }
  }
  return {g_default_host, g_default_port};
}

}  // namespace jlp
