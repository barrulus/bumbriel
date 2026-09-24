#pragma once

#include "config/animation_shader.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <variant>

extern "C" {
#include <umbrielfx/render/drag_physics.h>
}

namespace umbriel {
  class Section;

  enum class EffectScope : uint8_t {
    Content,
    BorderInner,
    BorderOuter,
    BorderFocus,
    Open,
    Close,
    Move,
    Resize,
    Focus,
    Scratchpad,
    Backdrop,
    Workspace,
    Overview,
    Screen,
    Overlay,
    Drag,
    Count
  };
  inline constexpr size_t kEffectScopeCount = static_cast<size_t>(EffectScope::Count);
  inline constexpr std::array<std::string_view, kEffectScopeCount> kEffectScopeNames{
      "content", "border.inner", "border.outer", "border.focus", "open",     "close",  "move",    "resize",
      "focus",   "scratchpad",   "backdrop",     "workspace",    "overview", "screen", "overlay", "drag"
  };
  using EffectMask = uint16_t;
  constexpr EffectMask effectBit(EffectScope scope) { return EffectMask{1} << static_cast<unsigned>(scope); }
  inline constexpr EffectMask kWindowEffects = ((EffectMask{1} << 10) - 1) | effectBit(EffectScope::Drag);
  inline constexpr EffectMask kLayerEffects =
      effectBit(EffectScope::Content) | effectBit(EffectScope::Open) | effectBit(EffectScope::Close);
  inline constexpr EffectMask kOutputEffects = ((EffectMask{1} << kEffectScopeCount) - 1) & ~kWindowEffects;
  inline constexpr EffectMask kAllEffects = kWindowEffects | kOutputEffects;
  enum class EffectOwner : uint8_t { Global, Output, Window, Layer, Region };
  [[nodiscard]] EffectMask effectMask(EffectOwner owner);
  [[nodiscard]] std::optional<EffectScope> effectScope(std::string_view name);
  [[nodiscard]] bool persistentEffect(EffectScope scope);
  [[nodiscard]] bool validEffectName(std::string_view name);

  struct EffectAction {
    EffectOwner owner = EffectOwner::Global;
    bool system = false;
    std::string operation, target;
    std::vector<std::string> names;
    EffectMask mask = kAllEffects;
    bool operator==(const EffectAction&) const = default;
  };
  std::optional<EffectAction> parseEffectAction(std::string_view text);
  std::string formatEffectAction(const EffectAction& action);

  struct EffectOrigin {
    std::string file;
    uint32_t line = 0, column = 0;
    bool operator==(const EffectOrigin&) const = default;
  };
  [[nodiscard]] EffectOrigin effectOrigin(const toml::node& node);
  using EffectNumber = std::variant<int32_t, float>;
  using EffectParam = std::variant<bool, int32_t, float, std::vector<EffectNumber>>;
  struct EffectPass {
    AnimationShaderSource source;
    std::string builtin;
    std::map<std::string, EffectParam> params;
    bool buffer = false;
    EffectOrigin origin;
    bool operator==(const EffectPass&) const = default;
  };
  struct EffectDrag {
    fx_drag_physics_parameters parameters = fx_drag_physics_default_parameters();
    bool operator==(const EffectDrag& other) const {
      const auto values = [](const fx_drag_physics_parameters& p) {
        return std::tie(
            p.stiffness, p.coupling, p.damping, p.pointer_response, p.stiffness_gradient, p.lag_gradient,
            p.downward_pull, p.motion_gain, p.decay
        );
      };
      return values(parameters) == values(other.parameters);
    }
  };
  struct EffectPipeline {
    bool enabled = true;
    bool palette = false;
    bool animated = true;
    bool focusedOnly = true;
    double speed = 1;
    int padding = 0, cursorRadius = 0;
    struct Light {
      bool enabled = false;
      double spread = 80, intensity = 1, threshold = 0.5;
      bool operator==(const Light&) const = default;
    } light;
    std::optional<EffectDrag> drag;
    std::optional<int> durationMs;
    std::optional<std::string> curve;
    std::vector<EffectPass> passes;
    EffectOrigin origin;
    bool operator==(const EffectPipeline&) const = default;
  };
  struct EffectDefinition {
    std::array<std::optional<EffectPipeline>, kEffectScopeCount> scopes;
    std::optional<std::vector<std::string>> choose;
    std::string selection = "unused_first";
    EffectOrigin origin;
    [[nodiscard]] EffectMask mask() const;
    bool operator==(const EffectDefinition&) const = default;
  };
  using EffectLibrary = std::map<std::string, EffectDefinition, std::less<>>;
  struct EffectSelector {
    std::vector<std::string> names;
    EffectOrigin origin;
    bool operator==(const EffectSelector&) const = default;
  };
  struct EffectPolicy {
    bool enabled = true, inCapture = false, readsCursor = false;
    std::string redraw = "auto";
    int fps = 0;
    bool operator==(const EffectPolicy&) const = default;
  };
  struct EffectRegion {
    std::string name, output;
    int x = 0, y = 0, width = 0, height = 0;
    EffectSelector effects;
    EffectOrigin origin;
    bool operator==(const EffectRegion&) const = default;
  };
  struct EffectAssignment {
    std::string effect, choice;
    EffectOrigin assignment, definition;
    bool operator==(const EffectAssignment&) const = default;
  };
  struct ResolvedEffect {
    std::optional<EffectPipeline> pipeline;
    EffectAssignment source;
    std::vector<EffectAssignment> overridden;
    bool runtimeDisabled = false;
    bool operator==(const ResolvedEffect&) const = default;
  };
  using ResolvedEffects = std::array<ResolvedEffect, kEffectScopeCount>;
  using EffectVariants = std::map<std::string, std::string, std::less<>>;

  [[nodiscard]] EffectLibrary readEffectLibrary(
      const toml::node* node, std::vector<ConfigDiagnostic>& diagnostics, std::vector<std::filesystem::path>& watches
  );
  [[nodiscard]] std::optional<EffectSelector>
  readEffectSelector(Section& section, std::vector<ConfigDiagnostic>& diagnostics);
  void readEffectPolicy(Section& section, EffectPolicy& policy, std::vector<ConfigDiagnostic>& diagnostics);
  [[nodiscard]] std::vector<EffectRegion>
  readEffectRegions(const toml::node* node, std::vector<ConfigDiagnostic>& diagnostics);
  void validateEffectSelector(
      const EffectLibrary& library, const EffectSelector& selector, EffectOwner owner,
      std::vector<ConfigDiagnostic>& diagnostics, EffectMask mask = kAllEffects
  );
  [[nodiscard]] ResolvedEffects resolveEffects(
      const EffectLibrary& library, std::span<const EffectSelector> selectors, EffectMask mask,
      const EffectVariants& variants = {}
  );
  void applyEffects(
      ResolvedEffects& resolved, const EffectLibrary& library, const EffectSelector& selector, EffectMask mask,
      const EffectVariants& variants = {}
  );
  [[nodiscard]] bool sameEffectPipeline(const EffectPipeline& a, const EffectPipeline& b);
} // namespace umbriel
