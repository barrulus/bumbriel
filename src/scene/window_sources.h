#pragma once

#include "core/animation.h"
#include "scene/surface_shadow.h"
#include "wlr.h"

#include <vector>

extern "C" {
#include "../../umbrielfx/internal/types/scene_source.h"
}

namespace umbriel {
  // A frame-local borrow, looked up by native close ID on every use. Captured
  // paired buffers retain their own storage and never retain this descriptor.
  struct WindowCloseSource {
    wlr_scene_tree* tree = nullptr;
    wlr_scene_tree* content = nullptr;
    wlr_scene_shadow* shadow = nullptr;
    wlr_box box{};
    int borderWidth = 0;
    int cornerRadius = 0;
    AnimatedValue lifecycle;
    std::vector<fx_scene_source_node_override> overrides;
  };
} // namespace umbriel
