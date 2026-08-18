#include "alert_overlay.h"

#include "esp_log.h"
#include "audio/chime.h"
#include "net/sk_put.h"
#include "notifications_registry.h"
#include "zone_registry.h"  // color_for_state

static const char* TAG = "jlp.alert";

namespace jlp {

namespace {

uint32_t color_for_not_state(NotState s) {
  // Reuse the maritime palette from ZoneState. The mapping is
  // direct since NotState and ZoneState use the same names.
  switch (s) {
    case NotState::Nominal:
    case NotState::Normal:    return 0x3fb950;  // green (shouldn't show)
    case NotState::Alert:     return 0xd29922;  // yellow
    case NotState::Warn:      return 0xdb6d28;  // orange
    case NotState::Alarm:     return 0xf85149;  // red
    case NotState::Emergency: return 0xa371f7;  // purple
  }
  return 0xf85149;
}

}  // namespace

void AlertOverlay::init() {
  // The modal lives on the active screen at z-order = top. Created
  // hidden; rebuild() will show it when there's a qualifying
  // notification to display.
  lv_obj_t* scr = lv_screen_active();
  root_ = lv_obj_create(scr);
  lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
  lv_obj_align(root_, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(root_, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root_, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_border_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(root_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root_, 32, LV_PART_MAIN);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
  // Stay above future siblings (the layout tree is re-parented
  // under scr on every swap; we keep the overlay above that).
  lv_obj_move_foreground(root_);

  state_label_ = lv_label_create(root_);
  lv_obj_set_style_text_color(state_label_, lv_color_hex(0xf85149),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(state_label_, &lv_font_montserrat_28,
                             LV_PART_MAIN);
  lv_label_set_text(state_label_, "ALARM");
  lv_obj_align(state_label_, LV_ALIGN_TOP_MID, 0, 0);

  path_label_ = lv_label_create(root_);
  lv_obj_set_style_text_color(path_label_, lv_color_hex(0x8b949e),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(path_label_, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_label_set_text(path_label_, "");
  lv_obj_align_to(path_label_, state_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

  msg_label_ = lv_label_create(root_);
  lv_obj_set_style_text_color(msg_label_, lv_color_hex(0xe6edf3),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(msg_label_, &lv_font_montserrat_20,
                             LV_PART_MAIN);
  lv_label_set_text(msg_label_, "");
  lv_label_set_long_mode(msg_label_, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg_label_, LV_HOR_RES - 80);
  lv_obj_align(msg_label_, LV_ALIGN_CENTER, 0, -20);
  lv_obj_set_style_text_align(msg_label_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  ack_button_ = lv_button_create(root_);
  lv_obj_set_size(ack_button_, 220, 80);
  lv_obj_align(ack_button_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(ack_button_, lv_color_hex(0x3fb950), LV_PART_MAIN);
  lv_obj_set_style_radius(ack_button_, 8, LV_PART_MAIN);
  lv_obj_t* ack_lbl = lv_label_create(ack_button_);
  lv_obj_set_style_text_color(ack_lbl, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_text_font(ack_lbl, &lv_font_montserrat_28, LV_PART_MAIN);
  lv_label_set_text(ack_lbl, "ACK");
  lv_obj_center(ack_lbl);

  lv_obj_add_event_cb(
      ack_button_,
      [](lv_event_t*) {
        AlertOverlay& self = alert_overlay();
        if (!self.current_path_.empty()) {
          // acknowledge() fires on_change(), which rebuilds the overlay
          // and moves current_path_ on — take a copy first so the SK ACK
          // names the alarm the operator actually tapped.
          const std::string path = self.current_path_;
          // Local ack: suppress this path in the registry so the
          // overlay dismisses NOW and the device stays usable, even
          // though the N2K bus keeps re-asserting the alarm. Re-arms
          // when the condition clears or escalates (see the registry).
          notifications().acknowledge(path);
          // Best-effort: also tell SK we acknowledged. Harmless for
          // bus-sourced alarms (they re-assert), and lets
          // server-mediated/UI notifications clear properly.
          put_notification_ack(path);
        }
      },
      LV_EVENT_CLICKED, nullptr);

  notifications().on_change([this]() {
    // Sound BEFORE rebuild, and only on an escalation (a notification
    // appeared or worsened) — never on a clear, ack, or message edit,
    // so acking one alarm doesn't re-chime the next-most-severe. Gate
    // on the same enabled_/min_state_ the overlay shows, so a muted or
    // below-threshold notification stays silent. configure()'s rebuild
    // (layout swaps) doesn't run through here, so swaps are silent too.
    // Chime only when the alert that ESCALATED asked for sound (via its
    // SK `method`) — not most_severe(), which could be a different,
    // quieter alert. A visual-only or server-silenced alert shows on the
    // overlay but stays quiet. Still gate on min_state via most_severe so
    // a below-threshold escalation doesn't beep.
    if (enabled_ && notifications().last_change_was_escalation() &&
        notifications().last_escalation_wants_sound()) {
      const Notification* n = notifications().most_severe();
      if (n && n->state >= min_state_) chime().play(n->state);
    }
    rebuild();
  });

  ESP_LOGI(TAG, "alert overlay initialised");
}

void AlertOverlay::configure(bool enabled, NotState min_state) {
  enabled_ = enabled;
  min_state_ = min_state;
  rebuild();
}

void AlertOverlay::rebuild() {
  if (!root_) return;
  lv_obj_move_foreground(root_);  // re-pin above any newly-swapped layout
  if (!enabled_) {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    current_path_.clear();
    return;
  }
  const Notification* n = notifications().most_severe();
  if (!n || n->state < min_state_) {
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    current_path_.clear();
    return;
  }
  current_path_ = n->path;
  lv_label_set_text(state_label_, not_state_name(n->state));
  lv_obj_set_style_text_color(state_label_,
                              lv_color_hex(color_for_not_state(n->state)),
                              LV_PART_MAIN);
  lv_label_set_text(path_label_, n->path.c_str());
  lv_label_set_text(msg_label_, n->message.empty() ? "(no message)"
                                                   : n->message.c_str());
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

AlertOverlay& alert_overlay() {
  static AlertOverlay r;
  return r;
}

}  // namespace jlp
