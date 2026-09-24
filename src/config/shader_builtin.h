#pragma once
#include "config/animation_shader.h"

#include <array>
#include <string_view>

namespace umbriel {
  inline constexpr std::array<std::string_view, 4> kBuiltinShaders{"grayscale", "invert", "saturation", "temperature"};
  std::optional<AnimationShaderSource> builtinShader(std::string_view name, double amount = 1.5, int kelvin = 4000);
} // namespace umbriel
