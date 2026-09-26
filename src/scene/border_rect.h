#pragma once

#include "view/border_ring.h"

#include <array>

struct wlr_scene_border;

namespace umbriel {

  struct BorderSnapshot {
    wlr_scene_border* node = nullptr;
    std::array<float, 4> innerColor{};
    std::array<float, 4> outerColor{};
    // Unscaled ring the snapshot was taken with, so a closing shrink collapses what was drawn.
    int innerWidth = 0;
    int outerWidth = 0;
    int cornerRadius = 0;
    // Effect padding the ring was drawn with.
    int padding = 0;
  };

  // Position and size the single-pass border relative to the content origin.
  // The render margin belongs only to raster coverage; widths remain logical.
  void applyBorderGeometry(wlr_scene_border* border, const BorderRing& ring, int innerWidth, int outerWidth);

} // namespace umbriel
