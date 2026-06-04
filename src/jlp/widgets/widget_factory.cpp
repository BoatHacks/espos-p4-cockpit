#include "widget_factory.h"

#include <math.h>

#include "../net/sk_put.h"
#include "../subject_registry.h"
#include "../zone_registry.h"

namespace jlp {

namespace {

uint32_t kFgHex = 0xe6edf3;
uint32_t kMutedHex = 0x8b949e;
uint32_t kAccentHex = 0x58a6ff;
constexpr uint32_t kTileBgHex = 0x161b22;

constexpr int32_t kBarSteps = 1000;  // LVGL bar/arc integer range

// Per-widget color overrides. Hex strings in spec ("#rrggbb" or "#rgb").
// bg_color tints the tile background; fg_color tints the value text.
// SK zones still take precedence — these are only fallbacks when no
// zone matches (or when no bind/no zones are configured).
struct Colors {
  uint32_t bg;
  uint32_t fg;
};

// Parse "#rrggbb" or "#rgb" into a 24-bit hex color. Returns true on
// success; on failure (missing field, malformed) leaves *out untouched.
bool parse_hex_color(const char* s, uint32_t* out) {
  if (!s || *s != '#') return false;
  const char* h = s + 1;
  size_t n = strlen(h);
  if (n != 3 && n != 6) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < n; i++) {
    char c = h[i];
    uint32_t d;
    if      (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
    else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
    else return false;
    v = (v << 4) | d;
  }
  if (n == 3) {
    // expand 0xRGB to 0xRRGGBB
    uint32_t r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
    v = (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
  }
  *out = v;
  return true;
}

Colors parse_colors(JsonObjectConst spec) {
  Colors c{kTileBgHex, kFgHex};
  parse_hex_color(spec["bg_color"] | (const char*)nullptr, &c.bg);
  parse_hex_color(spec["fg_color"] | (const char*)nullptr, &c.fg);
  return c;
}

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
  const Colors colors = parse_colors(spec);

  // No bind: single static text label, return that directly.
  if (!path) {
    lv_obj_t* lbl = lv_label_create(ctx.parent);
    apply_geometry(lbl, spec);
    lv_obj_set_style_text_color(lbl, lv_color_hex(colors.fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_label_set_text(lbl, caption ? caption : "");
    return lbl;
  }

  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Float);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  lv_obj_t* root = lv_obj_create(ctx.parent);
  apply_geometry(root, spec);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors.bg), LV_PART_MAIN);
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
  lv_obj_set_style_text_color(val, lv_color_hex(colors.fg), LV_PART_MAIN);
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
    Colors colors;
  };
  auto* lctx = new LabelCtx{parse_display(spec), root, colors};
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
        float raw = lv_subject_get_float(s);
        float v = raw * lc->d.scale + lc->d.offset;
        // Prefer the SK meta description over the formatted value.
        const std::string& desc = zones().description(lc->d.path);
        if (!desc.empty()) {
          lv_label_set_text(w, desc.c_str());
        } else {
          lv_label_set_text_fmt(w, "%.*f %s", lc->d.decimals, v, lc->d.unit);
        }
        // Zones are in raw SK units (e.g. ratio 0..1 for SOC); match
        // against raw. Fall back to the spec'd bg_color when no zone
        // matches — zone always wins to keep alarms visible.
        uint32_t bg = zone_color(lc->d.path, raw, lc->colors.bg);
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
  const Colors colors = parse_colors(spec);

  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Int);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  lv_obj_t* root = lv_obj_create(ctx.parent);
  apply_geometry(root, spec);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors.bg), LV_PART_MAIN);
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
    lv_obj_set_style_text_color(l, lv_color_hex(colors.fg), LV_PART_MAIN);
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
  struct ToggleObsCtx {
    Disp d;
    Colors colors;
  };
  auto* d_root = new ToggleObsCtx{parse_display(spec), colors};
  lv_obj_set_user_data(root, d_root);
  lv_obj_add_event_cb(
      root,
      [](lv_event_t* e) {
        delete static_cast<ToggleObsCtx*>(lv_obj_get_user_data(
            static_cast<lv_obj_t*>(lv_event_get_target(e))));
      },
      LV_EVENT_DELETE, nullptr);
  lv_subject_add_observer_obj(
      sub,
      [](lv_observer_t* obs, lv_subject_t* s) {
        auto* w = lv_observer_get_target_obj(obs);
        auto* tc = static_cast<ToggleObsCtx*>(lv_obj_get_user_data(w));
        int32_t raw = lv_subject_get_int(s);
        // Zones live in raw SK units; match against raw, not display.
        // Fall back to the spec'd bg_color when no zone matches.
        uint32_t c = zone_color(tc->d.path, (float)raw, tc->colors.bg);
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
  Colors colors;  // bg/fg overrides; zone match still wins
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
  const Colors colors = parse_colors(spec);
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
  int sa = spec["start_angle"] | 135;
  int ea = spec["end_angle"] | 45;
  float v_min = spec["min"] | 0.f;
  float v_max = spec["max"] | 100.f;
  Disp tmp_disp = parse_display(spec);

  // Total sweep, normalised to 0..360. Bands map their (from, to)
  // values into the same sweep.
  int total_sweep = ea - sa;
  if (total_sweep <= 0) total_sweep += 360;

  // ---- Bands (advisory colored ring painted UNDER the indicator).
  // Each band is its own lv_arc with no indicator and a thin track
  // styled in the band's color. Created before the indicator so the
  // indicator paints on top.
  JsonArrayConst bands = spec["bands"];
  if (!bands.isNull()) {
    for (JsonObjectConst b : bands) {
      float from = b["from"] | 0.f;
      float to = b["to"] | 0.f;
      const char* hex = b["color"] | "#3fb950";
      if (to < from) { float t = to; to = from; from = t; }
      // Map [from, to] in display-space back to arc angle range.
      float span = v_max - v_min;
      if (span <= 0) continue;
      float t0 = (from * tmp_disp.scale + tmp_disp.offset - v_min) / span;
      float t1 = (to   * tmp_disp.scale + tmp_disp.offset - v_min) / span;
      if (t0 < 0) t0 = 0;
      if (t1 > 1) t1 = 1;
      if (t1 <= t0) continue;
      int ang0 = sa + (int)(total_sweep * t0);
      int ang1 = sa + (int)(total_sweep * t1);
      uint32_t color = 0x3fb950;
      parse_hex_color(hex, &color);

      lv_obj_t* band = lv_arc_create(root);
      lv_obj_set_size(band, side, side);
      lv_obj_align(band, LV_ALIGN_CENTER, 0, 0);
      lv_arc_set_bg_angles(band, ang0 % 360, ang1 % 360);
      // Zero the indicator — we only want the bg ring visible.
      lv_arc_set_angles(band, ang0 % 360, ang0 % 360);
      lv_obj_remove_style(band, NULL, LV_PART_KNOB);
      lv_obj_clear_flag(band, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_arc_color(band, lv_color_hex(color), LV_PART_MAIN);
      lv_obj_set_style_arc_width(band, 4, LV_PART_MAIN);
    }
  }

  // ---- Tick marks (drawn via small line segments).
  // Major ticks at evenly-spaced angles around the arc. No labels in
  // v1 to keep the firmware light; designer can show tick numerals
  // since SVG text is cheap on the browser.
  int tick_count = spec["ticks"] | 0;
  if (tick_count > 1) {
    static lv_point_precise_t tick_pts[2];  // reused per tick
    float r_outer = side / 2.0f;
    float r_inner = r_outer - 6.0f;
    if (r_inner < 0) r_inner = 0;
    float cx = side / 2.0f;
    float cy = side / 2.0f;
    for (int i = 0; i < tick_count; i++) {
      float t = (float)i / (float)(tick_count - 1);
      float a = sa + total_sweep * t;
      float rad = a * 3.14159265f / 180.0f;
      // lv_line takes points relative to its parent; create a tiny
      // 1x1 lv_line for each tick.
      lv_obj_t* tick = lv_line_create(root);
      tick_pts[0].x = (lv_value_precise_t)(cx + r_inner * cosf(rad)
                                           + (box_w - side) / 2);
      tick_pts[0].y = (lv_value_precise_t)(cy + r_inner * sinf(rad)
                                           + (box_h - side) / 2);
      tick_pts[1].x = (lv_value_precise_t)(cx + r_outer * cosf(rad)
                                           + (box_w - side) / 2);
      tick_pts[1].y = (lv_value_precise_t)(cy + r_outer * sinf(rad)
                                           + (box_h - side) / 2);
      lv_line_set_points(tick, tick_pts, 2);
      lv_obj_set_style_line_color(tick, lv_color_hex(kMutedHex), LV_PART_MAIN);
      lv_obj_set_style_line_width(tick, 1, LV_PART_MAIN);
    }
  }

  lv_obj_t* arc = lv_arc_create(root);
  lv_obj_set_size(arc, side, side);
  lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
  lv_arc_set_range(arc, 0, kBarSteps);
  lv_arc_set_bg_angles(arc, sa, ea);
  lv_arc_set_angles(arc, sa, sa);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x30363d), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(kAccentHex), LV_PART_INDICATOR);
  // Also lift the inactive bg arc above the bands so the bands sit
  // visibly OUTSIDE rather than fighting the track. Re-pin the arc
  // on top by setting it as the parent's last child via z-order:
  lv_obj_move_foreground(arc);
  (void)v_min; (void)v_max;  // captured by RangeBinding below

  auto* rb_arc = new RangeBinding{parse_display(spec),
                                  spec["min"] | 0.f, spec["max"] | 100.f,
                                  colors};
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
        float raw = lv_subject_get_float(s);
        float v = raw * rb->display.scale + rb->display.offset;
        lv_arc_set_value(w, scale_to_steps(v, rb->min, rb->max));
        // Zones live in raw SK units; match against raw, not display.
        // Fall back to fg_color (which doubles as the indicator color)
        // when no zone matches, else default accent.
        uint32_t fallback = rb->colors.fg != kFgHex ? rb->colors.fg : kAccentHex;
        uint32_t c = zone_color(rb->display.path, raw, fallback);
        lv_obj_set_style_arc_color(w, lv_color_hex(c), LV_PART_INDICATOR);
      },
      arc, nullptr);

  // Caption + value as siblings of the arc, both centered on root.
  // Caption sits above the value when present.
  const char* caption = spec["label"] | (const char*)nullptr;
  lv_obj_t* val = lv_label_create(root);
  lv_obj_set_style_text_color(val, lv_color_hex(colors.fg), LV_PART_MAIN);
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
  const Colors colors = parse_colors(spec);
  lv_subject_t* sub = ctx.reg.get_or_create(path, SubjectKind::Float);
  if (!sub) { *err = std::string("kind conflict on ") + path; return nullptr; }
  ctx.live_paths.insert(path);

  // Tile-style frame so the bar is identifiable as a widget even with
  // no live value (when value is 0 the indicator doesn't draw; only
  // the track shows). Caption + value text always visible.
  lv_obj_t* root = lv_obj_create(ctx.parent);
  apply_geometry(root, spec);
  lv_obj_set_style_bg_color(root, lv_color_hex(colors.bg), LV_PART_MAIN);
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
  lv_obj_set_style_text_color(val, lv_color_hex(colors.fg), LV_PART_MAIN);
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
                                  spec["min"] | 0.f, spec["max"] | 100.f,
                                  colors};
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
        float raw = lv_subject_get_float(s);
        float v = raw * rb->display.scale + rb->display.offset;
        lv_bar_set_value(w, scale_to_steps(v, rb->min, rb->max), LV_ANIM_OFF);
        // Zones live in raw SK units; match against raw, not display.
        // Fall back to fg_color override (indicator color) when no
        // zone matches, else default accent.
        uint32_t fallback = rb->colors.fg != kFgHex ? rb->colors.fg : kAccentHex;
        uint32_t c = zone_color(rb->display.path, raw, fallback);
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
