#include "widget_factory.h"

#include "../net/sk_put.h"
#include "../subject_registry.h"

namespace jlp {

namespace {

uint32_t kFgHex = 0xe6edf3;
uint32_t kMutedHex = 0x8b949e;
uint32_t kAccentHex = 0x58a6ff;

constexpr int32_t kBarSteps = 1000;  // LVGL bar/arc integer range

struct Disp {
  float scale;
  float offset;
  int decimals;
  char unit[12];
};

Disp parse_display(JsonObjectConst spec) {
  Disp d{1.f, 0.f, 1, ""};
  JsonObjectConst display = spec["display"];
  if (!display.isNull()) {
    d.scale = display["scale"] | 1.f;
    d.offset = display["offset"] | 0.f;
    d.decimals = display["decimals"] | 1;
    snprintf(d.unit, sizeof(d.unit), "%s", display["unit"] | "");
  }
  return d;
}

void apply_geometry(lv_obj_t* obj, JsonObjectConst spec) {
  lv_obj_set_pos(obj, spec["x"] | 0, spec["y"] | 0);
  lv_obj_set_size(obj, spec["w"] | 120, spec["h"] | 60);
}

// Map a display-space value into [0, kBarSteps] for LVGL.
int32_t scale_to_steps(float display_value, float min, float max) {
  if (max <= min) return 0;
  float n = (display_value - min) / (max - min);
  if (n < 0) n = 0;
  if (n > 1) n = 1;
  return (int32_t)(n * kBarSteps + 0.5f);
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

  // Display config travels via user_data so the observer doesn't need
  // its own closure capture.
  auto* d = new Disp(parse_display(spec));
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

// ---- shared user_data struct for arc and bar ----
struct RangeBinding {
  Disp display;
  float min;  // display-space
  float max;
};

// ---- arc ----
lv_obj_t* build_arc(BuildCtx& ctx, JsonObjectConst spec, std::string* err) {
  const char* path = spec["bind"] | (const char*)nullptr;
  if (!path) { *err = "arc: bind required"; return nullptr; }
  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Float);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  lv_obj_t* arc = lv_arc_create(ctx.parent);
  apply_geometry(arc, spec);
  lv_arc_set_range(arc, 0, kBarSteps);
  int sa = spec["start_angle"] | 135;
  int ea = spec["end_angle"] | 45;
  lv_arc_set_bg_angles(arc, sa, ea);
  lv_arc_set_angles(arc, sa, sa);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x30363d), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(kAccentHex), LV_PART_INDICATOR);

  auto* rb_arc = new RangeBinding{parse_display(spec),
                                  spec["min"] | 0.f, spec["max"] | 100.f};
  lv_obj_set_user_data(arc, rb_arc);
  auto free_rb = [](lv_event_t* e) {
    delete static_cast<RangeBinding*>(lv_obj_get_user_data(
        static_cast<lv_obj_t*>(lv_event_get_target(e))));
  };
  lv_obj_add_event_cb(arc, free_rb, LV_EVENT_DELETE, nullptr);

  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        auto* rb = static_cast<RangeBinding*>(lv_obj_get_user_data(w));
        float v = lv_subject_get_float(s) * rb->display.scale + rb->display.offset;
        lv_arc_set_value(w, scale_to_steps(v, rb->min, rb->max));
      },
      arc, nullptr);

  // Center caption + value label as children of the arc.
  const char* caption = spec["label"] | (const char*)nullptr;
  if (caption) {
    lv_obj_t* cap = lv_label_create(arc);
    lv_obj_set_style_text_color(cap, lv_color_hex(kMutedHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(cap, caption);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, -18);
  }
  lv_obj_t* val = lv_label_create(arc);
  lv_obj_set_style_text_color(val, lv_color_hex(kFgHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_obj_align(val, LV_ALIGN_CENTER, 0, 8);
  lv_label_set_text(val, "—");
  auto* rb_val = new RangeBinding(*rb_arc);
  lv_obj_set_user_data(val, rb_val);
  lv_obj_add_event_cb(val, free_rb, LV_EVENT_DELETE, nullptr);
  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        auto* rb = static_cast<RangeBinding*>(lv_obj_get_user_data(w));
        float v = lv_subject_get_float(s) * rb->display.scale + rb->display.offset;
        lv_label_set_text_fmt(w, "%.*f %s", rb->display.decimals, v,
                              rb->display.unit);
      },
      val, nullptr);

  return arc;
}

// ---- bar ----
lv_obj_t* build_bar(BuildCtx& ctx, JsonObjectConst spec, std::string* err) {
  const char* path = spec["bind"] | (const char*)nullptr;
  if (!path) { *err = "bar: bind required"; return nullptr; }
  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Float);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  lv_obj_t* root = lv_obj_create(ctx.parent);
  apply_geometry(root, spec);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 4, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  bool vertical = spec["vertical"] | false;
  lv_obj_t* bar = lv_bar_create(root);
  lv_bar_set_range(bar, 0, kBarSteps);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x21262d), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(kAccentHex), LV_PART_INDICATOR);

  const char* caption = spec["label"] | (const char*)nullptr;
  lv_obj_t* cap = nullptr;
  if (caption) {
    cap = lv_label_create(root);
    lv_obj_set_style_text_color(cap, lv_color_hex(kMutedHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(cap, caption);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
  }

  lv_obj_t* val = lv_label_create(root);
  lv_obj_set_style_text_color(val, lv_color_hex(kFgHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_label_set_text(val, "—");
  lv_obj_align(val, LV_ALIGN_TOP_RIGHT, 0, 0);

  // Bar takes the rest of the box.
  if (vertical) {
    lv_obj_set_size(bar, 16, lv_pct(70));
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  } else {
    lv_obj_set_size(bar, lv_pct(100), 16);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  }

  // Two heap copies: bar's observer reads bar's user_data, val's
  // observer reads val's user_data. Each widget owns its copy.
  auto* rb_bar = new RangeBinding{parse_display(spec),
                                  spec["min"] | 0.f, spec["max"] | 100.f};
  auto* rb_val = new RangeBinding(*rb_bar);
  lv_obj_set_user_data(bar, rb_bar);
  lv_obj_set_user_data(val, rb_val);
  auto free_rb = [](lv_event_t* e) {
    delete static_cast<RangeBinding*>(lv_obj_get_user_data(
        static_cast<lv_obj_t*>(lv_event_get_target(e))));
  };
  lv_obj_add_event_cb(bar, free_rb, LV_EVENT_DELETE, nullptr);
  lv_obj_add_event_cb(val, free_rb, LV_EVENT_DELETE, nullptr);

  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        auto* rb = static_cast<RangeBinding*>(lv_obj_get_user_data(w));
        float v = lv_subject_get_float(s) * rb->display.scale + rb->display.offset;
        lv_bar_set_value(w, scale_to_steps(v, rb->min, rb->max), LV_ANIM_OFF);
      },
      bar, nullptr);
  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        auto* rb = static_cast<RangeBinding*>(lv_obj_get_user_data(w));
        float v = lv_subject_get_float(s) * rb->display.scale + rb->display.offset;
        lv_label_set_text_fmt(w, "%.*f %s", rb->display.decimals, v,
                              rb->display.unit);
      },
      val, nullptr);

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
  if (t == "arc")    return build_arc(ctx, spec, err);
  if (t == "bar")    return build_bar(ctx, spec, err);
  *err = std::string("unknown widget kind: ") + t;
  return nullptr;
}

}  // namespace jlp
