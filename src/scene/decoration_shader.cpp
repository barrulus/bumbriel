#include "scene/decoration_shader.h"

#include "config/config.h"
#include "scene/effects.h"

namespace umbriel {
  fx_decoration_shader* decorationShader(const DecorationShaderConfig& settings) {
    if (!settings.enabled)
      return nullptr;
    const auto program = preparedEffect(settings.effect, EffectScope::BorderOuter);
    return program ? program->decoration.get() : nullptr;
  }
  fx_decoration_parameters decorationParameters(
      const DecorationShaderConfig& settings, float padding, float coordinateScale, bool suppressLight
  ) {
    return {
        .speed = static_cast<float>(settings.speed),
        .padding = padding,
        .coordinate_scale = coordinateScale,
        .animated = settings.animated,
        .light = {
            .enabled = settings.light.enabled && !suppressLight,
            .spread = static_cast<float>(settings.light.spread),
            .intensity = static_cast<float>(settings.light.intensity),
            .threshold = static_cast<float>(settings.light.threshold)
        },
    };
  }
} // namespace umbriel
