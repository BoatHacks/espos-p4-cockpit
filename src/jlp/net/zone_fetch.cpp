#include "zone_fetch.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include "sensesp.h"
#include "sensesp_app.h"
#include "sensesp/signalk/signalk_ws_client.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../subject_registry.h"
#include "../zone_registry.h"

static const char* TAG = "jlp.zonefetch";

namespace jlp {

namespace {

constexpr size_t kMaxMetaBytes = 8 * 1024;

// Replace "." with "/" so SK's REST endpoint accepts the path. SK
// rejects dotted paths in URL segments — `tanks.fuel.1.currentLevel`
// → `tanks/fuel/1/currentLevel`.
std::string slashify(const std::string& path) {
  std::string out;
  out.reserve(path.size());
  for (char c : path) out.push_back(c == '.' ? '/' : c);
  return out;
}

struct FetchArgs {
  std::string host;
  uint16_t port;
  std::vector<std::string> paths;
  // SK access token (bare JWT, no "Bearer " prefix), captured on the
  // caller's thread. Empty when the server is open or no token yet.
  std::string token;
};

// A parsed meta blob waiting to be applied on event_loop. The
// JsonDocument keeps the deserialized tree alive across the task →
// event_loop hand-off; apply_meta reads it on the event_loop side.
struct MetaResult {
  std::string path;
  JsonDocument doc;
};

// A parsed value seed waiting to be applied on event_loop. The value
// is extracted into a plain variant (number-or-bool) on the fetch
// side so the event_loop side doesn't have to keep the source
// document around. String paths are seeded from description() at
// build time, so they're never carried here.
struct ValueResult {
  std::string path;
  bool is_float = false;
  float f = 0.0f;
  int i = 0;
};

// Read an HTTP GET body into `body`. Returns false on any
// open / status / empty-read failure. Reuses the passed-in client so
// the caller can keep one connection across the whole batch. `token`,
// when non-empty, is sent as a Bearer credential — required once the SK
// server has security enabled, or every /meta and /value request 401s
// and quiet paths (e.g. a steady tank level) never get their zones.
bool http_get_body(esp_http_client_handle_t client, const std::string& url,
                   const std::string& token, std::string* body) {
  esp_http_client_set_url(client, url.c_str());
  if (!token.empty()) {
    const std::string bearer = "Bearer " + token;
    esp_http_client_set_header(client, "Authorization", bearer.c_str());
  }
  if (esp_http_client_open(client, 0) != ESP_OK) return false;
  int content_length = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  if (status != 200) {
    // 401 is the signature of a secured SK server with a missing/stale
    // token — surface it, since it silently strips zones from quiet
    // paths and is otherwise invisible.
    if (status == 401) {
      ESP_LOGW(TAG, "401 on %s — SK token missing or rejected", url.c_str());
    }
    esp_http_client_close(client);
    return false;
  }
  size_t cap = (content_length > 0 && (size_t)content_length < kMaxMetaBytes)
                   ? (size_t)content_length
                   : kMaxMetaBytes;
  body->resize(cap);
  int total = 0;
  while (total < (int)cap) {
    int n = esp_http_client_read(client, &(*body)[total], cap - total);
    if (n <= 0) break;
    total += n;
  }
  body->resize(total);
  esp_http_client_close(client);
  return total > 0;
}

// Fetch + parse one path's /meta. Appends a MetaResult to `out` if the
// blob carries zones or a description. Does NOT touch event_loop — the
// whole batch is drained in a single onDelay(0) by the caller.
void fetch_meta(esp_http_client_handle_t client, const std::string& url,
                const std::string& token, const std::string& path,
                std::vector<MetaResult>* out) {
  std::string body;
  if (!http_get_body(client, url, token, &body)) return;

  MetaResult r;
  DeserializationError de = deserializeJson(r.doc, body);
  if (de) {
    ESP_LOGW(TAG, "%s: parse failed (%s)", path.c_str(), de.c_str());
    return;
  }
  // Only keep it if there's actually a zones array or description —
  // saves map churn on paths with no useful metadata.
  JsonObjectConst obj = r.doc.as<JsonObjectConst>();
  if (obj["zones"].isNull() && obj["description"].isNull()) return;
  r.path = path;
  out->push_back(std::move(r));
}

// Fetch the path's current value via SK REST and stage it for seeding.
// Without this, paths whose value rarely changes (solar current at
// idle, steady SOC, idle switch state) sit at the subject's initial 0
// forever because SK only sends deltas on change — the firmware never
// sees a value after subscribing, even though SK's REST `/value`
// endpoint has the current reading ready to go. Like fetch_meta, this
// stays off event_loop; the value is extracted here and the result
// applied in the batch drain.
void fetch_value(esp_http_client_handle_t client, const std::string& url,
                 const std::string& token, const std::string& path,
                 std::vector<ValueResult>* out) {
  std::string body;
  if (!http_get_body(client, url, token, &body)) return;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  JsonVariantConst v = doc.as<JsonObjectConst>()["value"];
  if (v.isNull()) return;

  ValueResult r;
  r.path = path;
  if (v.is<float>() || v.is<double>()) {
    r.is_float = true;
    r.f = v.as<float>();
  } else if (v.is<bool>()) {
    r.i = v.as<bool>() ? 1 : 0;
  } else if (v.is<int>()) {
    r.i = v.as<int>();
  } else {
    // String / object values aren't seeded — string subjects read
    // description() at build time and the str_buf is owned by the WS
    // listener (writing it here would race).
    return;
  }
  out->push_back(std::move(r));
}

void fetch_task(void* arg) {
  auto* a = static_cast<FetchArgs*>(arg);

  // One http client reused across paths — esp_http_client supports
  // setting a new URL on the existing handle, which avoids the per-
  // request TCP handshake.
  esp_http_client_config_t cfg = {};
  cfg.url = "http://placeholder";  // will be overridden per-path
  cfg.timeout_ms = 3000;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGW(TAG, "http client init failed");
    delete a;
    vTaskDelete(NULL);
    return;
  }

  // Accumulate every parsed result on the fetch task, then apply the
  // whole batch in ONE event_loop callback. The old form marshaled
  // one event_loop()->onDelay(0) per fetch — two per path — so a
  // 31-widget layout flooded event_loop with 62 callbacks, each
  // firing lv_subject_notify. Under the concurrent layout apply +
  // littlefs reformat, that storm pushed event_loop past the 15 s
  // liveness-watchdog threshold and rebooted the panel.
  auto metas = std::make_shared<std::vector<MetaResult>>();
  auto values = std::make_shared<std::vector<ValueResult>>();

  const std::string base =
      "http://" + a->host + ":" + std::to_string(a->port) +
      "/signalk/v1/api/vessels/self/";
  for (const auto& path : a->paths) {
    const std::string slashed = slashify(path);
    fetch_meta(client, base + slashed + "/meta", a->token, path, metas.get());
    // SK's /value endpoint returns the full node ({value, meta,
    // $source, ...}); we read the `value` member out of that wrapper.
    fetch_value(client, base + slashed, a->token, path, values.get());
  }
  esp_http_client_cleanup(client);
  ESP_LOGI(TAG, "fetched meta + value for %u paths (%u meta, %u val)",
           (unsigned)a->paths.size(), (unsigned)metas->size(),
           (unsigned)values->size());

  // Single hop onto event_loop to apply the whole batch.
  sensesp::event_loop()->onDelay(0, [metas, values]() {
    for (auto& m : *metas) {
      zones().apply_meta(m.path, m.doc.as<JsonObjectConst>());
    }
    for (auto& vr : *values) {
      auto kind = registry().kind_of(vr.path);
      if (!kind) continue;  // path not bound by any widget
      lv_subject_t* sub = registry().lookup(vr.path);
      if (!sub) continue;
      switch (*kind) {
        case SubjectKind::Float:
          if (vr.is_float) lv_subject_set_float(sub, vr.f);
          else lv_subject_set_float(sub, (float)vr.i);
          break;
        case SubjectKind::Int:
        case SubjectKind::Bool:
          // Coerce a float reading (SK may report an int path's value
          // as 5.0) so the seed isn't silently dropped — mirrors the
          // int→float coercion the Float case does.
          if (vr.is_float) lv_subject_set_int(sub, (int)vr.f);
          else lv_subject_set_int(sub, vr.i);
          break;
        case SubjectKind::String:
          break;  // seeded from description() at build time
      }
    }
  });

  delete a;
  vTaskDelete(NULL);
}

}  // namespace

void zone_fetch_for_paths(const std::string& sk_host, uint16_t sk_port,
                          const std::vector<std::string>& paths) {
  if (paths.empty()) return;
  // Grab the SK access token here, on the caller's thread, so the fetch
  // task never touches the WS client cross-thread. Empty string when
  // the server is open or no token has been obtained yet, which the
  // fetch treats as an unauthenticated request.
  std::string token;
  if (auto app = sensesp::SensESPApp::get()) {
    if (auto ws = app->get_ws_client()) {
      token = ws->get_auth_token().c_str();
    }
  }
  auto* a = new FetchArgs{sk_host, sk_port, paths, std::move(token)};
  if (xTaskCreate(fetch_task, "jlp_zonefetch", 8192, a, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to spawn zone fetch task");
    delete a;
  }
}

}  // namespace jlp
