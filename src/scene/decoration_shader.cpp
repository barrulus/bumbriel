#include "scene/decoration_shader.h"

#include "config/config.h"

#include <memory>
#include <vector>

namespace umbriel {
  namespace {
    struct Entry {
      AnimationShaderSource source;
      std::shared_ptr<fx_decoration_shader> program;
    };
    std::vector<Entry> cache;
    wlr_renderer* cachedRenderer = nullptr;
  } // namespace
  void prepareDecorationShaders(wlr_renderer* renderer) {
    auto previous = std::move(cache);
    cache.clear();
    const auto prepare = [&](const DecorationShaderConfig& settings) {
      if (!settings.enabled || !settings.shader)
        return;
      for (const auto& entry : cache)
        if (entry.source == *settings.shader)
          return;
      if (cachedRenderer == renderer) {
        for (const auto& entry : previous) {
          if (entry.source == *settings.shader) {
            cache.push_back(entry);
            return;
          }
        }
      }
      const auto& source = *settings.shader;
      cache.push_back(
          {source,
           {fx_decoration_shader_create(renderer, source.code.c_str(), source.file.c_str()),
            fx_decoration_shader_unref}}
      );
    };
    prepare(config().appearance.borderShader);
    for (const auto& rule : config().windowRules)
      if (rule.borderShader)
        prepare(*rule.borderShader);
    for (const auto& preset : config().shaders.borders)
      prepare(preset.settings);
    cachedRenderer = renderer;
  }
  void clearDecorationShaderCache() {
    cache.clear();
    cachedRenderer = nullptr;
  }
  fx_decoration_shader* decorationShader(const DecorationShaderConfig& settings) {
    if (!settings.enabled || !settings.shader)
      return nullptr;
    for (const auto& entry : cache)
      if (entry.source == *settings.shader)
        return entry.program.get();
    return nullptr;
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
