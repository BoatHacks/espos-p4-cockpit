#include "widget_factory.h"

#include "../net/sk_put.h"
#include "../subject_registry.h"

namespace jlp {

namespace {

uint32_t kFgHex = 0xe6edf3;
uint32_t kMutedHex = 0x8b949e;

void apply_geometry(lv_obj_t* obj, JsonObjectConst spec) {
  lv_obj_set_pos(obj, spec["x"] | 0, spec["y"] | 0);
  lv_obj_set_size(obj, spec["w"] | 120, spec["h"] | 60);
}

// ---- label ----
//
// Optional `bind`: subscribes to a numeric path (Float subject) and
// formats per `display { unit, scale, offset, decimals }`. If `bind` is
// omitted, the label shows the static `label` text.
lv_obj_t* build_label(BuildCtx& ctx, JsonObjectConst spec, std::string* err) {
  lv_obj_t* lbl = lv_label_create(ctx.parent);
  apply_geometry(lbl, spec);
  lv_obj_set_style_text_color(lbl, lv_color_hex(kFgHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, LV_PART_MAIN);

  const char* path = spec["bind"] | (const char*)nullptr;
  const char* caption = spec["label"] | (const char*)nullptr;

  if (!path) {
    lv_label_set_text(lbl, caption ? caption : "");
    return lbl;
  }

  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Float);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  // Stash display config on the widget via user_data so the observer
  // can pick it up without per-widget structs. We need a small struct
  // anyway for unit / scale / offset / decimals.
  struct Disp {
    float scale;
    float offset;
    int decimals;
    char unit[12];
  };
  auto* d = new Disp{1.f, 0.f, 1, ""};
  JsonObjectConst display = spec["display"];
  if (!display.isNull()) {
    d->scale = display["scale"] | 1.f;
    d->offset = display["offset"] | 0.f;
    d->decimals = display["decimals"] | 1;
    const char* u = display["unit"] | "";
    snprintf(d->unit, sizeof(d->unit), "%s", u);
  }
  lv_obj_set_user_data(lbl, d);
  lv_obj_add_event_cb(
      lbl,
      [](lv_event_t* e) {
        delete static_cast<Disp*>(lv_obj_get_user_data(
            static_cast<lv_obj_t*>(lv_event_get_target(e))));
      },
      LV_EVENT_DELETE, nullptr);

  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        auto* d = static_cast<Disp*>(lv_obj_get_user_data(w));
        float v = lv_subject_get_float(s) * d->scale + d->offset;
        lv_label_set_text_fmt(w, "%.*f %s", d->decimals, v, d->unit);
      },
      lbl, nullptr);

  // Initial placeholder until first delta arrives.
  lv_label_set_text(lbl, "—");
  return lbl;
}

// ---- toggle ----
//
// Requires `bind` (Int subject). No optimistic latch — visual state
// follows the subscription only. PUT path (tap → server PUT → echo
// back) lands in step 7.
lv_obj_t* build_toggle(BuildCtx& ctx, JsonObjectConst spec, std::string* err) {
  const char* path = spec["bind"] | (const char*)nullptr;
  if (!path) { *err = "toggle: bind required"; return nullptr; }

  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Int);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  lv_obj_t* root = lv_obj_create(ctx.parent);
  apply_geometry(root, spec);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x161b22), LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 8, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  const char* caption = spec["label"] | (const char*)nullptr;
  if (caption) {
    lv_obj_t* l = lv_label_create(root);
    lv_obj_set_style_text_color(l, lv_color_hex(kMutedHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(l, caption);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
  }

  lv_obj_t* sw = lv_switch_create(root);
  lv_obj_align(sw, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        if (lv_subject_get_int(s)) lv_obj_add_state(w, LV_STATE_CHECKED);
        else                       lv_obj_remove_state(w, LV_STATE_CHECKED);
      },
      sw, nullptr);

  // Click → send PUT with the opposite of what's currently displayed.
  // No optimistic state mutation: we wait for the server echo.
  auto* path_owned = new std::string(path);
  lv_obj_add_event_cb(
      sw,
      [](lv_event_t* e) {
        auto* p = static_cast<std::string*>(lv_event_get_user_data(e));
        auto* w = static_cast<lv_obj_t*>(lv_event_get_target(e));
        bool now_on = lv_obj_has_state(w, LV_STATE_CHECKED);
        put_bool(*p, !now_on);
      },
      LV_EVENT_CLICKED, path_owned);
  lv_obj_add_event_cb(
      sw,
      [](lv_event_t* e) {
        delete static_cast<std::string*>(lv_event_get_user_data(e));
      },
      LV_EVENT_DELETE, path_owned);

  return root;
}

}  // namespace

lv_obj_t* build_widget(BuildCtx& ctx, JsonObjectConst spec,
                       std::string* err) {
  const char* type = spec["type"] | (const char*)nullptr;
  if (!type) { *err = "widget missing type"; return nullptr; }
  std::string t(type);
  if (t == "label")  return build_label(ctx, spec, err);
  if (t == "toggle") return build_toggle(ctx, spec, err);
  *err = std::string("unknown widget kind: ") + t;
  return nullptr;
}

}  // namespace jlp
