#include "sk_put.h"

#include <Arduino.h>
#include <unordered_map>

#include "esp_log.h"
#include "sensesp/signalk/signalk_put_request.h"

static const char* TAG = "jlp.put";

namespace jlp {

namespace {

// One SKPutRequest per (path, kind). int and bool are tracked
// separately since the JSON types differ on the wire.
std::unordered_map<std::string, sensesp::SKPutRequest<bool>*> g_bool;
std::unordered_map<std::string, sensesp::SKPutRequest<int>*> g_int;

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

}  // namespace jlp
