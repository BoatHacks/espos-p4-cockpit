#include "sk_put.h"

#include <Arduino.h>
#include <unordered_map>

#include "esp_log.h"
#include "sensesp.h"
#include "sensesp/signalk/signalk_put_request.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "sensesp/system/uuid.h"
#include "sensesp_app.h"

static const char* TAG = "jlp.put";

namespace jlp {

namespace {

// One SKPutRequest per (path, kind). Different value types are
// tracked separately since the JSON wire format differs.
std::unordered_map<std::string, sensesp::SKPutRequest<bool>*> g_bool;
std::unordered_map<std::string, sensesp::SKPutRequest<int>*> g_int;
std::unordered_map<std::string, sensesp::SKPutRequest<float>*> g_float;
std::unordered_map<std::string, sensesp::SKPutRequest<String>*> g_string;

template <class T>
sensesp::SKPutRequest<T>* get(
    std::unordered_map<std::string, sensesp::SKPutRequest<T>*>& cache,
    const std::string& path) {
  auto it = cache.find(path);
  if (it != cache.end()) return it->second;
  // ignore_duplicates=false: we always want a tap to fire, even if the
  // server's current value matches what we're sending (rare but real
  // for momentary buttons).
  auto* p = new sensesp::SKPutRequest<T>(String(path.c_str()), "", false);
  cache.emplace(path, p);
  return p;
}

}  // namespace

void put_bool(const std::string& path, bool value) {
  ESP_LOGI(TAG, "PUT %s = %s", path.c_str(), value ? "true" : "false");
  get(g_bool, path)->set(value);
}

void put_int(const std::string& path, int value) {
  ESP_LOGI(TAG, "PUT %s = %d", path.c_str(), value);
  get(g_int, path)->set(value);
}

void put_float(const std::string& path, float value) {
  ESP_LOGI(TAG, "PUT %s = %f", path.c_str(), value);
  get(g_float, path)->set(value);
}

void put_string(const std::string& path, const std::string& value) {
  ESP_LOGI(TAG, "PUT %s = \"%s\"", path.c_str(), value.c_str());
  get(g_string, path)->set(String(value.c_str()));
}

void put_null(const std::string& path) {
  ESP_LOGI(TAG, "PUT %s = null", path.c_str());
  auto app = sensesp::SensESPApp::get();
  if (!app) return;
  auto ws = app->get_ws_client();
  if (!ws) return;

  // Build the SK PUT envelope by hand (SKPutRequest<T> can't carry a
  // null value): {"requestId":<uuid>,"put":{"path":<path>,"value":null}}.
  // Fire-and-forget — we don't track the response; the server's action
  // handler acts on the PUT regardless.
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["requestId"] = sensesp::generate_uuid4();
  JsonObject put = root["put"].to<JsonObject>();
  put["path"] = path;
  put["value"] = nullptr;  // ArduinoJson v7 serialises this as JSON null

  String s;
  serializeJson(doc, s);
  ws->sendTXT(s);
}

void put_position(const std::string& path, double latitude,
                  double longitude) {
  ESP_LOGI(TAG, "PUT %s = {lat=%.6f, lon=%.6f}", path.c_str(), latitude,
           longitude);
  auto app = sensesp::SensESPApp::get();
  if (!app) return;
  auto ws = app->get_ws_client();
  if (!ws) return;

  // {"requestId":<uuid>,"put":{"path":<path>,"value":{latitude,longitude}}}.
  // SKPutRequest<T> is scalar-typed, so build the envelope directly (as
  // put_null does). The anchor-alarm plugin's putPosition handler drops
  // the anchor at this position.
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["requestId"] = sensesp::generate_uuid4();
  JsonObject put = root["put"].to<JsonObject>();
  put["path"] = path;
  JsonObject val = put["value"].to<JsonObject>();
  val["latitude"] = latitude;
  val["longitude"] = longitude;

  String s;
  serializeJson(doc, s);
  ws->sendTXT(s);
}

// ---- notification ACK ----
//
// SignalK PUT requests against notifications.* paths are NOT honored
// by the server's PUT dispatcher — the modern notification API
// (server-mediated alarms) routes ACKs via dedicated REST endpoints
// keyed by the notification's UUID, not via PUTs to the path.
//
// However, the server's notification-handler interception path
// (`filterNotifications` in signalk-server) DOES consume incoming
// deltas on `notifications.*` paths and syncs the alarm's state to
// the delta's `state` field. So injecting an inbound delta with
// state="normal" achieves the same effect as the (now-non-existent)
// PUT convention. We send the delta directly over the SK WebSocket
// since it's already open.

void put_notification_ack(const std::string& path_after_prefix) {
  std::string full = "notifications." + path_after_prefix;
  ESP_LOGI(TAG, "ACK %s (delta state=normal)", full.c_str());

  auto app = sensesp::SensESPApp::get();
  if (!app) {
    ESP_LOGW(TAG, "no SensESPApp — cannot send ACK delta");
    return;
  }
  auto ws = app->get_ws_client();
  if (!ws) {
    ESP_LOGW(TAG, "no WS client — cannot send ACK delta");
    return;
  }

  // {updates: [{values: [{path: <full>, value: {state: "normal",
  //   message: "", method: []}}]}]}
  // No context — the server fills "vessels.self" for self-deltas.
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  JsonArray updates = root["updates"].to<JsonArray>();
  JsonObject update = updates.add<JsonObject>();
  JsonArray values = update["values"].to<JsonArray>();
  JsonObject v = values.add<JsonObject>();
  v["path"] = full;
  JsonObject val = v["value"].to<JsonObject>();
  val["state"] = "normal";
  val["message"] = "";
  val["method"].to<JsonArray>();

  String s;
  serializeJson(doc, s);
  ws->sendTXT(s);
}

void subscribe_new_paths(const std::set<std::string>& paths) {
  if (paths.empty()) return;

  // The post-swap hook that calls us runs synchronously inside
  // LayoutManager::apply(), which at boot executes on the setup task —
  // off event_loop. sendTXT must be serialized on event_loop with the
  // rest of the WS traffic, so marshal the whole send there. Capture
  // paths by value; the source set doesn't outlive this call.
  sensesp::event_loop()->onDelay(0, [paths]() {
    auto app = sensesp::SensESPApp::get();
    if (!app) return;
    auto ws = app->get_ws_client();
    if (!ws) return;

    // Match the shape SensESP's own subscribe_listeners() sends: a
    // vessels.self context and a subscribe array of {path, period}. With
    // sendMeta=all (a connection-level flag) one subscription delivers
    // both value and meta deltas; reuse the 1000 ms period the
    // SubjectRegistry creates its listeners with.
    constexpr int kListenDelayMs = 1000;
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["context"] = "vessels.self";
    JsonArray subscribe = root["subscribe"].to<JsonArray>();
    for (const auto& p : paths) {
      JsonObject entry = subscribe.add<JsonObject>();
      entry["path"] = p;
      entry["period"] = kListenDelayMs;
    }

    String s;
    serializeJson(doc, s);
    ESP_LOGI(TAG, "incremental subscribe for %u new path(s)",
             (unsigned)paths.size());
    ws->sendTXT(s);
  });
}

}  // namespace jlp
