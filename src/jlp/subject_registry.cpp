#include "subject_registry.h"

#include <Arduino.h>
#include "esp_log.h"
#include "sensesp/signalk/signalk_metadata_listener.h"
#include "sensesp/signalk/signalk_value_listener.h"
#include "sensesp/system/lambda_consumer.h"

#include "zone_registry.h"

static const char* TAG = "jlp.reg";

namespace jlp {

namespace {

constexpr int kListenDelayMs = 1000;

// Build a SensESP listener for `path` and pipe each emitted value into
// `subject` via `lv_subject_set_*`. Returns the listener pointer so the
// caller can store a teardown closure (though see note on SKListener
// lifetime — it has no public destructor, so teardown is a no-op in v1).
//
// Ownership: the LambdaConsumer is handed to `connect_to` as a
// shared_ptr, which captures it inside the observer lambda the
// producer owns. The consumer therefore lives exactly as long as
// the listener observes it — no orphaned `new LambdaConsumer<T>`
// hanging on the heap for the device lifetime.
//
// The SKValueListener itself is still `new`-allocated and not freed,
// because SensESP's SKListener::listeners_ static vector has no
// removal API (a stale entry would crash the WS subscribe loop). That
// leak is bounded (one per unique (path, kind) for device lifetime)
// and lives upstream; see subject_registry::garbage_collect.
template <class T, class Setter>
sensesp::SKValueListener<T>* attach(const std::string& path,
                                    lv_subject_t* subject, Setter setter) {
  auto* listener = new sensesp::SKValueListener<T>(
      String(path.c_str()), kListenDelayMs);
  listener->connect_to(std::make_shared<sensesp::LambdaConsumer<T>>(
      [subject, setter](T v) { setter(subject, v); }));
  return listener;
}

// Build a per-path metadata listener that feeds SK meta (zones +
// description) for `path` into ZoneRegistry. Replaces the removed
// SKWSClient::on_meta wildcard: instead of one hook receiving meta for
// every path and self-filtering, each bound path gets its own listener
// created here at bind time (the set of bound paths is exactly what we
// need zones for).
//
// The LambdaConsumer fires on the event_loop task (SKMetadataListener
// is a ValueProducer drained there), so apply_meta — which calls
// lv_subject_notify — is safe to invoke directly with no marshaling.
//
// Same leak-is-bounded lifetime as attach(): `new`-allocated and never
// freed. SKListener now has a destructor that unregisters, but there is
// still no removal path from the SubjectRegistry side (subjects live
// for the device lifetime, see garbage_collect), so the listener must
// outlive the subscription and matches the SKValueListener pattern.
void attach_meta(const std::string& path) {
  auto* listener = new sensesp::SKMetadataListener(
      String(path.c_str()), kListenDelayMs);
  std::string p = path;
  listener->connect_to(
      std::make_shared<sensesp::LambdaConsumer<sensesp::SKMetaView>>(
          [p](const sensesp::SKMetaView& m) {
            // raw is the full received meta document; its ["value"] is
            // the meta object {units, description, zones, ...} that
            // apply_meta parses. Null on a default (no-data-yet) view.
            if (!m.raw) return;
            JsonObjectConst meta = (*m.raw)["value"].as<JsonObjectConst>();
            if (meta.isNull()) return;
            zones().apply_meta(p, meta);
          }));
}

}  // namespace

lv_subject_t* SubjectRegistry::get_or_create(const std::string& path,
                                             SubjectKind kind) {
  auto it = map_.find(path);
  if (it != map_.end()) {
    if (it->second.entry->kind != kind) {
      ESP_LOGW(TAG, "kind conflict on %s: existing=%d wanted=%d",
               path.c_str(), (int)it->second.entry->kind, (int)kind);
      return nullptr;
    }
    return &it->second.entry->subject;
  }

  auto entry = std::make_unique<SubjectEntry>();
  entry->path = path;
  entry->kind = kind;
  entry->str_buf[0] = '\0';
  entry->str_prev[0] = '\0';

  switch (kind) {
    case SubjectKind::Float:
      lv_subject_init_float(&entry->subject, 0.f);
      break;
    case SubjectKind::Int:
    case SubjectKind::Bool:
      lv_subject_init_int(&entry->subject, 0);
      break;
    case SubjectKind::String:
      lv_subject_init_string(&entry->subject, entry->str_buf,
                             entry->str_prev, sizeof(entry->str_buf), "");
      break;
  }

  Slot slot;
  slot.entry = std::move(entry);

  // Wire a SensESP listener that pushes incoming SK values into the
  // subject. The listener self-registers in SKListener::listeners_ and
  // the WS client picks it up on next subscribe (re)build.
  switch (kind) {
    case SubjectKind::Float:
      attach<float>(path, &slot.entry->subject,
                    [](lv_subject_t* s, float v) { lv_subject_set_float(s, v); });
      break;
    case SubjectKind::Int:
      attach<int>(path, &slot.entry->subject,
                  [](lv_subject_t* s, int v) { lv_subject_set_int(s, v); });
      break;
    case SubjectKind::Bool:
      attach<bool>(path, &slot.entry->subject,
                   [](lv_subject_t* s, bool v) { lv_subject_set_int(s, v ? 1 : 0); });
      break;
    case SubjectKind::String:
      attach<String>(path, &slot.entry->subject,
                              [](lv_subject_t* s, String v) {
                                lv_subject_copy_string(s, v.c_str());
                              });
      break;
  }

  // Also subscribe to this path's SK metadata (zones + description) so
  // ZoneRegistry can tint bound widgets and swap labels to human names.
  // One SKMetadataListener per path, created exactly once here alongside
  // the value listener (both self-register for the next WS subscribe).
  attach_meta(path);

  lv_subject_t* sub = &slot.entry->subject;
  map_.emplace(path, std::move(slot));
  ESP_LOGI(TAG, "created subject for %s (kind=%d)", path.c_str(), (int)kind);

  return sub;
}

lv_subject_t* SubjectRegistry::lookup(const std::string& path) const {
  auto it = map_.find(path);
  if (it == map_.end()) return nullptr;
  return &it->second.entry->subject;
}

std::optional<SubjectKind> SubjectRegistry::kind_of(
    const std::string& path) const {
  auto it = map_.find(path);
  if (it == map_.end()) return std::nullopt;
  return it->second.entry->kind;
}

std::vector<std::string> SubjectRegistry::paths() const {
  std::vector<std::string> out;
  out.reserve(map_.size());
  for (auto& kv : map_) out.push_back(kv.first);
  return out;
}

void SubjectRegistry::garbage_collect(
    const std::set<std::string>& live_paths) {
  // SensESP SKListener has no public destructor and no removal from its
  // static listeners_ vector. Destroying our SubjectEntry while a
  // listener still references the subject would crash. v1 keeps every
  // subject for the device's lifetime; a v2 fix lives in SensESP, not
  // here. The argument is reserved for the API.
  (void)live_paths;
}

SubjectRegistry& registry() {
  static SubjectRegistry r;
  return r;
}

}  // namespace jlp
