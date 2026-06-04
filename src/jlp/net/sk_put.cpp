#include "sk_put.h"

#include <Arduino.h>
#include <unordered_map>

#include "esp_log.h"
#include "sensesp/signalk/signalk_put_request.h"

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

}  // namespace jlp
