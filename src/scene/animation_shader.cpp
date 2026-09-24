#include "scene/animation_shader.h"

#include "config/config.h"

#include <algorithm>
#include <array>
#include <memory>

extern "C" {
#include <umbrielfx/render/animation.h>
}

namespace umbriel {
  namespace {
    // Entering transitions fade in with progress and leaving ones fade out. Only alpha changes, uniformly, so the
    // window's shape and its analytic shadow are unaffected.
    constexpr const char* kBuiltinFade = R"(vec4 animation(vec2 uv) {
    float alpha = umbriel_direction < 0.0 ? 1.0 - umbriel_clamped_progress : umbriel_clamped_progress;
    return umbriel_sample(uv) * alpha;
})";
    struct BuiltinEntry {
      wlr_renderer* renderer = nullptr;
      std::shared_ptr<fx_animation_shader> shader;
    };
    BuiltinEntry builtinFade;
    BuiltinEntry builtinPhysics;

    fx_animation_shader* builtinFadeShader(wlr_renderer* renderer) {
      if (builtinFade.renderer != renderer) {
        builtinFade.renderer = renderer;
        builtinFade.shader = {
            fx_animation_shader_create(renderer, kBuiltinFade, "animation.builtin_fade"), fx_animation_shader_unref
        };
        fx_animation_shader_set_shape_preserving(builtinFade.shader.get(), true);
      }
      return builtinFade.shader.get();
    }
    static_assert(static_cast<unsigned>(AnimationEvent::InteractiveMove) + 1 == FX_ANIMATION_SLOTS);
    static_assert(static_cast<unsigned>(AnimationEvent::InteractiveMove) == FX_ANIMATION_INTERACTIVE_SLOT);

    template <typename Value> fx_animation_parameters parameters(const Value& value, float progress, float direction) {
      fx_animation_parameters result{};
      result.progress = progress;
      result.linear_progress = static_cast<float>(value.progress());
      result.direction = direction;
      result.transition_id = value.transitionId();
      std::ranges::copy(value.shaderSeed(), result.random_seed);
      return result;
    }
  } // namespace

  fx_animation_shader* interactivePhysicsShader(wlr_renderer* renderer) {
    if (builtinPhysics.renderer != renderer) {
      builtinPhysics.renderer = renderer;
      builtinPhysics.shader = {fx_drag_physics_shader_create(renderer), fx_animation_shader_unref};
    }
    return builtinPhysics.shader.get();
  }

  fx_animation_shader* lifecycleShader(wlr_renderer* renderer, AnimationEvent event) {
    const auto& settings = config().animation;
    const auto& open = settings.windowsIn;
    const auto& close = settings.windowsOut;
    // Slide keeps per-buffer alpha: its opacity curve differs from the lifecycle progress.
    const bool builtin = settings.enabled
        && ((event == AnimationEvent::WindowsIn && open.enabled && open.style != "slide")
            || (event == AnimationEvent::WindowsOut && close.enabled && close.style != "slide"));
    return builtin ? builtinFadeShader(renderer) : nullptr;
  }

  void prepareAnimationShaders(wlr_renderer* renderer) {
    for (unsigned event = 0; event < FX_ANIMATION_SLOTS; ++event) {
      (void)lifecycleShader(renderer, static_cast<AnimationEvent>(event));
    }
  }

  void clearAnimationShaderCache() {
    builtinFade = {};
    builtinPhysics = {};
  }

  fx_animation_parameters animationParameters(const AnimatedValue& value, float direction) {
    const double distance = value.target() - value.from();
    const auto progress = static_cast<float>(
        distance != 0.0 ? (value.current() - value.from()) / distance : evaluateCurve(value.curve(), value.progress())
    );
    return parameters(value, progress, direction != 0.0F ? direction : (distance < 0.0 ? -1.0F : 1.0F));
  }

  fx_animation_parameters animationParameters(const AnimatedColor& value, float direction) {
    return parameters(value, static_cast<float>(evaluateCurve(value.curve(), value.progress())), direction);
  }

} // namespace umbriel
