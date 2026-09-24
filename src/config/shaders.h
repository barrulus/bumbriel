#pragma once
#include "config/config.h"

#include <array>
#include <optional>
#include <string_view>

namespace umbriel {
  class Section;
  inline constexpr std::array<std::string_view, 4> kBuiltinShaders{"grayscale", "invert", "saturation", "temperature"};
  void readDecorationShader(Section& section, DecorationShaderConfig& target);
  const Config::Shaders::Pool* shaderPool(std::string_view name, std::string_view scope);
  const DecorationShaderConfig* borderPreset(std::string_view name);
  // The chromatic palette entries only; the greys and darks would mud a cycle.
  inline constexpr int kShaderPaletteCount = 4;
  std::array<float, kShaderPaletteCount * 4> shaderPalette(const Config::Colors& colors);
  void readShaders(Section& root, Config& loaded);
  // Reads a shader file and registers it with the config watcher.
  std::optional<AnimationShaderSource> readShaderSource(Section& section);
  std::optional<AnimationShaderSource> builtinShader(std::string_view name, double amount = 1.5, int kelvin = 4000);
} // namespace umbriel
