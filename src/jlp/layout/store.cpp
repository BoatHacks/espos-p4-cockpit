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
constexpr const char* kProbePath = "/lfs/.probe";
constexpr const char* kPartitionLabel = "spiffs";

bool g_mounted = false;

// Mount the partition (formatting on mount failure). Returns true on a
// successful mount. Does NOT validate writability — see probe_writable.
bool mount() {
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
  return true;
}

// Exercise the EXACT write path (tmp file + rename) the real layout
// write uses, so the boot probe trips on the same corruption the
// first push otherwise would. A probe that touches a different path
// or does a smaller write can pass while the real write still fails.
// Returns true iff a full write+rename round-trips cleanly.
bool probe_write_path() {
  // ~512 bytes — large enough to allocate a fresh block like a real
  // layout, which is where the first-write-after-mount corruption
  // surfaces. (A 3-byte canary stayed inside the metadata block and
  // didn't reproduce it.)
  std::string filler(512, 'x');
  FILE* f = fopen(kTmpPath, "wb");
  if (!f) return false;
  bool ok = fwrite(filler.data(), 1, filler.size(), f) == filler.size();
  if (fclose(f) != 0) ok = false;
  if (ok && rename(kTmpPath, kProbePath) != 0) ok = false;
  remove(kTmpPath);
  remove(kProbePath);
  return ok;
}
}  // namespace

bool store_init() {
  if (g_mounted) return true;
  if (!mount()) return false;

  // LittleFS's lazy format-on-mount (format_if_mount_failed) lays down
  // a minimal superblock that, on this flash, fails the FIRST real
  // write-of-a-new-block with "Corrupted dir pair at {0x1,0x0}". The
  // existing reformat-on-write-failure path heals it, but only AFTER
  // the user's first push pays the reformat. To make that first push
  // clean, do a full write-path probe here at boot and, if it fails,
  // explicitly format + re-probe until the FS round-trips a real
  // write. Up to two attempts — one format almost always settles it.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (probe_write_path()) {
      if (attempt > 0) ESP_LOGI(TAG, "littlefs writable after reformat");
      g_mounted = true;
      return true;
    }
    ESP_LOGW(TAG, "littlefs not writable at boot; reformatting (try %d)",
             attempt + 1);
    esp_vfs_littlefs_unregister(kPartitionLabel);
    if (esp_littlefs_format(kPartitionLabel) != ESP_OK) {
      ESP_LOGE(TAG, "boot reformat failed");
    }
    if (!mount()) return false;
  }
  ESP_LOGE(TAG, "littlefs still not writable after 2 reformats; "
                "writes will reformat-retry per push");
  g_mounted = true;  // mounted for reads; writes self-heal via retry
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

// One write attempt: tmp + rename. Returns true on success, errno
// describes the failure path on false.
static bool try_write_atomic(const std::string& json) {
  FILE* f = fopen(kTmpPath, "wb");
  if (!f) {
    ESP_LOGW(TAG, "open tmp failed: %s", strerror(errno));
    return false;
  }
  size_t n = fwrite(json.data(), 1, json.size(), f);
  bool ok = (n == json.size());
  if (fclose(f) != 0) ok = false;
  if (!ok) {
    ESP_LOGW(TAG, "write %s failed (%u/%u)", kTmpPath, (unsigned)n,
             (unsigned)json.size());
    remove(kTmpPath);
    return false;
  }
  if (rename(kTmpPath, kPath) != 0) {
    ESP_LOGW(TAG, "rename %s -> %s failed: %s", kTmpPath, kPath,
             strerror(errno));
    remove(kTmpPath);
    return false;
  }
  return true;
}

bool store_write_atomic(const std::string& json) {
  if (!g_mounted) return false;
  if (try_write_atomic(json)) {
    ESP_LOGI(TAG, "persisted layout (%u bytes)", (unsigned)json.size());
    return true;
  }
  // LittleFS can land in a corrupted-dir-pair state where individual
  // writes fail but the mount stays "successful". Reformat and retry
  // once. Losing the partition contents is fine — the only data on it
  // is the layout we're about to (re)write.
  ESP_LOGW(TAG, "write failed; attempting littlefs reformat + retry");
  esp_vfs_littlefs_unregister(kPartitionLabel);
  g_mounted = false;
  if (esp_littlefs_format(kPartitionLabel) != ESP_OK) {
    ESP_LOGE(TAG, "littlefs format failed");
    // Re-mount whatever state we can; further writes may still fail.
    store_init();
    return false;
  }
  if (!store_init()) {
    ESP_LOGE(TAG, "remount after format failed");
    return false;
  }
  if (try_write_atomic(json)) {
    ESP_LOGI(TAG, "persisted layout after reformat (%u bytes)",
             (unsigned)json.size());
    return true;
  }
  ESP_LOGE(TAG, "persist still failing after reformat");
  return false;
}

bool store_clear() {
  if (!g_mounted) return false;
  if (remove(kPath) == 0) {
    ESP_LOGI(TAG, "cleared %s", kPath);
    return true;
  }
  if (errno == ENOENT) {
    // Already gone — same outcome as success from the caller's view.
    return true;
  }
  ESP_LOGW(TAG, "clear %s failed: %s", kPath, strerror(errno));
  return false;
}

}  // namespace jlp
