#pragma once

#include "lvgl.h"
#include <stdint.h>

namespace jlp {

// A small status strip pinned to the top of the active screen. Survives
// every layout swap — never created from user-supplied JSON. The
// remaining area of the screen is `content_root()` and is where the
// layout player parents its widget tree.
class StatusOverlay {
 public:
  void init();

  // Setters; safe to call from the event_loop task.
  void set_hostname(const char* hostname);
  void set_wifi(const char* line);                   // "MOIN -58dBm" or "down"
  void set_sk(const char* line);                     // "ok" / "connecting" ...
  void set_n2k(int64_t rx_idle_seconds, unsigned clients);
  void set_uptime_heap(uint32_t uptime_s, uint32_t free_heap);

  lv_obj_t* content_root() { return content_root_; }

  // Show or hide the strip. When hidden, content_root() expands to
  // fill the full screen. Re-show restores the strip and shrinks
  // content_root by kStripHeight from the top.
  void set_visible(bool visible);
  bool is_visible() const { return visible_; }

 private:
  lv_obj_t* strip_ = nullptr;
  lv_obj_t* content_root_ = nullptr;
  lv_obj_t* lbl_host_ = nullptr;
  lv_obj_t* lbl_wifi_ = nullptr;
  lv_obj_t* lbl_sk_ = nullptr;
  lv_obj_t* lbl_n2k_ = nullptr;
  lv_obj_t* lbl_sys_ = nullptr;
  bool visible_ = true;
};

StatusOverlay& overlay();

}  // namespace jlp
