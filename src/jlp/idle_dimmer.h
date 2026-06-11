#pragma once

#include <stdint.h>

namespace jlp {

// Backlight power-save manager. Polls LVGL's input-inactivity counter
// once per second on the event_loop task and turns the panel backlight
// off after the configured idle timeout. Any touch event resets the
// counter automatically (LVGL does this), notifications and layout
// pushes wake explicitly via wake().
//
// 0 = disabled (backlight always on at on_brightness).
class IdleDimmer {
 public:
  // Wires the 1 Hz poll into event_loop. Call once at boot, after
  // lvgl_init().
  void init();

  // Reconfigure from a layout. Re-arms the idle window if currently
  // dimmed. dim_pct is the brightness while idle (0 = fully off).
  // On the Waveshare 7B the LCD's sync signals desensitize the GT911
  // touch grid below ~95 % brightness; tap-wake is only fully reliable
  // very close to full brightness. Lower dim levels save power but
  // require notifications or a fresh push to wake the panel.
  void configure(uint32_t idle_timeout_sec, uint8_t dim_pct);

  // Force the backlight on and re-arm the idle timer. Called from
  // notification deltas and layout pushes.
  void wake();

  bool is_on() const { return on_; }
  uint32_t idle_timeout_sec() const { return idle_timeout_sec_; }
  uint8_t dim_pct() const { return dim_pct_; }

 private:
  void set_on(bool on);

  uint32_t idle_timeout_sec_ = 0;   // 0 = disabled
  uint8_t on_brightness_pct_ = 95;  // matches init_backlight()
  // 80 % default: just enough drop to be clearly "asleep" while
  // staying close enough to full to keep GT911 tap-wake working
  // reliably. See header comment for why we can't go further.
  uint8_t dim_pct_ = 80;
  bool on_ = true;
};

IdleDimmer& idle_dimmer();

}  // namespace jlp
