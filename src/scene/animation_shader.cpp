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
    struct CacheEntry {
      wlr_renderer* renderer = nullptr;
      std::optional<AnimationShaderSource> source;
      std::shared_ptr<fx_animation_shader> shader;
    };
    std::array<CacheEntry, FX_ANIMATION_SLOTS> cache;
    std::optional<std::string> pairSelection;
    bool pairEnabled = true;
    static_assert(static_cast<unsigned>(AnimationEvent::Overview) + 1 == FX_ANIMATION_SLOTS);

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
      wlr_scene_node_set_animation(
          node, static_cast<unsigned>(event), value.animating() ? animationShader(renderer, event) : nullptr,
          &parameters
      );
    }
  } // namespace

  const Config::Animation::Pair* selectedPair() {
    const auto& animation = config().animation;
    auto found = animation.pairs.find(pairSelection.value_or(animation.preset));
    if (found == animation.pairs.end())
      found = animation.pairs.find(animation.preset);
    return found == animation.pairs.end() ? nullptr : &found->second;
  }
  Config::Animation::WindowsIn selectedWindowsIn() {
    const auto* pair = selectedPair();
    auto result = pair != nullptr && pairEnabled ? pair->open : config().animation.windowsIn;
    if (!pairEnabled)
      result.shader.reset();
    return result;
  }
  Config::Animation::WindowsOut selectedWindowsOut() {
    const auto* pair = selectedPair();
    auto result = pair != nullptr && pairEnabled ? pair->close : config().animation.windowsOut;
    if (!pairEnabled)
      result.shader.reset();
    return result;
  }
  bool selectAnimationPair(std::string_view operation) {
    const auto& pairs = config().animation.pairs;
    if (operation == "toggle")
      pairEnabled = !pairEnabled;
    else if (operation == "off")
      pairEnabled = false;
    else if (operation == "on")
      pairEnabled = true;
    else if (operation == "default") {
      pairSelection.reset();
      pairEnabled = true;
    } else if (operation == "cycle") {
      const auto current = pairSelection ? pairs.find(*pairSelection) : pairs.end();
      const auto next = current == pairs.end() ? pairs.begin() : std::next(current);
      pairSelection = next == pairs.end() ? std::nullopt : std::optional(next->first);
      pairEnabled = true;
    } else if (pairs.contains(std::string(operation))) {
      pairSelection = operation;
      pairEnabled = true;
    } else
      return false;
    return true;
  }

  fx_animation_shader* animationShader(wlr_renderer* renderer, AnimationEvent event) {
    const auto& settings = config().animation;
    const auto open = selectedWindowsIn();
    const auto close = selectedWindowsOut();
    const std::optional<AnimationShaderSource>* source = nullptr;
    bool enabled = false;
    const char* label = "animation";
#define EVENT(id, field, name)                                                                                         \
  case AnimationEvent::id:                                                                                             \
    source = &settings.field.shader;                                                                                   \
    enabled = settings.field.enabled;                                                                                  \
    label = "animation." name;                                                                                         \
    break
    switch (event) {
      EVENT(DimUnfocused, dimUnfocused, "dim_unfocused");
      EVENT(Border, border, "border");
      EVENT(WindowsMove, windowsMove, "windows_move");
    case AnimationEvent::WindowsIn:
      source = &open.shader;
      enabled = open.enabled;
      label = "animation.windows_in";
      break;
    case AnimationEvent::WindowsOut:
      source = &close.shader;
      enabled = close.enabled;
      label = "animation.windows_out";
      break;
      EVENT(Scratchpad, scratchpad, "scratchpad");
      EVENT(Layers, layers, "layers");
      EVENT(Workspaces, workspaces, "workspaces");
      EVENT(Overview, overview, "overview");
    }
#undef EVENT
    auto& entry = cache[static_cast<unsigned>(event)];
    if (!settings.enabled || !enabled || source == nullptr || !*source) {
      entry = {};
      return nullptr;
    }
    if (entry.renderer != renderer || entry.source != *source) {
      entry.renderer = renderer;
      entry.source = *source;
      const auto& input = **source;
      entry.shader = {
          fx_animation_shader_create(renderer, input.code.c_str(), input.file.empty() ? label : input.file.c_str()),
          fx_animation_shader_unref
      };
    }
    return entry.shader.get();
  }

  void prepareAnimationShaders(wlr_renderer* renderer) {
    for (unsigned event = 0; event < FX_ANIMATION_SLOTS; ++event) {
      (void)animationShader(renderer, static_cast<AnimationEvent>(event));
    }
  }

  void clearAnimationShaderCache() { cache = {}; }

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
