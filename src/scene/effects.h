#pragma once

#include "config/config.h"

#include <memory>

struct wlr_renderer;
struct wlr_scene_rect;
struct wlr_scene_node;
struct fx_animation_parameters;
struct fx_animation_shader;
struct fx_postprocess_chain;
struct fx_decoration_shader;
struct fx_scene_postprocess;

namespace umbriel {
  struct PreparedEffect {
    std::vector<std::shared_ptr<fx_animation_shader>> animation;
    std::shared_ptr<fx_postprocess_chain> postprocess;
    std::shared_ptr<fx_decoration_shader> decoration;
  };
  class EffectEvent {
  public:
    EffectEvent() = default;
    ~EffectEvent();
    EffectEvent(const EffectEvent&) = delete;
    EffectEvent& operator=(const EffectEvent&) = delete;
    void rebuild(wlr_renderer* renderer);
    void update(
        wlr_scene_node* node, unsigned slot, const ResolvedEffect& next, EffectScope scope,
        const fx_animation_parameters& parameters, bool running, bool enabled, fx_animation_shader* fallback = nullptr
    );
    [[nodiscard]] bool custom() const { return m_programs && !m_programs->animation.empty(); }
    [[nodiscard]] uint64_t generation() const { return m_generation; }
    [[nodiscard]] const ResolvedEffect& captured() const { return m_captured; }
    void reset();

  private:
    uint64_t m_transition = 0, m_generation = 0;
    bool m_started = false;
    EffectScope m_scope = EffectScope::Open;
    ResolvedEffect m_captured;
    std::shared_ptr<const PreparedEffect> m_programs;
    std::array<float, 32> m_palette{};
    int m_paletteCount = 0;
  };
  bool effectsEnabled();
  bool setEffectSystem(std::string_view operation, wlr_renderer* renderer, std::vector<ConfigDiagnostic>& diagnostics);
  bool nativeEffectEnabled(EffectScope scope, bool layer = false);
  bool prepareEffects(wlr_renderer* renderer, const Config& config, std::vector<ConfigDiagnostic>& diagnostics);
  void clearEffects();
  fx_scene_postprocess outputEffect(const ResolvedEffect& effect, EffectScope scope);
  void applyEffect(wlr_scene_rect* rect, const ResolvedEffect& effect, EffectScope scope);
  std::shared_ptr<const PreparedEffect> preparedEffect(std::string_view name, EffectScope scope);
  std::shared_ptr<const PreparedEffect> preparedEffect(const ResolvedEffect& effect, EffectScope scope);
} // namespace umbriel
