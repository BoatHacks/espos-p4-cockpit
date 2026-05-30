#include "store.h"

#include "esp_littlefs.h"
#include "esp_log.h"
#include <cstdio>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static const char* TAG = "jlp.store";

namespace jlp {

namespace {
constexpr const char* kBase = "/lfs";
constexpr const char* kPath = "/lfs/layout.json";
constexpr const char* kTmpPath = "/lfs/layout.json.tmp";
constexpr const char* kPartitionLabel = "spiffs";

bool g_mounted = false;
}  // namespace

bool store_init() {
  if (g_mounted) return true;
  esp_vfs_littlefs_conf_t conf{};
  conf.base_path = kBase;
  conf.partition_label = kPartitionLabel;
  conf.format_if_mount_failed = true;
  conf.dont_mount = false;
  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "littlefs mount failed: %s", esp_err_to_name(err));
    return false;
  }
  size_t total = 0, used = 0;
  if (esp_littlefs_info(kPartitionLabel, &total, &used) == ESP_OK) {
    ESP_LOGI(TAG, "littlefs mounted at %s (%u/%u bytes used)", kBase,
             (unsigned)used, (unsigned)total);
  }
  g_mounted = true;
  return true;
}

bool store_read(std::string* out) {
  if (!g_mounted) return false;
  FILE* f = fopen(kPath, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz <= 0) { fclose(f); return false; }
  fseek(f, 0, SEEK_SET);
  out->resize(sz);
  size_t n = fread(&(*out)[0], 1, sz, f);
  fclose(f);
  if ((long)n != sz) {
    ESP_LOGW(TAG, "short read on %s: %u/%ld", kPath, (unsigned)n, sz);
    return false;
  }
  return true;
}

bool store_write_atomic(const std::string& json) {
  if (!g_mounted) return false;
  FILE* f = fopen(kTmpPath, "wb");
  if (!f) {
    ESP_LOGE(TAG, "open tmp failed: %s", strerror(errno));
    return false;
  }
  size_t n = fwrite(json.data(), 1, json.size(), f);
  bool ok = (n == json.size());
  if (fclose(f) != 0) ok = false;
  if (!ok) {
    ESP_LOGE(TAG, "write %s failed (%u/%u)", kTmpPath, (unsigned)n,
             (unsigned)json.size());
    remove(kTmpPath);
    return false;
  }
  if (rename(kTmpPath, kPath) != 0) {
    ESP_LOGE(TAG, "rename %s -> %s failed: %s", kTmpPath, kPath,
             strerror(errno));
    remove(kTmpPath);
    return false;
  }
  ESP_LOGI(TAG, "persisted layout (%u bytes)", (unsigned)json.size());
  return true;
}

}  // namespace jlp
