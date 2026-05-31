#include "widget_factory.h"

#include "../net/sk_put.h"
#include "../subject_registry.h"
#include "../zone_registry.h"

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
  char path[80];  // bind path, for zone lookups in observers
};

Disp parse_display(JsonObjectConst spec) {
  Disp d{1.f, 0.f, 1, "", ""};
  JsonObjectConst display = spec["display"];
  if (!display.isNull()) {
    d.scale = display["scale"] | 1.f;
    d.offset = display["offset"] | 0.f;
    d.decimals = display["decimals"] | 1;
    snprintf(d.unit, sizeof(d.unit), "%s", display["unit"] | "");
  }
  snprintf(d.path, sizeof(d.path), "%s", spec["bind"] | "");
  return d;
}

void apply_geometry(lv_obj_t* obj, JsonObjectConst spec) {
  lv_obj_set_pos(obj, spec["x"] | 0, spec["y"] | 0);
  lv_obj_set_size(obj, spec["w"] | 120, spec["h"] | 60);
}

// Returns the zone-coded color for `display_value` on `path`, or
// `fallback` if the path has no zones or value is outside all zones.
uint32_t zone_color(const char* path, float display_value, uint32_t fallback) {
  if (!path || !*path) return fallback;
  const Zone* z = zones().match(path, display_value);
  return z ? color_for_state(z->state) : fallback;
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
// Render modes:
//   - caption only (no `bind`)     -> one large-font line of static text
//   - bind only   (no `label`)     -> body text large-font, centered
//   - both                         -> small-font caption on top, body
//                                     below (typical HMI tile layout)
//
// Body text is the SK meta `description` when one is published, else
// the formatted numeric value. This makes a label bound to e.g.
// `electrical.switches.bank.213.1.state` show "BMS DnC" instead of
// "1.0", which matches what operators read on the physical relay.
// The tile background is zone-tinted from the current value, same as
// toggle / arc / bar.
lv_obj_t* build_label(BuildCtx& ctx, JsonObjectConst spec, std::string* err) {
  const char* path = spec["bind"] | (const char*)nullptr;
  const char* caption = spec["label"] | (const char*)nullptr;

  // No bind: single static text label, return that directly.
  if (!path) {
    lv_obj_t* lbl = lv_label_create(ctx.parent);
    apply_geometry(lbl, spec);
    lv_obj_set_style_text_color(lbl, lv_color_hex(kFgHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_text(lbl, caption ? caption : "");
    return lbl;
  }

  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Float);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  lv_obj_t* root = lv_obj_create(ctx.parent);
  apply_geometry(root, spec);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x161b22), LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(root, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 4, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  if (caption && *caption) {
    lv_obj_t* cap = lv_label_create(root);
    lv_obj_set_style_text_color(cap, lv_color_hex(kMutedHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(cap, caption);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
  }

  lv_obj_t* val = lv_label_create(root);
  lv_obj_set_style_text_color(val, lv_color_hex(kFgHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_label_set_text(val, "—");
  if (caption && *caption) {
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 0, 20);
  } else {
    lv_obj_center(val);
  }

  // The observer needs both the value-label and the tile root so it
  // can update the text and the bg color on each delta. Stash both on
  // the value label.
  struct LabelCtx {
    Disp d;
    lv_obj_t* tile;
  };
  auto* lctx = new LabelCtx{parse_display(spec), root};
  lv_obj_set_user_data(val, lctx);
  lv_obj_add_event_cb(
      val,
      [](lv_event_t* e) {
        delete static_cast<LabelCtx*>(lv_obj_get_user_data(
            static_cast<lv_obj_t*>(lv_event_get_target(e))));
      },
      LV_EVENT_DELETE, nullptr);

  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        auto* lc = static_cast<LabelCtx*>(lv_obj_get_user_data(w));
        float v = lv_subject_get_float(s) * lc->d.scale + lc->d.offset;
        // Prefer the SK meta description over the formatted value.
        const std::string& desc = zones().description(lc->d.path);
        if (!desc.empty()) {
          lv_label_set_text(w, desc.c_str());
        } else {
          lv_label_set_text_fmt(w, "%.*f %s", lc->d.decimals, v, lc->d.unit);
        }
        uint32_t bg = zone_color(lc->d.path, v, 0x161b22);
        lv_obj_set_style_bg_color(lc->tile, lv_color_hex(bg), LV_PART_MAIN);
      },
      val, nullptr);

  return root;
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
  // LVGL default theme adds a 1-2 px outline + a soft shadow around
  // every lv_obj. Both extend past the geometric bounding box and
  // make tightly-spaced tiles look loose on the device (designer
  // doesn't replicate them). Zero both so the visible tile matches
  // the JSON (x,y,w,h) 1:1.
  lv_obj_set_style_outline_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(root, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 8, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // Inline layout: caption flushed left and vertically centered,
  // switch flushed right and vertically centered. Switch takes a
  // fixed comfortable touch size; caption fills the rest.
  const char* caption = spec["label"] | (const char*)nullptr;
  if (caption) {
    lv_obj_t* l = lv_label_create(root);
    lv_obj_set_style_text_color(l, lv_color_hex(kFgHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_label_set_text(l, caption);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  }

  lv_obj_t* sw = lv_switch_create(root);
  lv_obj_set_size(sw, 60, 30);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        if (lv_subject_get_int(s)) lv_obj_add_state(w, LV_STATE_CHECKED);
        else                       lv_obj_remove_state(w, LV_STATE_CHECKED);
      },
      sw, nullptr);

  // Tile background takes the zone color of the current int value, if
  // the bound path has zones. No-op for typical bool switches.
  auto* d_root = new Disp(parse_display(spec));
  lv_obj_set_user_data(root, d_root);
  lv_obj_add_event_cb(
      root,
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
        float v = (float)lv_subject_get_int(s) * d->scale + d->offset;
        uint32_t c = zone_color(d->path, v, 0x161b22);
        lv_obj_set_style_bg_color(w, lv_color_hex(c), LV_PART_MAIN);
      },
      root, nullptr);

  // Click → send PUT for the opposite of what the SUBSCRIPTION says
  // (not what LVGL just latched, since lv_switch flips itself on click
  // before our handler fires). Let LVGL's optimistic visual flip stand
  // briefly so the tap feels responsive, then 500ms later reconcile
  // against the subject — if no echo arrived, snap back to truth.
  // 500ms is comfortably above the ~300ms Maretron 126208 ACK round
  // trip so a successful command doesn't trigger a visible re-flip.
  struct TogglePressCtx {
    std::string path;
    lv_subject_t* sub;
    lv_obj_t* sw;
    lv_timer_t* reconcile;
  };
  auto* ctx_owned = new TogglePressCtx{path, sub, sw, nullptr};
  lv_obj_add_event_cb(
      sw,
      [](lv_event_t* e) {
        auto* c = static_cast<TogglePressCtx*>(lv_event_get_user_data(e));
        bool was_on = lv_subject_get_int(c->sub) != 0;
        put_bool(c->path, !was_on);
        // Cancel any prior pending reconcile — user can tap again
        // before the previous one fires.
        if (c->reconcile) {
          lv_timer_delete(c->reconcile);
          c->reconcile = nullptr;
        }
        c->reconcile = lv_timer_create(
            [](lv_timer_t* t) {
              auto* c = static_cast<TogglePressCtx*>(lv_timer_get_user_data(t));
              bool sub_on = lv_subject_get_int(c->sub) != 0;
              if (sub_on) lv_obj_add_state(c->sw, LV_STATE_CHECKED);
              else        lv_obj_remove_state(c->sw, LV_STATE_CHECKED);
              lv_timer_delete(t);
              c->reconcile = nullptr;
            },
            500, c);
        lv_timer_set_repeat_count(c->reconcile, 1);
      },
      LV_EVENT_CLICKED, ctx_owned);
  lv_obj_add_event_cb(
      sw,
      [](lv_event_t* e) {
        auto* c = static_cast<TogglePressCtx*>(lv_event_get_user_data(e));
        if (c->reconcile) lv_timer_delete(c->reconcile);
        delete c;
      },
      LV_EVENT_DELETE, ctx_owned);

  return root;
}

// ---- shared user_data struct for arc and bar ----
struct RangeBinding {
  Disp display;
  float min;  // display-space
  float max;
};

// ---- arc ----
//
// Layout: a transparent container of the user's geometry holds the
// arc widget (sized to fill, ignoring clicks) plus two child labels
// for caption and value. The labels are siblings of the arc, not
// children — `lv_arc` clips children to its arc shape which hides
// any centered text.
lv_obj_t* build_arc(BuildCtx& ctx, JsonObjectConst spec, std::string* err) {
  const char* path = spec["bind"] | (const char*)nullptr;
  if (!path) { *err = "arc: bind required"; return nullptr; }
  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Float);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  lv_obj_t* root = lv_obj_create(ctx.parent);
  apply_geometry(root, spec);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(root, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // Arcs are circular: take min(w,h) so they stay round regardless of
  // the user's bounding box. Extra width/height becomes empty space
  // around the arc (caption + value still center on root, which lines
  // them up inside the squared arc since the arc is centered too).
  int box_w = spec["w"] | 120;
  int box_h = spec["h"] | 60;
  int side = box_w < box_h ? box_w : box_h;
  lv_obj_t* arc = lv_arc_create(root);
  lv_obj_set_size(arc, side, side);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
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
        uint32_t c = zone_color(rb->display.path, v, kAccentHex);
        lv_obj_set_style_arc_color(w, lv_color_hex(c), LV_PART_INDICATOR);
      },
      arc, nullptr);

  // Caption + value as siblings of the arc, both centered on root.
  // Caption sits above the value when present.
  const char* caption = spec["label"] | (const char*)nullptr;
  lv_obj_t* val = lv_label_create(root);
  lv_obj_set_style_text_color(val, lv_color_hex(kFgHex), LV_PART_MAIN);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_label_set_text(val, "—");
  lv_obj_center(val);
  if (caption) {
    lv_obj_t* cap = lv_label_create(root);
    lv_obj_set_style_text_color(cap, lv_color_hex(kMutedHex), LV_PART_MAIN);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(cap, caption);
    lv_obj_align_to(cap, val, LV_ALIGN_OUT_TOP_MID, 0, -2);
  }

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

  return root;
}

// ---- bar ----
lv_obj_t* build_bar(BuildCtx& ctx, JsonObjectConst spec, std::string* err) {
  const char* path = spec["bind"] | (const char*)nullptr;
  if (!path) { *err = "bar: bind required"; return nullptr; }
  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Float);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  // Tile-style frame so the bar is identifiable as a widget even with
  // no live value (when value is 0 the indicator doesn't draw; only
  // the track shows). Caption + value text always visible.
  lv_obj_t* root = lv_obj_create(ctx.parent);
  apply_geometry(root, spec);
  lv_obj_set_style_bg_color(root, lv_color_hex(0x161b22), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(root, lv_color_hex(0x30363d), LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 1, LV_PART_MAIN);
  lv_obj_set_style_outline_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(root, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 8, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  bool vertical = spec["vertical"] | false;

  const char* caption = spec["label"] | (const char*)nullptr;
  if (caption) {
    lv_obj_t* cap = lv_label_create(root);
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
  lv_obj_t* bar = lv_bar_create(root);
  lv_bar_set_range(bar, 0, kBarSteps);
  // Track: medium grey, clearly visible against tile bg.
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x30363d), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(kAccentHex), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
  if (vertical) {
    lv_obj_set_size(bar, 24, lv_pct(75));
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  } else {
    lv_obj_set_size(bar, lv_pct(100), 24);
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
        uint32_t c = zone_color(rb->display.path, v, kAccentHex);
        lv_obj_set_style_bg_color(w, lv_color_hex(c), LV_PART_INDICATOR);
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
