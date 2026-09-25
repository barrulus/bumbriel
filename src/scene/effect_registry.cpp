#include "scene/effect_registry.h"

#include "config/config.h"
#include "core/log.h"

#include <algorithm>

extern "C" {
#include <umbrielfx/render/effect.h>
}

namespace umbriel {
  namespace {
    constexpr Logger kLog("effects");
    EffectRegistry* s_registry = nullptr;

    // Entering transitions fade in with progress and leaving ones fade out. Only alpha changes, uniformly, so the
    // window's shape and its analytic shadow are unaffected.
    constexpr const char* kBuiltinFade = R"(vec4 animation(vec2 uv) {
    float alpha = umbriel_direction < 0.0 ? 1.0 - umbriel_clamped_progress : umbriel_clamped_progress;
    return umbriel_sample(uv) * alpha;
})";

    // The config event an animation slot binds through `effect =`, or null for slots without one.
    struct EventBinding {
      const std::string* effect = nullptr;
      bool enabled = false;
      const char* name = "";
    };
    EventBinding eventBinding(const Config::Animation& settings, AnimationEvent event) {
      switch (event) {
      case AnimationEvent::DimUnfocused:
        return {&settings.dimUnfocused.effect, settings.dimUnfocused.enabled, "dim_unfocused"};
      case AnimationEvent::Border:
        return {&settings.border.effect, settings.border.enabled, "border"};
      case AnimationEvent::WindowsMove:
        return {&settings.windowsMove.effect, settings.windowsMove.enabled, "windows_move"};
      case AnimationEvent::WindowsIn:
        return {&settings.windowsIn.effect, settings.windowsIn.enabled, "windows_in"};
      case AnimationEvent::WindowsOut:
        return {&settings.windowsOut.effect, settings.windowsOut.enabled, "windows_out"};
      case AnimationEvent::Scratchpad:
        return {&settings.scratchpad.effect, settings.scratchpad.enabled, "scratchpad"};
      case AnimationEvent::Layers:
        return {&settings.layers.effect, settings.layers.enabled, "layers"};
      case AnimationEvent::Workspaces:
        return {&settings.workspaces.effect, settings.workspaces.enabled, "workspaces"};
      case AnimationEvent::Overview:
        return {&settings.overview.effect, settings.overview.enabled, "overview"};
      case AnimationEvent::Window:
      case AnimationEvent::Overlay:
      case AnimationEvent::BorderEffect:
      case AnimationEvent::Drag:
        return {};
      }
      return {};
    }

    fx_effect_kind toFxKind(EffectKind kind) {
      switch (kind) {
      case EffectKind::Animation:
        return FX_EFFECT_ANIMATION;
      case EffectKind::Border:
        return FX_EFFECT_BORDER;
      case EffectKind::Window:
        return FX_EFFECT_WINDOW;
      case EffectKind::Screen:
        return FX_EFFECT_SCREEN;
      case EffectKind::Cursor:
        return FX_EFFECT_CURSOR;
      }
      return FX_EFFECT_ANIMATION;
    }
  } // namespace

  EffectRegistry& effectRegistry() { return *s_registry; }

  EffectRegistry::EffectRegistry(Server& server) : m_server(&server) { s_registry = this; }

  EffectRegistry::~EffectRegistry() {
    clear();
    if (s_registry == this) {
      s_registry = nullptr;
    }
  }

  void EffectRegistry::clear() {
    m_programs.clear();
    m_builtinFade.reset();
    m_renderer = nullptr;
  }

  void EffectRegistry::referencedNames(std::vector<std::string>& names) const {
    const Config& settings = config();
    const auto add = [&](std::string_view name) {
      if (!name.empty() && name != kEffectOff && std::ranges::find(names, name) == names.end()) {
        names.emplace_back(name);
      }
    };
    add(settings.effects.border);
    add(settings.effects.window);
    add(settings.effects.screen);
    add(settings.effects.cursor);
    for (const WindowRule& rule : settings.windowRules) {
      add(rule.borderEffect.value_or(""));
      add(rule.windowEffect.value_or(""));
    }
    for (const OutputRule& rule : settings.outputs) {
      add(rule.screenEffect.value_or(""));
    }
    for (unsigned slot = 0; slot < FX_ANIMATION_SLOTS; ++slot) {
      const EventBinding binding = eventBinding(settings.animation, static_cast<AnimationEvent>(slot));
      if (binding.effect != nullptr) {
        add(*binding.effect);
      }
    }
    // A referenced border preset pulls its overlay in. Overlays name window presets, which carry none of their own.
    for (const EffectPreset& preset : settings.effects.presets) {
      if (!preset.overlay.empty() && std::ranges::find(names, preset.name) != names.end()) {
        add(preset.overlay);
      }
    }
  }

  void EffectRegistry::compile(const EffectPreset& preset) {
    Entry& entry = m_programs[preset.name];
    if (entry.shader != nullptr && entry.kind == preset.kind && entry.code == preset.shader.code) {
      return;
    }
    entry.kind = preset.kind;
    entry.code = preset.shader.code;
    entry.shader.reset();
    if (preset.inert()) {
      return;
    }
    const std::string label =
        preset.shader.file.empty() ? "effects.preset." + preset.name : preset.shader.file.string();
    entry.shader = {
        fx_effect_shader_create(m_renderer, toFxKind(preset.kind), preset.shader.code.c_str(), label.c_str()),
        fx_effect_shader_unref
    };
    if (entry.shader == nullptr) {
      kLog.error(
          "effect preset '{}' ({}) failed to compile; rendering plainly", preset.name, effectKindName(preset.kind)
      );
    }
  }

  void EffectRegistry::prepare(wlr_renderer* renderer) {
    if (renderer != m_renderer) {
      // Programs belong to one GL context. A new renderer starts from nothing.
      m_programs.clear();
      m_builtinFade.reset();
      m_renderer = renderer;
    }
    const Config& settings = config();
    std::vector<std::string> names;
    referencedNames(names);
    std::erase_if(m_programs, [&](const auto& item) { return std::ranges::find(names, item.first) == names.end(); });
    for (const std::string& name : names) {
      if (const EffectPreset* preset = findEffectPreset(settings.effects, name)) {
        compile(*preset);
      }
    }
    // Slide keeps per-buffer alpha: its opacity curve differs from the lifecycle progress.
    const bool fadeNeeded = settings.animation.enabled
        && ((settings.animation.windowsIn.enabled && settings.animation.windowsIn.style != "slide")
            || (settings.animation.windowsOut.enabled && settings.animation.windowsOut.style != "slide"));
    if (fadeNeeded && m_builtinFade == nullptr) {
      m_builtinFade = {
          fx_effect_shader_create(m_renderer, FX_EFFECT_ANIMATION, kBuiltinFade, "animation.builtin_fade"),
          fx_effect_shader_unref
      };
      fx_effect_shader_set_shape_preserving(m_builtinFade.get(), true);
    } else if (!fadeNeeded) {
      m_builtinFade.reset();
    }
  }

  fx_effect_shader* EffectRegistry::preset(std::string_view name, EffectKind kind) const {
    const auto entry = m_programs.find(name);
    return entry != m_programs.end() && entry->second.kind == kind ? entry->second.shader.get() : nullptr;
  }

  const EffectPreset* EffectRegistry::presetConfig(std::string_view name) const {
    return findEffectPreset(config().effects, name);
  }

  fx_effect_shader* EffectRegistry::animationShader(AnimationEvent event) const {
    const Config::Animation& settings = config().animation;
    const EventBinding binding = eventBinding(settings, event);
    if (binding.effect == nullptr || !settings.enabled || !binding.enabled || binding.effect->empty()) {
      return nullptr;
    }
    return preset(*binding.effect, EffectKind::Animation);
  }

  fx_effect_shader* EffectRegistry::lifecycleShader(AnimationEvent event) const {
    if (fx_effect_shader* custom = animationShader(event)) {
      return custom;
    }
    const Config::Animation& settings = config().animation;
    const bool builtin = settings.enabled
        && ((event == AnimationEvent::WindowsIn && settings.windowsIn.enabled && settings.windowsIn.style != "slide")
            || (event == AnimationEvent::WindowsOut
                && settings.windowsOut.enabled
                && settings.windowsOut.style != "slide"));
    return builtin ? m_builtinFade.get() : nullptr;
  }

  void EffectRegistry::fillTimeUniforms(
      fx_animation_parameters& parameters, float seconds, const EffectPreset& preset, const fx_effect_shader* shader
  ) const {
    if (fx_effect_shader_reads(shader, "umbriel_time")) {
      if (fx_uniform* time = fx_parameters_add_uniform(&parameters, "umbriel_time", FX_UNIFORM_FLOAT, 1)) {
        time->floats[0] = seconds;
      }
    }
    if (!preset.palette) {
      return;
    }
    const auto palette = effectPalette(config().colors);
    if (fx_uniform* colors = fx_parameters_add_uniform(&parameters, "umbriel_palette", FX_UNIFORM_VEC4, 4)) {
      for (size_t i = 0; i < palette.size(); ++i) {
        std::ranges::copy(palette[i], &colors->floats[i * 4]);
      }
    }
    if (fx_uniform* count = fx_parameters_add_uniform(&parameters, "umbriel_palette_count", FX_UNIFORM_INT, 1)) {
      count->ints[0] = static_cast<int32_t>(palette.size());
    }
  }

} // namespace umbriel
