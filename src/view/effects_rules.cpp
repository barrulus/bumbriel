#include "view/effects.h"

namespace umbriel {

  ViewEffectNames resolveViewEffectNames(const Effects& effects, const ResolvedWindowRule& rule) {
    // The most specific selector replaces the default by name; "off" and "" both disable it.
    const auto pick = [](const std::string& fallback, const std::optional<std::string>& override) {
      if (!override) {
        return fallback;
      }
      return *override == kEffectOff ? std::string() : *override;
    };
    return {.border = pick(effects.border, rule.borderEffect), .window = pick(effects.window, rule.windowEffect)};
  }

  bool borderEffectApplies(const BorderEffectGate& gate) {
    return gate.focused && gate.decorated && !gate.urgent && !gate.fullscreen;
  }

} // namespace umbriel
