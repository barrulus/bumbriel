#pragma once
#include "config/config.h"

namespace umbriel {
  class Section;
  void readShaders(Section& root, Config& loaded);
  std::optional<AnimationShaderSource> builtinShader(std::string_view name, double amount = 1.5, int kelvin = 4000);
} // namespace umbriel
