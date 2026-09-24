#pragma once
#include "config/config.h"

#include <optional>
#include <string>
#include <string_view>

struct wlr_renderer;
struct wlr_scene_rect;
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
  // Keep each marker's palette tied to its preset, including when the chain is shared or disabled.
  void applyPostprocessShader(wlr_scene_rect* rect, fx_postprocess_chain* chain, std::string_view presetName);
  std::string_view selectedShader(const ShaderSelection& selection, std::string_view fallback);
  bool cycleShader(
      ShaderSelection& selection, std::string_view scope, std::string_view pool = {}, std::string_view fallback = {}
  );
  ShaderSelection& globalShaderSelection();
} // namespace umbriel
