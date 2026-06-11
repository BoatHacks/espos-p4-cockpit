#include "idle_dimmer.h"

#include "esp_log.h"
#include "lvgl.h"

#include "sensesp_base_app.h"
#include "sensesp_cockpit_display/lvgl/lv_drivers.h"

static const char* TAG = "jlp.dimmer";

namespace jlp {

void IdleDimmer::set_on(bool on) {
  if (on == on_) return;
  on_ = on;
  auto* d = sensesp_cockpit_display::get_display();
  if (!d) return;
  if (on) {
    // Wake order matters: turn the panel sync back on BEFORE raising
    // the backlight, so the user doesn't see a flash of stale VRAM.
    d->set_display_on(true);
    // LVGL only redraws dirty regions; after a panel sleep the active
    // screen is "clean" so nothing is pushed until something changes.
    // Invalidate the whole screen so the next tick paints immediately.
    lv_obj_t* scr = lv_screen_active();
    if (scr) lv_obj_invalidate(scr);
    d->set_brightness(on_brightness_pct_);
  } else {
    // When dim_pct is 0, also put the panel itself to sleep. The LCD's
    // active sync signals couple into the GT911 touch grid, so with
    // them off the touch chip can still detect taps at 0% backlight.
    // At any non-zero dim_pct, keep the panel running so the dimmed
    // content stays visible.
    d->set_brightness(dim_pct_);
    if (dim_pct_ == 0) d->set_display_on(false);
  }
  ESP_LOGI(TAG, "backlight %s (dim_pct=%u)", on ? "on" : "off",
           (unsigned)dim_pct_);
}

void IdleDimmer::init() {
  // LVGL tracks input inactivity automatically. Poll on a 1 Hz cadence
  // — finer resolution is wasteful for human-scale timeouts.
  sensesp::event_loop()->onRepeat(1000, [this]() {
    if (idle_timeout_sec_ == 0) {
      if (!on_) set_on(true);
      return;
    }
    uint32_t inactive_ms = lv_display_get_inactive_time(NULL);
    bool should_be_on = inactive_ms < idle_timeout_sec_ * 1000U;
    if (should_be_on != on_) set_on(should_be_on);
  });
}

void IdleDimmer::configure(uint32_t idle_timeout_sec, uint8_t dim_pct) {
  if (dim_pct > 100) dim_pct = 100;
  if (idle_timeout_sec_ == idle_timeout_sec && dim_pct_ == dim_pct) return;
  idle_timeout_sec_ = idle_timeout_sec;
  dim_pct_ = dim_pct;
  ESP_LOGI(TAG, "idle timeout=%us dim_pct=%u",
           (unsigned)idle_timeout_sec_, (unsigned)dim_pct_);
  // Re-arm the idle window so a freshly-pushed layout isn't immediately
  // dimmed out (the user is actively looking at the panel right now).
  wake();
}

void IdleDimmer::wake() {
  lv_display_trigger_activity(NULL);
  if (!on_) set_on(true);
}

IdleDimmer& idle_dimmer() {
  static IdleDimmer d;
  return d;
}

}  // namespace jlp
