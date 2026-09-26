#include "scene/animation_shader.h"

#include "scene/effect_registry.h"

#include <algorithm>

extern "C" {
#include <umbrielfx/render/effect.h>
}

namespace umbriel {
  namespace {
    static_assert(static_cast<unsigned>(AnimationEvent::Overview) + 1 == FX_ANIMATION_SLOTS);
    static_assert(static_cast<unsigned>(AnimationEvent::Window) == FX_SLOT_WINDOW);
    static_assert(static_cast<unsigned>(AnimationEvent::BorderEffect) == FX_SLOT_BORDER_EFFECT);
    static_assert(static_cast<unsigned>(AnimationEvent::Drag) == FX_SLOT_DRAG);
    static_assert(static_cast<unsigned>(AnimationEvent::WindowsIn) == FX_SLOT_WINDOWS_IN);
    static_assert(static_cast<unsigned>(AnimationEvent::WindowsOut) == FX_SLOT_WINDOWS_OUT);

    template <typename Value>
    void update(
        wlr_scene_node* node, wlr_renderer* renderer, AnimationEvent event, const Value& value, float progress,
        float direction
    ) {
      if (node == nullptr) {
        return;
      }
      fx_animation_parameters parameters{};
      parameters.progress = progress;
      parameters.linear_progress = static_cast<float>(value.progress());
      parameters.direction = direction;
      parameters.transition_id = value.transitionId();
      std::ranges::copy(value.shaderSeed(), parameters.random_seed);
      EffectRegistry& registry = effectRegistry();
      fx_effect_shader* shader = value.animating() ? lifecycleShader(renderer, event) : nullptr;
      // A preset bound to this event gets the shared uniforms too: umbriel_time (when it reads it) and, for
      // palette = true, the [colors] palette. The built-in fade has no preset and reads neither.
      if (shader != nullptr) {
        if (const EffectPreset* preset = registry.animationPreset(event)) {
          registry.fillTimeUniforms(parameters, registry.clockSeconds(), *preset, shader);
        }
      }
      wlr_scene_node_set_animation(node, static_cast<unsigned>(event), shader, &parameters);
    }
  } // namespace

  fx_effect_shader* animationShader(wlr_renderer* /*renderer*/, AnimationEvent event) {
    return effectRegistry().animationShader(event);
  }

  fx_effect_shader* lifecycleShader(wlr_renderer* /*renderer*/, AnimationEvent event) {
    return effectRegistry().lifecycleShader(event);
  }

  void prepareAnimationShaders(wlr_renderer* renderer) { effectRegistry().prepare(renderer); }

  void clearAnimationShaderCache() { effectRegistry().clear(); }

  void updateAnimationShader(
      wlr_scene_node* node, wlr_renderer* renderer, AnimationEvent event, const AnimatedValue& value, float direction
  ) {
    const double distance = value.target() - value.from();
    const auto progress = static_cast<float>(
        distance != 0.0 ? (value.current() - value.from()) / distance : evaluateCurve(value.curve(), value.progress())
    );
    update(node, renderer, event, value, progress, direction != 0.0F ? direction : (distance < 0.0 ? -1.0F : 1.0F));
  }

  void updateAnimationShader(
      wlr_scene_node* node, wlr_renderer* renderer, AnimationEvent event, const AnimatedColor& value, float direction
  ) {
    update(node, renderer, event, value, static_cast<float>(evaluateCurve(value.curve(), value.progress())), direction);
  }
} // namespace umbriel
