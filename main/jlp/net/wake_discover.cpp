// SPDX-License-Identifier: LicenseRef-Source-Available-No-Redistribution
#include "wake_discover.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <string>

#include "ArduinoJson.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "espos_sk.h"
#include "espos_voice/wyoming_satellite.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sk_server.h"

namespace jlp {
namespace {

constexpr const char* kTag = "wake_discover";
constexpr const char* kPath = "/plugins/signalk-openwakeword/api/status";

espos_voice::WyomingSatellite* s_sat = nullptr;

struct BodySink {
  std::string body;
};

esp_err_t on_http_event(esp_http_client_event_t* evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data) {
    auto* sink = static_cast<BodySink*>(evt->user_data);
    // Bounded: the status document is small, and a plugin that answers with
    // something huge should not be able to grow this without limit.
    if (sink->body.size() + evt->data_len <= 2048) {
      sink->body.append(static_cast<const char*>(evt->data), evt->data_len);
    }
  }
  return ESP_OK;
}

bool fetch_status(const std::string& url, const std::string& token,
                  std::string* out) {
  BodySink sink;
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 3000;
  cfg.event_handler = on_http_event;
  cfg.user_data = &sink;
  // perform(), not open()/fetch_headers(): the latter enables a body cache
  // whose assert has rebooted this device mid-fetch before (see zone_fetch).
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return false;
  if (!token.empty()) {
    esp_http_client_set_header(c, "Authorization", ("Bearer " + token).c_str());
  }
  esp_err_t err = esp_http_client_perform(c);
  int status = esp_http_client_get_status_code(c);
  esp_http_client_cleanup(c);
  if (err != ESP_OK || status != 200) {
    ESP_LOGI(kTag, "status query: err=%s http=%d", esp_err_to_name(err), status);
    return false;
  }
  *out = std::move(sink.body);
  return true;
}

// The satellite's wake path wants a numeric address (inet_pton), and the SK
// server may be configured by hostname.
bool resolve_ipv4(const std::string& host, std::string* out) {
  struct in_addr a;
  if (inet_pton(AF_INET, host.c_str(), &a) == 1) {
    *out = host;
    return true;
  }
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
  char buf[INET_ADDRSTRLEN] = {};
  auto* sa = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
  inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
  freeaddrinfo(res);
  if (!buf[0]) return false;
  *out = buf;
  return true;
}

void discover_task(void*) {
  const SkServer srv = sk_server();
  if (srv.host.empty() || !s_sat) {
    vTaskDelete(nullptr);
    return;
  }

  char tok[512] = "";
  (void)espos_sk_get_token(tok, sizeof(tok));

  const std::string url =
      "http://" + srv.host + ":" + std::to_string(srv.port) + kPath;
  std::string body;
  if (!fetch_status(url, tok, &body)) {
    ESP_LOGI(kTag, "no wake service on %s — staying on the on-device word",
             srv.host.c_str());
    vTaskDelete(nullptr);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    ESP_LOGW(kTag, "status document did not parse");
    vTaskDelete(nullptr);
    return;
  }

  const char* status = doc["status"] | "";
  if (std::string(status) != "ready") {
    ESP_LOGI(kTag, "wake service status '%s' — staying on the on-device word",
             status);
    vTaskDelete(nullptr);
    return;
  }

  // Take only the PORT from the advertised uri. Its host is whatever the
  // plugin was told to advertise, and with advertiseHost unset that is
  // 127.0.0.1 -- correct for the server, useless to a panel across the
  // network. The SK server we already talk to is the right host by
  // definition: the plugin runs on it.
  uint16_t port = 10400;
  const char* uri = doc["uri"] | "";
  if (const char* colon = strrchr(uri, ':')) {
    int p = atoi(colon + 1);
    if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
  }

  std::string ip;
  if (!resolve_ipv4(srv.host, &ip)) {
    ESP_LOGW(kTag, "could not resolve %s", srv.host.c_str());
    vTaskDelete(nullptr);
    return;
  }

  // Empty word list: which word to listen for is the service's business, set
  // once on the server rather than duplicated into every panel's config.
  if (s_sat->set_wake_network(ip, port, {})) {
    ESP_LOGI(kTag, "wake service READY — network wake to %s:%u", ip.c_str(), port);
  } else {
    ESP_LOGW(kTag, "switch to network wake failed — keeping the on-device word");
  }
  vTaskDelete(nullptr);
}

}  // namespace

void wake_discover_start(espos_voice::WyomingSatellite* sat) {
  static bool done = false;
  if (done || !sat) return;  // one shot: the answer does not change at runtime
  done = true;
  s_sat = sat;
  xTaskCreate(discover_task, "wake_discover", 5120, nullptr, 3, nullptr);
}

}  // namespace jlp
