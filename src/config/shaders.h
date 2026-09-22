#pragma once
#include "config/config.h"

#include <array>
#include <optional>
#include <string_view>

namespace umbriel {
  class Section;
  inline constexpr std::array<std::string_view, 4> kBuiltinShaders{"grayscale", "invert", "saturation", "temperature"};
  void readShaders(Section& root, Config& loaded);
  // Reads a shader file and registers it with the config watcher.
  std::optional<AnimationShaderSource> readShaderSource(Section& section);
  std::optional<AnimationShaderSource> builtinShader(std::string_view name, double amount = 1.5, int kelvin = 4000);
} // namespace umbriel
