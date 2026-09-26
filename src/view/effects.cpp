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
    const EffectPreset* preset = effectRegistry().presetConfig(m_border);
    return preset != nullptr && preset->kind == EffectKind::Border && !preset->inert() ? preset->padding : 0;
  }

  void ViewEffects::track(const void* owner) {
    if (std::ranges::find(m_owners, owner) == m_owners.end()) {
      m_owners.push_back(owner);
    }
  }

  void ViewEffects::untrack(const void* owner) {
    if (owner == nullptr) {
      return;
    }
    effectRegistry().removeInstance(owner);
    std::erase(m_owners, owner);
  }

  void ViewEffects::apply(const ApplyInput& input) {
    EffectRegistry& registry = effectRegistry();
    // The merge gate: with nothing selected this is two string checks.
    if (!configured()) {
      if (registry.active()) {
        untrack(input.border);
        untrack(input.surface);
        if (input.border != nullptr) {
          wlr_scene_node_set_animation(input.border, FX_SLOT_BORDER_EFFECT, nullptr, nullptr);
        }
      }
      return;
    }
    const EffectPreset* preset = m_border.empty() ? nullptr : registry.presetConfig(m_border);
    fx_effect_shader* shader = preset != nullptr ? registry.preset(m_border, EffectKind::Border) : nullptr;
    const bool active = shader != nullptr && input.border != nullptr && borderEffectApplies(input.gate);
    if (input.border != nullptr) {
      if (active) {
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
      } else {
        wlr_scene_node_set_animation(input.border, FX_SLOT_BORDER_EFFECT, nullptr, nullptr);
      }
    }
    if (active) {
      track(input.border);
      registry.updateInstance(
          input.border,
          {
              .output = input.output,
              .visible = wlr_scene_node_visible_in_box(input.border, &input.outputBox),
              .readsTime = fx_effect_shader_reads(shader, "umbriel_time"),
              .advancing = preset->animated && preset->speed > 0.0F && input.clockAdvancing,
          }
      );
    } else {
      untrack(input.border);
    }
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
