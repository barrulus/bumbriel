#include "config/shaders.h"

#include <algorithm>

namespace umbriel {
  std::array<float, kShaderPaletteCount * 4> shaderPalette(const Config::Colors& colors) {
    const std::array<const std::array<float, 4>*, kShaderPaletteCount> ramp{
        &colors.accentPrimary, &colors.accentSecondary, &colors.warning, &colors.error
    };
    std::array<float, kShaderPaletteCount * 4> out{};
    for (size_t entry = 0; entry < ramp.size(); ++entry) {
      std::ranges::copy(*ramp[entry], out.begin() + static_cast<std::ptrdiff_t>(entry * 4));
    }
    return out;
  }

} // namespace umbriel
