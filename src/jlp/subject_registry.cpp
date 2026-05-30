#include "subject_registry.h"

#include <Arduino.h>
#include "esp_log.h"
#include "sensesp/signalk/signalk_value_listener.h"
#include "sensesp/system/lambda_consumer.h"

static const char* TAG = "jlp.reg";

namespace jlp {

namespace {

constexpr int kListenDelayMs = 1000;

// Build a SensESP listener for `path` and pipe each emitted value into
// `subject` via `lv_subject_set_*`. Returns the listener pointer so the
// caller can store a teardown closure (though see note on SKListener
// lifetime — it has no public destructor, so teardown is a no-op in v1).
template <class T, class Setter>
sensesp::SKValueListener<T>* attach(const std::string& path,
                                    lv_subject_t* subject, Setter setter) {
  auto* listener = new sensesp::SKValueListener<T>(
      String(path.c_str()), kListenDelayMs);
  listener->connect_to(new sensesp::LambdaConsumer<T>(
      [subject, setter](T v) { setter(subject, v); }));
  return listener;
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
