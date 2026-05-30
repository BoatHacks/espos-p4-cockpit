#pragma once

#include "lvgl.h"
#include <string>

namespace jlp {

enum class ApplySource { Boot, BootStore, BootDefault, PostLayout, BootFetched };

struct ApplyResult {
  bool ok;
  std::string err;
  std::string name;       // root "name" field on success
  unsigned screens;
  unsigned widgets;
  std::string warning;    // non-fatal note, e.g. persistence failed
};

class LayoutManager {
 public:
  // `parent` is where the screens/tabview goes. Set once at boot.
  void init(lv_obj_t* parent);

  // Parses, validates, builds, and (on success) swaps in the new
  // layout. On failure, the current layout stays up.
  ApplyResult apply(const std::string& json, ApplySource src);

  const std::string& active_name() const { return active_name_; }
  ApplySource active_source() const { return active_source_; }

 private:
  lv_obj_t* parent_ = nullptr;
  lv_obj_t* current_root_ = nullptr;
  std::string active_name_;
  ApplySource active_source_ = ApplySource::Boot;
};

LayoutManager& layout_manager();

}  // namespace jlp
