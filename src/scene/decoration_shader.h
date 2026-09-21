#pragma once

struct wlr_renderer;
struct fx_decoration_shader;
namespace umbriel {
  struct DecorationShaderConfig;
  void prepareDecorationShaders(wlr_renderer* renderer);
  void clearDecorationShaderCache();
  [[nodiscard]] fx_decoration_shader* decorationShader(const DecorationShaderConfig& settings);
} // namespace umbriel
