#pragma once

#include <umbrielfx/render/decoration.h>

struct wlr_renderer;
namespace umbriel {
  struct DecorationShaderConfig;
  [[nodiscard]] fx_decoration_shader* decorationShader(const DecorationShaderConfig& settings);
  [[nodiscard]] fx_decoration_parameters decorationParameters(
      const DecorationShaderConfig& settings, float padding, float coordinateScale, bool suppressLight
  );
} // namespace umbriel
