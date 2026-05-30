#include "layout_manager.h"

#include <ArduinoJson.h>
#include <set>
#include <unordered_set>
#include "esp_log.h"

#include "../subject_registry.h"
#include "../widgets/widget_factory.h"

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

  // v1 non-atomic swap. Step 5 introduces detached-parent staging.
  if (current_root_) {
    lv_obj_delete(current_root_);
    current_root_ = nullptr;
  }

  lv_obj_t* root = lv_obj_create(parent_);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  std::set<std::string> live_paths;
  JsonArrayConst screens = doc["screens"];
  if (!build_screens(root, screens, registry(), &r.err, &live_paths,
                     &r.widgets)) {
    lv_obj_delete(root);
    return r;
  }

  current_root_ = root;
  r.ok = true;
  const char* name = doc["name"] | "(unnamed)";
  r.name = name;
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
