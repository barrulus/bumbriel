#include "config/effects.h"

#include "check.h"

using umbriel::EffectKind;
using umbriel::EffectPreset;
using umbriel::Effects;

namespace {
  Effects twoPresets() {
    Effects effects;
    effects.presets.push_back(EffectPreset{.name = "pulse", .kind = EffectKind::Border, .shader = {.code = "x"}});
    effects.presets.push_back(EffectPreset{.name = "lines", .kind = EffectKind::Window, .shader = {.code = "y"}});
    return effects;
  }
} // namespace

UMBRIEL_TEST(effectKindsParseTheirCanonicalNamesOnly) {
  CHECK(umbriel::parseEffectKind("animation") == EffectKind::Animation);
  CHECK(umbriel::parseEffectKind("border") == EffectKind::Border);
  CHECK(umbriel::parseEffectKind("window") == EffectKind::Window);
  CHECK(umbriel::parseEffectKind("screen") == EffectKind::Screen);
  CHECK(umbriel::parseEffectKind("cursor") == EffectKind::Cursor);
  CHECK(!umbriel::parseEffectKind("Border"));
  CHECK(!umbriel::parseEffectKind("overlay"));
  CHECK(!umbriel::parseEffectKind(""));
  CHECK_EQ(umbriel::effectKindName(EffectKind::Cursor), std::string_view("cursor"));
}

UMBRIEL_TEST(effectReferencesRequireAnExistingPresetOfTheRightKind) {
  const Effects effects = twoPresets();
  CHECK(umbriel::findEffectPreset(effects, "pulse") != nullptr);
  CHECK(umbriel::findEffectPreset(effects, "PULSE") == nullptr);
  CHECK(!umbriel::effectReferenceError(effects, "", EffectKind::Border, false));
  CHECK(!umbriel::effectReferenceError(effects, "pulse", EffectKind::Border, false));
  CHECK_EQ(
      umbriel::effectReferenceError(effects, "pulse", EffectKind::Window, false).value_or(""),
      std::string("effect 'pulse' is a border preset, not a window preset")
  );
  CHECK_EQ(
      umbriel::effectReferenceError(effects, "missing", EffectKind::Window, false).value_or(""),
      std::string("unknown effect 'missing'")
  );
}

UMBRIEL_TEST(offIsOnlyValidWhereAnOverrideCanDisableTheDefault) {
  const Effects effects = twoPresets();
  CHECK(!umbriel::effectReferenceError(effects, "off", EffectKind::Border, true));
  CHECK_EQ(
      umbriel::effectReferenceError(effects, "off", EffectKind::Border, false).value_or(""),
      std::string("unknown effect 'off'")
  );
}

UMBRIEL_TEST(inertPresetsKeepTheirNameWithoutSource) {
  EffectPreset preset{.name = "broken", .kind = EffectKind::Screen};
  CHECK(preset.inert());
  preset.shader.code = "vec4 screen(vec2 uv) { return umbriel_sample(uv); }";
  CHECK(!preset.inert());
}

int main() { return RUN_TESTS(); }
