#pragma once
#include "config/config.h"

#include <optional>
#include <string>
#include <string_view>

struct wlr_renderer;
struct fx_postprocess_chain;
namespace umbriel {
  struct ShaderSelection {
    std::optional<std::string> preset;
    bool enabled = true;
  };
  void preparePostprocessShaders(wlr_renderer* renderer);
  void clearPostprocessShaders();
  fx_postprocess_chain* postprocessShader(std::string_view name);
  const Config::Shaders::Preset* postprocessPreset(std::string_view name);
  std::string_view selectedShader(const ShaderSelection& selection, std::string_view fallback);
  void cycleShader(ShaderSelection& selection, std::string_view scope);
  ShaderSelection& globalShaderSelection();
} // namespace umbriel
