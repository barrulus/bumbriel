#pragma once

#include "config/effects.h"
#include "scene/animation_shader.h"
#include "scene/effect_ledger.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct fx_effect_shader;
struct fx_animation_parameters;
struct wlr_renderer;

namespace umbriel {

  class Server;

  // One compiled program per referenced preset for the current renderer, plus
  // the built-in lifecycle fade. Prepares at startup, on reload with the
  // effects or animation flags, and after renderer recovery; never compiles in
  // a render callback. Failures are cached as null with a diagnostic.
  class EffectRegistry {
  public:
    explicit EffectRegistry(Server& server);
    ~EffectRegistry();
    EffectRegistry(const EffectRegistry&) = delete;
    EffectRegistry& operator=(const EffectRegistry&) = delete;

    void prepare(wlr_renderer* renderer);
    void clear();
    [[nodiscard]] wlr_renderer* renderer() const { return m_renderer; }

    // The program for `name`, or null when the preset is off, inert, of another kind, or failed to compile.
    [[nodiscard]] fx_effect_shader* preset(std::string_view name, EffectKind kind) const;
    [[nodiscard]] const EffectPreset* presetConfig(std::string_view name) const;
    // The preset bound to an animation event through `effect =`, or null.
    [[nodiscard]] fx_effect_shader* animationShader(AnimationEvent event) const;
    // The program a lifecycle fade composes through: the event's preset, or for windows_in and windows_out without
    // one, the built-in fade. Null when buffers fade individually.
    [[nodiscard]] fx_effect_shader* lifecycleShader(AnimationEvent event) const;
    // The preset bound to an animation event through `effect =`, or null (also null for the built-in fade).
    [[nodiscard]] const EffectPreset* animationPreset(AnimationEvent event) const;
    // The animation clock in seconds, read only when a program needs it. Measured from an epoch
    // taken on the first call, so the millisecond count backing it can exceed a float's exact
    // integer range without the returned value losing precision.
    [[nodiscard]] float clockSeconds() const;
    // Adds `umbriel_time` when `shader` reads it, and the `[colors]` palette for palette presets.
    void fillTimeUniforms(
        fx_animation_parameters& parameters, float seconds, const EffectPreset& preset, const fx_effect_shader* shader
    ) const;

    [[nodiscard]] bool active() const { return m_ledger.active() > 0; }
    [[nodiscard]] EffectLedger& ledger() { return m_ledger; }
    void setSuspended(bool suspended) { m_ledger.setSuspended(suspended); }
    // Records an instance; schedules its output's effect frame when that output gains its first eligible instance.
    void updateInstance(const void* owner, const EffectInstanceState& state);
    void removeInstance(const void* owner);
    void removeOutput(const void* output) { m_ledger.removeOutput(output); }
    // Creates the scene's light layer once a compiled border preset has `light`.
    void ensureLightLayer();

  private:
    struct Entry {
      EffectKind kind = EffectKind::Animation;
      std::string code;
      std::shared_ptr<fx_effect_shader> shader; // null once compilation failed
    };
    void compile(const EffectPreset& preset);
    void referencedNames(std::vector<std::string>& names) const;

    Server* m_server = nullptr;
    wlr_renderer* m_renderer = nullptr;
    std::map<std::string, Entry, std::less<>> m_programs;
    std::shared_ptr<fx_effect_shader> m_builtinFade;
    EffectLedger m_ledger;
    mutable uint64_t m_clockEpochMsec = 0;
    mutable bool m_clockEpochSet = false;
  };

  // The Server's registry. Set in Server's constructor before any view exists.
  [[nodiscard]] EffectRegistry& effectRegistry();

  // Seconds elapsed from `epochMsec` to `nowMsec`, computed in double precision so the result keeps
  // sub-millisecond resolution long after the raw millisecond count exceeds a float's exact range.
  [[nodiscard]] inline float effectClockSeconds(uint64_t nowMsec, uint64_t epochMsec) {
    return static_cast<float>(static_cast<double>(nowMsec - epochMsec) / 1000.0);
  }

} // namespace umbriel
