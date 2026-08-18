#include "zone_fetch.h"

#include <ArduinoJson.h>
#include "cockpit_hal/ui.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "espos_sk.h"
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
  // Captured on the caller's thread because fetch_task must not touch
  // the SKWSClient. `ssl` mirrors the WS transport so the REST scheme
  // matches it (see zone_fetch_for_paths / http_get_body).
  std::string token;
  bool ssl = false;
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
// Accumulate the response body into the std::string handed in via
// user_data. Used with esp_http_client_perform (below) instead of the
// open()/fetch_headers()/read() streaming pattern: that pattern leaves
// esp_http_client's cache_data_in_fetch_hdr flag set (only perform()
// clears it), so when a response body arrives while the headers are
// still parsing — routine for SK's small REST replies delivered in one
// segment — it hits an assert in http_on_body (orig_raw_data ==
// raw_data) and reboots the device. perform() disables that cache and
// routes the body through this handler, sidestepping the assert.
// Body sink for the perform() handler: accumulates response data and
// flags if it would exceed kMaxMetaBytes, so http_get_body can reject an
// oversized reply rather than parse a truncated prefix.
struct BodySink {
  std::string data;
  bool overflow = false;
};

esp_err_t zone_http_event(esp_http_client_event_t* e) {
  if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data) {
    auto* sink = static_cast<BodySink*>(e->user_data);
    if (sink->data.size() + e->data_len > kMaxMetaBytes) {
      sink->overflow = true;
      return ESP_OK;  // keep draining, but the result is now rejected
    }
    sink->data.append(static_cast<const char*>(e->data), e->data_len);
  }
  return ESP_OK;
}

bool http_get_body(const std::string& url, bool ssl, const std::string& token,
                   std::string* body) {
  body->clear();
  BodySink sink;

  // A FRESH client per request. The prior form reused one handle across
  // every path via set_url(); with esp_http_client_perform() that reuse
  // desyncs the client's internal orig_raw_data/raw_data buffer pointers
  // and trips assert(orig_raw_data == raw_data) in http_on_body —
  // rebooting the device mid-fetch (seen on navigation.anchor.* paths).
  // A new handle per call starts that state clean, and perform() (unlike
  // open()/fetch_headers()) disables the fetch-header body cache so the
  // assert path is never entered at all.
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = 3000;
  cfg.event_handler = zone_http_event;
  cfg.user_data = &sink;
  if (ssl) {
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.skip_cert_common_name_check = true;
  }
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return false;
  if (!token.empty()) {
    const std::string bearer = "Bearer " + token;
    esp_http_client_set_header(client, "Authorization", bearer.c_str());
  }

  esp_err_t err = esp_http_client_perform(client);
  int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
  esp_http_client_cleanup(client);

  if (err != ESP_OK) return false;
  if (status != 200) {
    // 401 is the signature of a secured SK server with a missing/stale
    // token — surface it, since it silently strips zones from quiet
    // paths and is otherwise invisible.
    if (status == 401) {
      ESP_LOGW(TAG, "401 on %s — SK token missing or rejected", url.c_str());
    }
    return false;
  }
  if (sink.overflow) {
    // Reject rather than hand back a truncated prefix that would parse
    // into wrong/partial meta. kMaxMetaBytes (8 KiB) is well above any
    // real SK meta/value blob, so this only trips on a runaway response.
    ESP_LOGW(TAG, "%s: response exceeds %u bytes — skipping", url.c_str(),
             (unsigned)kMaxMetaBytes);
    return false;
  }
  *body = std::move(sink.data);
  return !body->empty();
}

// Fetch + parse one path's /meta. Appends a MetaResult to `out` if the
// blob carries zones or a description. Does NOT touch event_loop — the
// whole batch is drained in a single onDelay(0) by the caller.
void fetch_meta(bool ssl, const std::string& url, const std::string& token,
                const std::string& path, std::vector<MetaResult>* out) {
  std::string body;
  if (!http_get_body(url, ssl, token, &body)) return;

  MetaResult r;
  DeserializationError de = deserializeJson(r.doc, body);
  if (de) {
    // A 200 with a non-JSON body happens for paths the server exposes
    // without meta (seen on navigation.anchor.* when the anchor is up).
    // Harmless — just skip this path's meta. Debug, not warn: it's
    // expected, not a fault.
    ESP_LOGD(TAG, "%s: meta not JSON (%s) — skipping", path.c_str(),
             de.c_str());
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
void fetch_value(bool ssl, const std::string& url, const std::string& token,
                 const std::string& path, std::vector<ValueResult>* out) {
  std::string body;
  if (!http_get_body(url, ssl, token, &body)) return;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  JsonVariantConst v = doc.as<JsonObjectConst>()["value"];
  if (v.isNull()) return;

  ValueResult r;
  r.path = path;
  // Check bool and int before float: ArduinoJson reports is<float>() true
  // for integers too, so testing float first would route an integer
  // reading through as<float>() and lose precision on large values.
  if (v.is<bool>()) {
    r.i = v.as<bool>() ? 1 : 0;
  } else if (v.is<int>()) {
    r.i = v.as<int>();
  } else if (v.is<float>() || v.is<double>()) {
    r.is_float = true;
    r.f = v.as<float>();
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

  // Accumulate every parsed result on the fetch task, then apply the
  // whole batch in ONE event_loop callback. The old form marshaled
  // one cockpit_hal::ui::after(0) per fetch — two per path — so a
  // 31-widget layout flooded event_loop with 62 callbacks, each
  // firing lv_subject_notify. Under the concurrent layout apply +
  // littlefs reformat, that storm pushed event_loop past the 15 s
  // liveness-watchdog threshold and rebooted the panel.
  auto metas = std::make_shared<std::vector<MetaResult>>();
  auto values = std::make_shared<std::vector<ValueResult>>();

  // Mirror the WS transport: HTTPS when SensESP negotiated TLS, plain
  // HTTP otherwise. The token rides whichever scheme the WS itself uses
  // to obtain it, so a plaintext deployment gains no extra exposure here
  // (the WS access-request already sent it in the clear); a TLS
  // deployment keeps it encrypted.
  const std::string base =
      (a->ssl ? "https://" : "http://") + a->host + ":" +
      std::to_string(a->port) + "/signalk/v1/api/vessels/self/";
  for (const auto& path : a->paths) {
    const std::string slashed = slashify(path);
    fetch_meta(a->ssl, base + slashed + "/meta", a->token, path, metas.get());
    // SK's /value endpoint returns the full node ({value, meta,
    // $source, ...}); we read the `value` member out of that wrapper.
    fetch_value(a->ssl, base + slashed, a->token, path, values.get());
  }
  ESP_LOGI(TAG, "fetched meta + value for %u paths (%u meta, %u val)",
           (unsigned)a->paths.size(), (unsigned)metas->size(),
           (unsigned)values->size());

  // Single hop onto event_loop to apply the whole batch.
  cockpit_hal::ui::post([metas, values]() {
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
  bool ssl = false;
  {
    // espOS holds the token (empty when the server is open or none yet);
    // the REST scheme mirrors the stream's (plain http on espOS today).
    char tok[512] = "";
    if (espos_sk_get_token(tok, sizeof(tok)) == ESP_OK) token = tok;
  }
  auto* a = new FetchArgs{sk_host, sk_port, paths, std::move(token), ssl};
  if (xTaskCreate(fetch_task, "jlp_zonefetch", 8192, a, 4, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to spawn zone fetch task");
    delete a;
  }
}

}  // namespace jlp
