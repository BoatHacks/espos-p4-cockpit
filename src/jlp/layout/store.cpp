#include "store.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "jlp.store";

namespace jlp {

namespace {
// Dedicated NVS partition (see p4_16mb.csv). Isolated from the system
// "nvs" partition that holds WiFi creds so a layout write can never
// crowd those out. LittleFS was abandoned here: on this flash it did
// not survive a power-cycle — the superblock came back unmountable and
// got silently reformatted, dropping the persisted layout every time.
// NVS is wear-leveled and power-fail-safe.
constexpr const char* kPartition = "layout";
constexpr const char* kNamespace = "jlp";
constexpr const char* kKey = "layout";  // blob key

bool g_ready = false;

// Boot diagnostic, surfaced via /hello.
char g_boot_report[96] = "uninit";
}  // namespace

const char* store_boot_report() { return g_boot_report; }

bool store_init() {
  if (g_ready) return true;

  esp_err_t err = nvs_flash_init_partition(kPartition);
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // First boot on a blank/old-format partition: erase + re-init. This
    // is the ONLY destructive path and only runs when there's nothing
    // valid to lose.
    ESP_LOGW(TAG, "nvs partition needs erase (%s); erasing",
             esp_err_to_name(err));
    nvs_flash_erase_partition(kPartition);
    err = nvs_flash_init_partition(kPartition);
  }
  if (err != ESP_OK) {
    snprintf(g_boot_report, sizeof(g_boot_report), "nvs init failed: %s",
             esp_err_to_name(err));
    ESP_LOGE(TAG, "nvs_flash_init_partition failed: %s",
             esp_err_to_name(err));
    return false;
  }
  g_ready = true;

  // Report whether a layout blob is present + its size, for /hello.
  nvs_handle_t h;
  size_t sz = 0;
  if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &h) ==
      ESP_OK) {
    nvs_get_blob(h, kKey, nullptr, &sz);  // sz set to blob length
    nvs_close(h);
  }
  snprintf(g_boot_report, sizeof(g_boot_report), "nvs ok layout_sz=%u",
           (unsigned)sz);
  return true;
}

bool store_read(std::string* out) {
  if (!g_ready) return false;
  nvs_handle_t h;
  if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &h) !=
      ESP_OK) {
    return false;
  }
  size_t sz = 0;
  esp_err_t err = nvs_get_blob(h, kKey, nullptr, &sz);
  if (err != ESP_OK || sz == 0) {
    nvs_close(h);
    return false;
  }
  out->resize(sz);
  err = nvs_get_blob(h, kKey, &(*out)[0], &sz);
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_get_blob failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool store_write_atomic(const std::string& json) {
  if (!g_ready) return false;
  nvs_handle_t h;
  esp_err_t err =
      nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_open (rw) failed: %s", esp_err_to_name(err));
    return false;
  }
  // NVS set_blob + commit is itself atomic and power-fail-safe: the new
  // value isn't visible until commit, and a power loss mid-write leaves
  // the prior value intact. No tmp+rename dance needed.
  err = nvs_set_blob(h, kKey, json.data(), json.size());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs persist failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "persisted layout (%u bytes)", (unsigned)json.size());
  return true;
}

bool store_clear() {
  if (!g_ready) return false;
  nvs_handle_t h;
  if (nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &h) !=
      ESP_OK) {
    return false;
  }
  esp_err_t err = nvs_erase_key(h, kKey);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  // ESP_ERR_NVS_NOT_FOUND == already absent: same outcome as success.
  if (err == ESP_ERR_NVS_NOT_FOUND) return true;
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs erase failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "cleared layout");
  return true;
}

bool store_flag_get(const char* key, bool dflt) {
  nvs_handle_t h;
  if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &h) !=
      ESP_OK) {
    return dflt;  // partition not mounted yet, or nothing ever written
  }
  uint8_t v = 0;
  esp_err_t err = nvs_get_u8(h, key, &v);
  nvs_close(h);
  if (err != ESP_OK) return dflt;
  return v != 0;
}

bool store_flag_set(const char* key, bool value) {
  nvs_handle_t h;
  esp_err_t err =
      nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs_open (rw) failed for '%s': %s", key,
             esp_err_to_name(err));
    return false;
  }
  err = nvs_set_u8(h, key, value ? 1 : 0);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "nvs write '%s' failed: %s", key, esp_err_to_name(err));
    return false;
  }
  return true;
}

}  // namespace jlp
