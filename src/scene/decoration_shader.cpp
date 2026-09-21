#include "scene/decoration_shader.h"

#include "config/config.h"

#include <memory>
#include <vector>
extern "C" {
#include <umbrielfx/render/decoration.h>
}

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
} // namespace umbriel
