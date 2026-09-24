#include "scene/postprocess.h"

#include "config/shaders.h"
#include "core/log.h"

#include <algorithm>
#include <memory>
#include <vector>
extern "C" {
#include <umbrielfx/render/postprocess.h>
}

namespace umbriel {
  namespace {
    struct Entry {
      Config::Shaders::Preset preset;
      std::shared_ptr<fx_postprocess_chain> chain;
    };
    std::vector<Entry> cache;
    wlr_renderer* cachedRenderer = nullptr;
    ShaderSelection globalSelection;
    constexpr Logger log("shaders");
  } // namespace
  void preparePostprocessShaders(wlr_renderer* renderer) {
    auto previous = std::move(cache);
    cache.clear();
    auto presets = config().shaders.presets;
    for (const auto name : kBuiltinShaders) {
      if (std::ranges::any_of(presets, [&](const auto& preset) { return preset.name == name; }))
        continue;
      Config::Shaders::Preset preset;
      preset.name = name;
      preset.scope = "output";
      preset.passes.push_back({builtinShader(name), false});
      presets.push_back(std::move(preset));
    }
    for (auto& preset : presets) {
      std::shared_ptr<fx_postprocess_chain> chain;
      bool found = false;
      if (renderer == cachedRenderer)
        for (const auto& old : previous)
          if (old.preset.passes == preset.passes) {
            chain = old.chain;
            found = true;
            break;
          }
      if (!found && !preset.passes.empty() && preset.passes.size() <= FX_POSTPROCESS_MAX_PASSES) {
        std::vector<fx_postprocess_source> sources;
        std::vector<std::string> labels;
        labels.reserve(preset.passes.size());
        sources.reserve(preset.passes.size());
        for (const auto& pass : preset.passes)
          labels.push_back(pass.source ? pass.source->file.string() : preset.name);
        for (size_t i = 0; i < preset.passes.size(); ++i) {
          const auto& pass = preset.passes[i];
          if (!pass.source) {
            sources.clear();
            break;
          }
          sources.push_back({pass.source->code.c_str(), labels[i].c_str(), pass.buffer});
        }
        if (!sources.empty())
          chain = {fx_postprocess_chain_create(renderer, sources.data(), sources.size()), fx_postprocess_chain_unref};
        else
          log.warn("Preset '{}' has a missing pass; using ordinary rendering", preset.name);
      }
      cache.push_back({std::move(preset), std::move(chain)});
    }
    cachedRenderer = renderer;
  }
  void clearPostprocessShaders() {
    cache.clear();
    cachedRenderer = nullptr;
  }
  fx_postprocess_chain* postprocessShader(std::string_view name) {
    if (!config().shaders.enabled || name.empty() || name == "off")
      return nullptr;
    for (const auto& entry : cache)
      if (entry.preset.name == name)
        return entry.chain.get();
    return nullptr;
  }
  const Config::Shaders::Preset* postprocessPreset(std::string_view name) {
    for (const auto& entry : cache)
      if (entry.preset.name == name)
        return &entry.preset;
    return nullptr;
  }
  void applyPostprocessShader(wlr_scene_rect* rect, fx_postprocess_chain* chain, std::string_view presetName) {
    wlr_scene_rect_set_postprocess(rect, chain);
    const auto* preset = postprocessPreset(presetName);
    if (chain != nullptr && preset != nullptr && preset->palette) {
      const auto palette = shaderPalette(config().colors);
      wlr_scene_rect_set_palette(rect, palette.data(), kShaderPaletteCount);
    } else {
      wlr_scene_rect_set_palette(rect, nullptr, 0);
    }
  }
  std::string_view selectedShader(const ShaderSelection& selection, std::string_view fallback) {
    if (!selection.enabled)
      return "off";
    if (selection.preset && postprocessPreset(*selection.preset) != nullptr)
      return *selection.preset;
    return fallback;
  }
  bool cycleShader(
      ShaderSelection& selection, std::string_view scope, std::string_view poolName, std::string_view fallback
  ) {
    if (!poolName.empty()) {
      const auto* pool = shaderPool(poolName, scope);
      if (!pool)
        return false;
      const auto current = std::ranges::find(pool->presets, selectedShader(selection, fallback));
      selection.preset = current == pool->presets.end() || std::next(current) == pool->presets.end()
          ? pool->presets.front()
          : *std::next(current);
      selection.enabled = true;
      return true;
    }
    std::vector<std::string> names;
    const bool category = scope == "cursor" || scope == "screen";
    const std::string prefix = std::string(scope) + ".";
    for (const auto& entry : cache)
      if (category ? entry.preset.scope == "global" && entry.preset.name.starts_with(prefix)
                   : entry.preset.scope == scope)
        names.push_back(entry.preset.name);
    std::ranges::sort(names);
    const auto current = selection.preset ? std::ranges::find(names, *selection.preset) : names.end();
    selection.enabled = true;
    if (current == names.end())
      selection.preset = names.empty() ? std::nullopt : std::optional(names.front());
    else if (std::next(current) == names.end())
      selection.preset.reset();
    else
      selection.preset = *std::next(current);
    return true;
  }
  ShaderSelection& globalShaderSelection() { return globalSelection; }
} // namespace umbriel
