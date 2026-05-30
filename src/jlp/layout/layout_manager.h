#pragma once

#include "lvgl.h"
#include <string>

namespace jlp {

enum class ApplySource { Boot, BootStore, BootDefault, PostLayout, BootFetched };

struct ApplyResult {
  bool ok;
  std::string err;
  std::string name;  // root "name" field on success
  unsigned screens;
  unsigned widgets;
};

class LayoutManager {
 public:
  // `parent` is where the screens/tabview goes. Set once at boot.
  void init(lv_obj_t* parent);

  // Parses, validates, builds, and (on success) swaps in the new
  // layout. On failure, the current layout stays up. v1 is non-atomic:
  // the swap tears down the old tree before building the new one. The
  // detached-parent staging comes in step 5.
  ApplyResult apply(const std::string& json, ApplySource src);

 private:
  lv_obj_t* parent_ = nullptr;
  lv_obj_t* current_root_ = nullptr;
};

LayoutManager& layout_manager();

}  // namespace jlp
