#include "config/effects.h"

#include "check.h"
#include "scene/effect_ledger.h"

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

UMBRIEL_TEST(ledgerCountsOnlyVisibleAdvancingTimeReaders) {
  umbriel::EffectLedger ledger;
  int outputA = 0;
  int outputB = 0;
  int viewOne = 0;
  int viewTwo = 0;
  ledger.update(&viewOne, {.output = &outputA, .visible = true, .readsTime = true, .advancing = true});
  ledger.update(&viewTwo, {.output = &outputA, .visible = true, .readsTime = false, .advancing = true});
  CHECK_EQ(ledger.eligible(&outputA), 1U);
  CHECK_EQ(ledger.eligible(&outputB), 0U);
  CHECK_EQ(ledger.active(), 2U);

  // A frozen clock, animated = false, or speed = 0 all arrive as advancing = false.
  ledger.update(&viewOne, {.output = &outputA, .visible = true, .readsTime = true, .advancing = false});
  CHECK_EQ(ledger.eligible(&outputA), 0U);
  ledger.update(&viewOne, {.output = &outputA, .visible = true, .readsTime = true, .advancing = true});
  CHECK_EQ(ledger.eligible(&outputA), 1U);

  // Hidden or off-output instances never request frames.
  ledger.update(&viewOne, {.output = &outputA, .visible = false, .readsTime = true, .advancing = true});
  CHECK_EQ(ledger.eligible(&outputA), 0U);
  ledger.update(&viewOne, {.output = &outputB, .visible = true, .readsTime = true, .advancing = true});
  CHECK_EQ(ledger.eligible(&outputA), 0U);
  CHECK_EQ(ledger.eligible(&outputB), 1U);

  ledger.remove(&viewOne);
  CHECK_EQ(ledger.eligible(&outputB), 0U);
  CHECK_EQ(ledger.active(), 1U);
}

UMBRIEL_TEST(ledgerSuspensionAndOutputRemovalClearEligibility) {
  umbriel::EffectLedger ledger;
  int output = 0;
  int screen = 0;
  int cursor = 0;
  ledger.update(&screen, {.output = &output, .visible = true, .readsTime = true, .advancing = true});
  ledger.update(&cursor, {.output = &output, .visible = true, .readsTime = true, .advancing = true});
  CHECK_EQ(ledger.eligible(&output), 2U);
  ledger.setSuspended(true);
  CHECK(ledger.suspended());
  CHECK_EQ(ledger.eligible(&output), 0U);
  CHECK_EQ(ledger.active(), 2U);
  ledger.setSuspended(false);
  CHECK_EQ(ledger.eligible(&output), 2U);
  ledger.removeOutput(&output);
  CHECK_EQ(ledger.eligible(&output), 0U);
  CHECK_EQ(ledger.active(), 0U);
}

UMBRIEL_TEST(ledgerUpdatesReplaceAnOwnersPreviousState) {
  umbriel::EffectLedger ledger;
  int output = 0;
  int owner = 0;
  for (int i = 0; i < 3; ++i) {
    ledger.update(&owner, {.output = &output, .visible = true, .readsTime = true, .advancing = true});
  }
  CHECK_EQ(ledger.eligible(&output), 1U);
  CHECK_EQ(ledger.active(), 1U);
}

int main() { return RUN_TESTS(); }
