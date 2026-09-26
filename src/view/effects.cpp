#include "view/effects.h"

#include "scene/effect_registry.h"

#include <algorithm>

extern "C" {
#include <umbrielfx/render/effect.h>
}

namespace umbriel {

  void ViewEffects::resolve(const Effects& effects, const ResolvedWindowRule& rule) {
    const ViewEffectNames names = resolveViewEffectNames(effects, rule);
    m_border = names.border;
    m_window = names.window;
  }

  int ViewEffects::borderPadding() const {
    if (m_border.empty()) {
      return 0;
    }
    const EffectRegistry& registry = effectRegistry();
    return borderPresetPadding(
        registry.presetConfig(m_border), registry.preset(m_border, EffectKind::Border) != nullptr
    );
  }

  void ViewEffects::track(const void* owner) {
    if (std::ranges::find(m_owners, owner) == m_owners.end()) {
      m_owners.push_back(owner);
    }
  }

  void ViewEffects::untrack(const void* owner) {
    if (owner == nullptr || std::erase(m_owners, owner) == 0) {
      return;
    }
    effectRegistry().removeInstance(owner);
  }

  void ViewEffects::apply(const ApplyInput& input) {
    EffectRegistry& registry = effectRegistry();
    const bool gateOpen = input.border != nullptr && !m_border.empty() && borderEffectApplies(input.gate);
    fx_effect_shader* shader = gateOpen ? registry.preset(m_border, EffectKind::Border) : nullptr;
    const EffectPreset* preset = shader != nullptr ? registry.presetConfig(m_border) : nullptr;
    if (preset == nullptr) {
      if (input.border != nullptr) {
        wlr_scene_node_set_animation(input.border, FX_SLOT_BORDER_EFFECT, nullptr, nullptr);
      }
      untrack(input.border);
      if (m_window.empty()) {
        untrack(input.surface);
      }
      return;
    }
    fx_animation_parameters parameters{};
    const bool advancing = preset->animated && preset->speed > 0.0F;
    registry.fillTimeUniforms(parameters, advancing ? input.seconds * preset->speed : 0.0F, *preset, shader);
    if (preset->light) {
      parameters.light = {
          .enabled = true,
          .spread = static_cast<float>(preset->light->spread),
          .intensity = preset->light->intensity,
          .threshold = preset->light->threshold,
      };
    }
    wlr_scene_node_set_animation(input.border, FX_SLOT_BORDER_EFFECT, shader, &parameters);
    track(input.border);
    registry.updateInstance(
        input.border,
        {
            .output = input.output,
            .visible = wlr_scene_node_visible_in_box(input.border, &input.outputBox),
            .readsTime = fx_effect_shader_reads(shader, "umbriel_time"),
            .advancing = advancing && input.clockAdvancing,
        }
    );
  }

  void ViewEffects::detach() {
    EffectRegistry& registry = effectRegistry();
    for (const void* owner : m_owners) {
      registry.removeInstance(owner);
    }
    m_owners.clear();
  }

  void ViewEffects::detachNodes(wlr_scene_node* surface, wlr_scene_node* border) {
    untrack(surface);
    untrack(border);
  }

} // namespace umbriel
