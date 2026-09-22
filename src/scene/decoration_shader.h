#pragma once

#include <umbrielfx/render/decoration.h>

struct wlr_renderer;
namespace umbriel {
  struct DecorationShaderConfig;
  void prepareDecorationShaders(wlr_renderer* renderer);
  void clearDecorationShaderCache();
  [[nodiscard]] fx_decoration_shader* decorationShader(const DecorationShaderConfig& settings);
  [[nodiscard]] fx_decoration_parameters decorationParameters(
      const DecorationShaderConfig& settings, float padding, float coordinateScale, bool suppressLight
  );
} // namespace umbriel
