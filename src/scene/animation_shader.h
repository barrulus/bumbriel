#pragma once

#include "config/config.h"
#include "core/animation.h"

struct wlr_scene_node;
struct wlr_renderer;
struct fx_animation_shader;
struct fx_animation_parameters;

namespace umbriel {
  // Stable inner-to-outer composition order for effects sharing a target.
  enum class AnimationEvent : unsigned {
    DimUnfocused,
    Border,
    WindowsResize,
    WindowsMove,
    WindowsIn,
    WindowsOut,
    Scratchpad,
    Layers,
    Workspaces,
    Overview,
    InteractiveMove
  };

  [[nodiscard]] fx_animation_shader* interactivePhysicsShader(wlr_renderer* renderer);
  [[nodiscard]] fx_animation_shader* lifecycleShader(wlr_renderer* renderer, AnimationEvent event);
  void prepareAnimationShaders(wlr_renderer* renderer);
  void clearAnimationShaderCache();
  fx_animation_parameters animationParameters(const AnimatedValue& value, float direction = 0.0F);
  fx_animation_parameters animationParameters(const AnimatedColor& value, float direction);
} // namespace umbriel
