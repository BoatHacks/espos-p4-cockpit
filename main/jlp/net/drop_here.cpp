#include "drop_here.h"

#include <string>

#include <ArduinoJson.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "espos_sk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sk_put.h"
#include "sk_server.h"

static const char* TAG = "jlp.drophere";

namespace jlp {

namespace {

struct DropArgs {
  std::string host;
  uint16_t port;
  std::string token;
  bool ssl;
};

// Minimal auth'd GET into a std::string (mirrors zone_fetch's helper,
// kept local so drop_here stays self-contained).
esp_err_t http_event(esp_http_client_event_t* e) {
  if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data) {
    static_cast<std::string*>(e->user_data)->append((const char*)e->data,
                                                     e->data_len);
  }
  return ESP_OK;
}

void drop_task(void* arg) {
  auto* a = static_cast<DropArgs*>(arg);
  std::string body;

  esp_http_client_config_t cfg = {};
  const std::string url =
      (a->ssl ? "https://" : "http://") + a->host + ":" +
      std::to_string(a->port) +
      "/signalk/v1/api/vessels/self/navigation/position";
  cfg.url = url.c_str();
  cfg.timeout_ms = 3000;
  cfg.event_handler = http_event;
  cfg.user_data = &body;
  if (a->ssl) {
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.skip_cert_common_name_check = true;
  }
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGW(TAG, "http client init failed");
    delete a;
    vTaskDelete(NULL);
    return;
  }
  if (!a->token.empty()) {
    const std::string bearer = "Bearer " + a->token;
    esp_http_client_set_header(client, "Authorization", bearer.c_str());
  }

  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200) {
    ESP_LOGW(TAG, "position GET failed (err=%s status=%d) — not dropping",
             esp_err_to_name(err), status);
    delete a;
    vTaskDelete(NULL);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    ESP_LOGW(TAG, "position parse failed — not dropping");
    delete a;
    vTaskDelete(NULL);
    return;
  }
  JsonVariantConst v = doc.as<JsonObjectConst>()["value"];
  const double kNoFix = 1e9;
  double lat = v["latitude"] | kNoFix;
  double lon = v["longitude"] | kNoFix;
  // Reject a missing/out-of-range fix before PUTting a bogus drop.
  if (!(lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0)) {
    ESP_LOGW(TAG, "no valid GPS fix (lat=%.3f lon=%.3f) — not dropping", lat,
             lon);
    delete a;
    vTaskDelete(NULL);
    return;
  }

  // PUT the fix to navigation.anchor.position → plugin drops there.
  put_position("navigation.anchor.position", lat, lon);
  ESP_LOGI(TAG, "drop-here: PUT %.6f, %.6f", lat, lon);

  delete a;
  vTaskDelete(NULL);
}

}  // namespace

void drop_anchor_here() {
  // Read host/port + token/ssl on the caller (event_loop) thread so the
  // task never touches the WS client cross-thread. Same as zone_fetch.
  SkServer s = sk_server();
  std::string token;
  bool ssl = false;
  {
    // espOS holds the token; the REST scheme mirrors the stream's (plain
    // http on the espOS stream today).
    char tok[512] = "";
    if (espos_sk_get_token(tok, sizeof(tok)) == ESP_OK) token = tok;
  }
  auto* a = new DropArgs{s.host, s.port, std::move(token), ssl};
  if (xTaskCreate(drop_task, "jlp_drophere", 8192, a, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to spawn drop-here task");
    delete a;
  }
}

}  // namespace jlp
