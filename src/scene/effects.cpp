#include "scene/effects.h"

#include "config/shaders.h"
#include "config/store.h"

#include <algorithm>
#include <type_traits>

extern "C" {
#include <umbrielfx/render/animation.h>
#include <umbrielfx/render/decoration.h>
#include <umbrielfx/render/postprocess.h>
}

namespace umbriel {
  namespace {
    struct CacheEntry {
      EffectScope scope;
      std::vector<EffectPass> passes;
      std::shared_ptr<PreparedEffect> programs;
    };
    wlr_renderer* cachedRenderer = nullptr;
    std::vector<CacheEntry> cache;
    using PreparedLibrary =
        std::map<std::string, std::array<std::shared_ptr<const PreparedEffect>, kEffectScopeCount>, std::less<>>;
    PreparedLibrary active;
    std::vector<EffectEvent*> events;
    std::optional<bool> systemOverride;

    std::vector<fx_shader_param> parameters(const EffectPass& pass) {
      std::vector<fx_shader_param> result;
      for (const auto& [name, value] : pass.params) {
        fx_shader_param param{};
        param.name = name.c_str();
        param.length = 1;
        std::visit(
            [&](const auto& value) {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<T, bool>) {
                param.type = FX_PARAM_BOOL;
                param.integers[0] = value;
              } else if constexpr (std::is_same_v<T, int32_t>) {
                param.type = FX_PARAM_INT;
                param.integers[0] = value;
              } else if constexpr (std::is_same_v<T, float>) {
                param.type = FX_PARAM_FLOAT;
                param.numbers[0] = value;
              } else {
                param.length = value.size();
                const bool integers = std::ranges::all_of(value, [](const auto& component) {
                  return std::holds_alternative<int32_t>(component);
                });
                param.type = integers ? FX_PARAM_INT : FX_PARAM_FLOAT;
                for (size_t i = 0; i < value.size(); ++i)
                  if (integers)
                    param.integers[i] = std::get<int32_t>(value[i]);
                  else
                    param.numbers[i] = std::visit([](auto number) { return static_cast<float>(number); }, value[i]);
              }
            },
            value
        );
        result.push_back(param);
      }
      return result;
    }

    EffectScope contract(EffectScope scope) {
      if (scope == EffectScope::BorderOuter)
        return scope;
      return persistentEffect(scope) ? EffectScope::Content : EffectScope::Open;
    }

    std::shared_ptr<PreparedEffect> compile(wlr_renderer* renderer, EffectScope scope, const EffectPipeline& pipeline) {
      auto effect = std::make_shared<PreparedEffect>();
      const bool outer = scope == EffectScope::BorderOuter;
      const bool postprocess = persistentEffect(scope);
      std::vector<fx_postprocess_source> sources;
      std::vector<std::vector<fx_shader_param>> params;
      params.reserve(pipeline.passes.size());
      for (size_t i = 0; i < pipeline.passes.size(); ++i) {
        const auto& pass = pipeline.passes[i];
        params.push_back(pass.builtin.empty() ? parameters(pass) : std::vector<fx_shader_param>{});
        const auto& values = params.back();
        if (outer && i == 0) {
          effect->decoration = {
              fx_decoration_shader_create(renderer, pass.source.code.c_str(), pass.source.file.c_str()),
              fx_decoration_shader_unref
          };
          if (!effect->decoration
              || !fx_decoration_shader_set_params(effect->decoration.get(), values.data(), values.size()))
            return nullptr;
        } else if (postprocess) {
          sources.push_back(
              {pass.source.code.c_str(), pass.source.file.c_str(), pass.buffer, values.data(), values.size()}
          );
        } else {
          std::shared_ptr<fx_animation_shader> shader{
              fx_animation_shader_create(renderer, pass.source.code.c_str(), pass.source.file.c_str()),
              fx_animation_shader_unref
          };
          if (!shader || !fx_animation_shader_set_params(shader.get(), values.data(), values.size()))
            return nullptr;
          effect->animation.push_back(std::move(shader));
        }
      }
      if (!sources.empty()) {
        effect->postprocess = {
            fx_postprocess_chain_create(renderer, sources.data(), sources.size()), fx_postprocess_chain_unref
        };
        if (!effect->postprocess)
          return nullptr;
      }
      return effect;
    }
  } // namespace

  bool effectsEnabled() { return systemOverride.value_or(config().effectPolicy.enabled); }

  bool setEffectSystem(std::string_view operation, wlr_renderer* renderer, std::vector<ConfigDiagnostic>& diagnostics) {
    const auto previous = systemOverride;
    if (operation == "default")
      systemOverride.reset();
    else if (operation == "toggle")
      systemOverride = !effectsEnabled();
    else
      systemOverride = operation == "on";
    if (!prepareEffects(renderer, config(), diagnostics)) {
      systemOverride = previous;
      return false;
    }
    return true;
  }

  bool nativeEffectEnabled(EffectScope scope, bool layer) {
    const auto& animation = config().animation;
    if (!animation.enabled)
      return false;
    switch (scope) {
    case EffectScope::Open:
      return layer ? animation.layers.enabled : animation.windowsIn.enabled;
    case EffectScope::Close:
      return layer ? animation.layers.enabled : animation.windowsOut.enabled;
    case EffectScope::Move:
    case EffectScope::Resize:
    case EffectScope::Drag:
      return animation.windowsMove.enabled;
    case EffectScope::Focus:
      return animation.dimUnfocused.enabled;
    case EffectScope::BorderFocus:
      return animation.border.enabled;
    case EffectScope::Scratchpad:
    case EffectScope::Backdrop:
      return animation.scratchpad.enabled;
    case EffectScope::Workspace:
      return animation.workspaces.enabled;
    case EffectScope::Overview:
      return animation.overview.enabled;
    default:
      return true;
    }
  }

  EffectEvent::~EffectEvent() { std::erase(events, this); }

  void EffectEvent::rebuild(wlr_renderer* renderer) {
    if (m_programs && m_captured.pipeline)
      m_programs = compile(renderer, m_scope, *m_captured.pipeline);
  }

  void EffectEvent::reset() {
    std::erase(events, this);
    m_started = false;
    m_transition = 0;
    m_programs.reset();
    m_captured = {};
    m_paletteCount = 0;
  }

  void EffectEvent::update(
      wlr_scene_node* node, unsigned slot, const ResolvedEffect& next, EffectScope scope,
      const fx_animation_parameters& parameters, bool running, bool enabled, fx_animation_shader* fallback
  ) {
    if (!node)
      return;
    if (!running) {
      reset();
      wlr_scene_node_set_animation(node, slot, nullptr, nullptr);
      return;
    }
    if (!m_started || m_transition != parameters.transition_id) {
      m_started = true;
      m_transition = parameters.transition_id;
      m_captured = next;
      m_generation = configStore().generation();
      m_scope = scope;
      m_programs = enabled ? preparedEffect(next, scope) : nullptr;
      if (m_programs && !std::ranges::contains(events, this))
        events.push_back(this);
      m_paletteCount = next.pipeline && next.pipeline->palette ? kShaderPaletteCount : 0;
      std::ranges::copy(shaderPalette(config().colors), m_palette.begin());
    }
    if ((!enabled || next.runtimeDisabled) && m_programs) {
      std::erase(events, this);
      m_programs.reset();
      wlr_scene_node_set_animation(node, slot, nullptr, nullptr);
    }
    auto capturedParameters = parameters;
    capturedParameters.palette_count = m_paletteCount;
    std::ranges::copy(m_palette, capturedParameters.palette);
    if (custom()) {
      std::array<fx_animation_shader*, FX_ANIMATION_MAX_PASSES> shaders{};
      std::ranges::transform(m_programs->animation, shaders.begin(), [](const auto& shader) { return shader.get(); });
      wlr_scene_node_set_animation_pipeline(
          node, slot, shaders.data(), m_programs->animation.size(), &capturedParameters
      );
    } else
      wlr_scene_node_set_animation(node, slot, fallback, &parameters);
  }

  bool prepareEffects(wlr_renderer* renderer, const Config& config, std::vector<ConfigDiagnostic>& diagnostics) {
    const bool rendererChanged = cachedRenderer != renderer;
    if (rendererChanged) {
      clearEffects();
      cachedRenderer = renderer;
    }
    PreparedLibrary candidate;
    if (!systemOverride.value_or(config.effectPolicy.enabled)) {
      active.clear();
      return true;
    }
    bool valid = true;
    std::vector<size_t> used;
    for (const auto& [name, definition] : config.effects) {
      if (definition.choose)
        continue;
      for (size_t i = 0; i < definition.scopes.size(); ++i) {
        const auto scope = static_cast<EffectScope>(i);
        const auto& pipeline = definition.scopes[i];
        if (!pipeline || !pipeline->enabled || scope == EffectScope::Drag)
          continue;
        auto passes = pipeline->passes;
        for (auto& pass : passes) {
          pass.origin = {};
          pass.source.file.clear();
        }
        auto found = std::ranges::find_if(cache, [&](const auto& entry) {
          return entry.scope == contract(scope) && entry.passes == passes;
        });
        if (found == cache.end()) {
          cache.push_back({contract(scope), std::move(passes), compile(renderer, scope, *pipeline)});
          found = std::prev(cache.end());
        }
        used.push_back(static_cast<size_t>(found - cache.begin()));
        if (!found->programs) {
          diagnostics.push_back(
              {ConfigDiagnostic::Severity::Error,
               "effect '"
                   + name
                   + "' "
                   + std::string(kEffectScopeNames[i])
                   + " failed GPU compilation or uniform binding",
               pipeline->origin.file, pipeline->origin.line, pipeline->origin.column}
          );
          valid = false;
        }
        candidate[name][i] = found->programs;
      }
    }
    if (valid) {
      active = std::move(candidate);
      if (rendererChanged)
        for (auto* event : events)
          event->rebuild(renderer);
    }
    size_t index = 0;
    std::erase_if(cache, [&](const auto& entry) {
      return !std::ranges::contains(used, index++) && (!entry.programs || entry.programs.use_count() == 1);
    });
    return valid;
  }

  void clearEffects() {
    active.clear();
    cache.clear();
    cachedRenderer = nullptr;
  }

  fx_scene_postprocess outputEffect(const ResolvedEffect& effect, EffectScope scope) {
    fx_scene_postprocess result{};
    const auto program = preparedEffect(effect, scope);
    if (!program || !effect.pipeline)
      return result;
    const auto& settings = *effect.pipeline;
    result.chain = program->postprocess.get();
    result.speed = settings.speed;
    result.frozen = !settings.animated || settings.speed == 0;
    result.cursor_radius = settings.cursorRadius;
    if (settings.palette) {
      std::ranges::copy(shaderPalette(config().colors), result.palette);
      result.palette_count = kShaderPaletteCount;
    }
    return result;
  }

  void applyEffect(wlr_scene_rect* rect, const ResolvedEffect& effect, EffectScope scope) {
    const auto program = preparedEffect(effect, scope);
    wlr_scene_rect_set_postprocess(rect, program ? program->postprocess.get() : nullptr);
    if (!program || !effect.pipeline)
      return;
    const auto& settings = *effect.pipeline;
    const auto palette = shaderPalette(config().colors);
    wlr_scene_rect_set_palette(rect, palette.data(), settings.palette ? kShaderPaletteCount : 0);
    wlr_scene_rect_set_postprocess_time(rect, settings.animated, settings.speed);
  }

  std::shared_ptr<const PreparedEffect> preparedEffect(std::string_view name, EffectScope scope) {
    const auto found = active.find(name);
    return found == active.end() ? nullptr : found->second[static_cast<size_t>(scope)];
  }

  std::shared_ptr<const PreparedEffect> preparedEffect(const ResolvedEffect& effect, EffectScope scope) {
    if (!effect.pipeline || !effect.pipeline->enabled)
      return nullptr;
    return preparedEffect(effect.source.effect, scope);
  }
} // namespace umbriel
