#pragma once

#include "lvgl.h"
#include <functional>
#include <set>
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

  // Called after every successful layout swap. The bool argument is
  // true iff the new layout introduced bound paths that weren't in
  // the previous layout — caller can use it to decide whether a WS
  // reconnect (and the brief visible flicker) is worth it.
  //
  // (SensESP only subscribes listeners once at on_connected; new
  // listeners added later are silently ignored until reconnect.
  // So a push that introduces new paths needs a WS restart for those
  // values to flow; a push that only rearranges existing widgets
  // doesn't.)
  // Fires after every successful apply + swap. `new_paths` is the set
  // of bound paths the new layout introduced that weren't already in
  // the previous one (or all paths on first apply).
  // `src` and `all_paths` let the hook re-seed values on a boot apply
  // (BootStore / BootFetched), where no paths are "new" but the quiet
  // ones — e.g. navigation.anchor.maxRadius — still need backfilling
  // because their delta was missed before the WS subscribed.
  using PostSwapHook =
      std::function<void(bool new_paths_introduced,
                         const std::set<std::string>& new_paths,
                         ApplySource src,
                         const std::set<std::string>& all_paths)>;
  void set_post_swap_hook(PostSwapHook h) { post_swap_ = std::move(h); }

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
  PostSwapHook post_swap_;
  // Paths the currently-applied layout binds. Used to detect whether
  // the next swap introduces brand-new paths (which would require a
  // SK WS reconnect for the subscription to include them).
  std::set<std::string> known_paths_;
};

LayoutManager& layout_manager();

}  // namespace jlp
