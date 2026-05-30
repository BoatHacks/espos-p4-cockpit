#include "layout_manager.h"

#include <ArduinoJson.h>
#include <set>
#include <unordered_set>
#include "esp_log.h"

#include "../status_overlay.h"
#include "../subject_registry.h"
#include "../widgets/widget_factory.h"
#include "store.h"

static const char* TAG = "jlp.layout";

namespace jlp {

namespace {

constexpr int kSchemaVersion = 1;
constexpr size_t kMaxJsonBytes = 64 * 1024;

// Builds the screen(s) under `root`. Single screen for v1; tabview
// added when len(screens) > 1.
bool build_screens(lv_obj_t* root, JsonArrayConst screens,
                   SubjectRegistry& reg, std::string* err,
                   std::set<std::string>* live_paths, unsigned* widget_count) {
  if (screens.size() == 0) { *err = "no screens"; return false; }

  if (screens.size() == 1) {
    JsonObjectConst s = screens[0];
    JsonArrayConst widgets = s["widgets"];
    BuildCtx ctx{root, reg, *live_paths};
    for (JsonObjectConst w : widgets) {
      if (!build_widget(ctx, w, err)) return false;
      ++*widget_count;
    }
    return true;
  }

  // Multi-screen: tabview.
  lv_obj_t* tv = lv_tabview_create(root);
  lv_tabview_set_tab_bar_size(tv, 36);
  lv_obj_set_size(tv, lv_pct(100), lv_pct(100));
  for (JsonObjectConst s : screens) {
    const char* title = s["title"] | "?";
    lv_obj_t* tab = lv_tabview_add_tab(tv, title);
    JsonArrayConst widgets = s["widgets"];
    BuildCtx ctx{tab, reg, *live_paths};
    for (JsonObjectConst w : widgets) {
      if (!build_widget(ctx, w, err)) return false;
      ++*widget_count;
    }
  }
  return true;
}

// Validate global structure before building (kind-conflict detection
// happens in get_or_create during build; here we catch duplicates and
// missing required fields cheaply).
bool validate(JsonObjectConst doc, std::string* err) {
  int schema = doc["schema"] | 0;
  if (schema != kSchemaVersion) {
    char buf[64];
    snprintf(buf, sizeof(buf), "unsupported schema %d (want %d)", schema,
             kSchemaVersion);
    *err = buf;
    return false;
  }
  JsonArrayConst screens = doc["screens"];
  if (screens.isNull() || screens.size() == 0) {
    *err = "no screens";
    return false;
  }

  std::unordered_set<std::string> ids;
  for (JsonObjectConst s : screens) {
    const char* sid = s["id"] | (const char*)nullptr;
    if (!sid) { *err = "screen missing id"; return false; }
    if (!ids.insert(sid).second) {
      *err = std::string("duplicate screen id: ") + sid;
      return false;
    }
    JsonArrayConst widgets = s["widgets"];
    std::unordered_set<std::string> wids;
    for (JsonObjectConst w : widgets) {
      const char* wid = w["id"] | (const char*)nullptr;
      if (!wid) { *err = "widget missing id"; return false; }
      if (!wids.insert(wid).second) {
        *err = std::string("duplicate widget id in screen ") + sid + ": " + wid;
        return false;
      }
      if (!(w["type"] | (const char*)nullptr)) {
        *err = std::string("widget missing type in screen ") + sid;
        return false;
      }
    }
  }
  return true;
}

}  // namespace

void LayoutManager::init(lv_obj_t* parent) { parent_ = parent; }

ApplyResult LayoutManager::apply(const std::string& json, ApplySource src) {
  ApplyResult r{false, "", "", 0, 0};
  if (!parent_) { r.err = "layout_manager not initialised"; return r; }

  if (json.size() > kMaxJsonBytes) {
    r.err = "layout too large";
    return r;
  }

  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, json);
  if (de) { r.err = std::string("parse: ") + de.c_str(); return r; }

  if (!validate(doc.as<JsonObjectConst>(), &r.err)) return r;

  // Atomic swap: build the new tree as a child of `parent_` but hidden,
  // then unhide on success and delete the old. If the build fails, the
  // current layout stays up untouched.
  //
  // (Earlier this code used lv_obj_create(NULL) which creates a SCREEN,
  // not a regular object — re-parenting a screen under another object
  // is undefined in LVGL 9 and resulted in widgets that never rendered.)
  lv_obj_t* staging = lv_obj_create(parent_);
  lv_obj_set_size(staging, lv_pct(100), lv_pct(100));
  lv_obj_set_pos(staging, 0, 0);
  lv_obj_set_style_bg_opa(staging, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(staging, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(staging, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(staging, 0, LV_PART_MAIN);
  lv_obj_clear_flag(staging, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(staging, LV_OBJ_FLAG_HIDDEN);

  std::set<std::string> live_paths;
  JsonArrayConst screens = doc["screens"];
  if (!build_screens(staging, screens, registry(), &r.err, &live_paths,
                     &r.widgets)) {
    lv_obj_delete(staging);
    return r;
  }

  // Apply status_overlay flag before the swap. Default true (matches
  // the original always-on behavior — existing layouts unchanged).
  // Toggling resizes content_root, so the staging tree needs to be
  // re-parented after the resize to inherit the new geometry.
  bool overlay_visible = true;
  if (doc["status_overlay"].is<bool>()) {
    overlay_visible = doc["status_overlay"];
  }
  overlay().set_visible(overlay_visible);

  lv_obj_t* old_root = current_root_;
  lv_obj_clear_flag(staging, LV_OBJ_FLAG_HIDDEN);
  current_root_ = staging;
  if (old_root) lv_obj_delete(old_root);

  // Persist only after a successful swap, and only for pushed layouts
  // (boot paths re-read the same persisted blob).
  if (src == ApplySource::PostLayout) {
    if (!store_write_atomic(json)) {
      r.warning = "layout rendered but not persisted";
    }
  }

  active_name_ = doc["name"] | "(unnamed)";
  active_source_ = src;
  if (post_swap_) post_swap_();
  r.ok = true;
  r.name = active_name_;
  r.screens = screens.size();
  ESP_LOGI(TAG, "applied layout '%s' (src=%d screens=%u widgets=%u paths=%u)",
           r.name.c_str(), (int)src, r.screens, r.widgets,
           (unsigned)live_paths.size());
  return r;
}

LayoutManager& layout_manager() {
  static LayoutManager m;
  return m;
}

}  // namespace jlp
