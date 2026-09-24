#pragma once
#include "config/config.h"

#include <array>

namespace umbriel {
  inline constexpr int kShaderPaletteCount = 4;
  std::array<float, kShaderPaletteCount * 4> shaderPalette(const Config::Colors& colors);
} // namespace umbriel
