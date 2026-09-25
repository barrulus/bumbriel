# Effects Stage 2: Configuration and the effect registry

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse `[effects]`, `[effects.preset.<name>]`, the selectors (`[[window_rule]] border_effect/window_effect`, `[output.X] screen_effect`, `[animation.<event>] effect`, `[animation.windows_drag] physics`), validate every reference after loading, reject duplicate presets across files, and give the Server an `EffectRegistry` that compiles exactly the referenced presets once per renderer.

**Architecture:** `src/config/effects.{h,cpp}` absorbs `animation_shader.{h,cpp}` (the shader file reader becomes `readShaderSource`) and holds the pure types and reference checks. `config.cpp` collects references with their TOML locations while parsing and validates them once every section is read, so forward and cross-include references work. `config_merge.cpp` records the defining file of each preset before merging so duplicates are Errors naming both files. `EffectRegistry` (Server-owned) keys programs by kind and exact source, prepares at startup, on reload with the `effects`/`animation` flags, and after renderer recovery; a pure `EffectLedger` inside it tracks per-output frame eligibility for later stages.

**Tech Stack:** C++23, toml++, `check.h` unit tests, harness.

**Spec:** `docs/superpowers/specs/2026-09-25-effects-design.md` §1 (Configuration) and §2 (`EffectRegistry`). Index: `2026-09-25-effects-00-index.md`.

## Global Constraints

- **Reuse first:** Before adding a utility, helper, or method, inspect existing implementations and callers. Reuse or narrowly extend the owning helper; if none fits, record why in this plan and give shared behavior one owner.
- **Comments and documentation (maintainer policy):** Comments and documentation explain what the code currently does and any non-obvious constraint a reader needs. Do not narrate history, migrations, rejected alternatives, or "why we don't do X"; git holds that. This applies to Markdown, `meson.build`, and code comments alike. Keep them short; if a comment is longer than the code it describes, cut it down. The comments in this plan's snippets are written to that policy and can be kept; prose in the plan that explains a decision is for the executor, not for the code.
- **Directed reuse for this stage:** Reuse the existing shader reader, `Section`, `warnAt`, include expansion, config-change projections, and rule resolution. Keep one reference-validation path and one program ownership implementation; adapt existing helpers before introducing parallel parsers or caches.
- **Every task gate:** Before marking a task complete or committing, audit its diff for duplicate helpers and for comments or doc text that narrate history, migrations, or alternatives, or that outgrow the code they describe.

See the index. Stage-specific:

- The legacy `[animation.<event>] shader` key keeps working in this stage (Stage 3 removes it with the check migration), so every existing check stays green.
- `packagedAnimationDefaultsMatchCompiledDefaults` (`tests/unit/config_load.cpp:3190`) requires new `Config::Animation` members to default to the packaged values; `windows_drag.physics` defaults to `false` and `examples/config.toml` gains no active animation keys.
- Every diagnostic is a Warning except duplicate presets across files (Error). Warnings leave the setting off; an Error keeps the previous configuration.
- Any section reader that claims keys does so per kind: a `border` preset claims `padding`, `speed`, `animated`, `overlay`, `light`; a `cursor` preset claims `radius`; every kind claims `kind`, `shader`, `palette`. Inapplicable keys fall through to `Section`'s unknown-key warning.

---

### Task 2.1: `src/config/effects.{h,cpp}` absorbs the shader reader

**Files:**
- Create: `src/config/effects.h`, `src/config/effects.cpp`
- Delete: `src/config/animation_shader.h`, `src/config/animation_shader.cpp`
- Rename: `tests/unit/animation_shader.cpp` → `tests/unit/shader_source.cpp`
- Create: `tests/unit/effects.cpp`
- Modify: `src/config/config.h:2,561-640` (include and the nine `shader` members' type), `src/config/config.cpp:1096-1102`, `meson.build:505`, `tests/meson.build:12`, `docs/design/animation-shaders.md:8` (name only; the full doc rewrite is Stage 8)

**Interfaces:**
- Produces (`src/config/effects.h`): everything in the index glossary under `src/config/effects.h`, plus `[[nodiscard]] bool EffectPreset::inert() const { return shader.code.empty(); }`.
- Produces: `ShaderReadResult readShaderSource(Section& section, std::string_view key, std::vector<ConfigDiagnostic>& diagnostics);` — identical behaviour to `readAnimationShader`, with the key name a parameter and messages saying "shader" instead of "animation shader".

- [ ] **Step 1: Write the failing pure tests**

Create `tests/unit/effects.cpp`:

```cpp
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
```

Register in `tests/meson.build` after `['animation-shader', ...]` (which is renamed in Step 3):

```meson
  ['effects', ['unit/effects.cpp'], []],
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'just configure >/dev/null && meson compile -C build-debug effects-test'`
Expected: compile error, `config/effects.h` not found.

- [ ] **Step 3: Create `effects.h`/`effects.cpp` and retire `animation_shader.{h,cpp}`**

Create `src/config/effects.h`:

```cpp
#pragma once

#include "config/config_diag.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace umbriel {

  class Section;

  enum class EffectKind : std::uint8_t { Animation, Border, Window, Screen, Cursor };

  [[nodiscard]] std::optional<EffectKind> parseEffectKind(std::string_view text);
  [[nodiscard]] std::string_view effectKindName(EffectKind kind);

  // Keep source text in the resolved configuration. File edits then participate
  // in config equality, and render paths never perform filesystem I/O.
  struct ShaderSource {
    std::string code;
    std::filesystem::path file;
    bool operator==(const ShaderSource&) const = default;
  };

  struct ShaderReadResult {
    std::optional<ShaderSource> source;
    // Includes missing files so creating one can trigger another config load.
    std::vector<std::filesystem::path> watchPaths;
  };

  inline constexpr std::size_t kShaderSourceLimit = 256 * 1024;

  // Reads the shader file path under `key`. Relative file paths belong to the
  // TOML value's source file, including when tables were merged.
  [[nodiscard]] ShaderReadResult
  readShaderSource(Section& section, std::string_view key, std::vector<ConfigDiagnostic>& diagnostics);

  // The reserved selector value that disables a default per window or output.
  inline constexpr std::string_view kEffectOff = "off";

  struct BorderLight {
    int spread = 80;        // 1-256 logical px
    float intensity = 1.0F; // 0-4
    float threshold = 0.5F; // 0-1
    bool operator==(const BorderLight&) const = default;
  };

  struct EffectPreset {
    std::string name;
    EffectKind kind = EffectKind::Animation;
    // Empty code means the file was missing or unreadable: the preset exists so
    // references resolve, but it renders plainly.
    ShaderSource shader;
    bool palette = false;
    int padding = 0;                   // border: 0-1024 logical px
    float speed = 1.0F;                // border: 0-10
    bool animated = true;              // border
    std::string overlay;               // border: names a window preset
    std::optional<BorderLight> light;  // border
    int radius = 0;                    // cursor: 0-4096, 0 = whole output
    [[nodiscard]] bool inert() const { return shader.code.empty(); }
    bool operator==(const EffectPreset&) const = default;
  };

  struct Effects {
    std::vector<EffectPreset> presets;
    std::string border;   // "" = off
    std::string window;
    std::string screen;
    std::string cursor;
    int maxFps = 0;       // 0-240, 0 follows the refresh rate
    bool inCapture = false;
    bool operator==(const Effects&) const = default;
  };

  [[nodiscard]] const EffectPreset* findEffectPreset(const Effects& effects, std::string_view name);
  // "" and, when allowOff, "off" are valid; otherwise the preset must exist with
  // `kind`. Returns the diagnostic text on failure.
  [[nodiscard]] std::optional<std::string>
  effectReferenceError(const Effects& effects, std::string_view name, EffectKind kind, bool allowOff);

} // namespace umbriel
```

Create `src/config/effects.cpp`: move the body of `readAnimationShader` from `src/config/animation_shader.cpp:14-97` into `readShaderSource`, with these changes: `section.take(key)` instead of `"shader"`; `AnimationShaderSource` → `ShaderSource`; `kAnimationShaderSourceLimit` → `kShaderSourceLimit`; every message drops the word "animation" ("shader path must be a string", "shader path must not be empty or contain NUL bytes", "relative shader requires a config source path", "shader source exceeds 256 KiB", "shader source must not be blank or contain NUL bytes"; "cannot read shader file '{}': {}" and "shader file '{}' must be a readable regular file" are unchanged). Then append:

```cpp
  std::optional<EffectKind> parseEffectKind(std::string_view text) {
    if (text == "animation") {
      return EffectKind::Animation;
    }
    if (text == "border") {
      return EffectKind::Border;
    }
    if (text == "window") {
      return EffectKind::Window;
    }
    if (text == "screen") {
      return EffectKind::Screen;
    }
    if (text == "cursor") {
      return EffectKind::Cursor;
    }
    return std::nullopt;
  }

  std::string_view effectKindName(EffectKind kind) {
    switch (kind) {
    case EffectKind::Animation:
      return "animation";
    case EffectKind::Border:
      return "border";
    case EffectKind::Window:
      return "window";
    case EffectKind::Screen:
      return "screen";
    case EffectKind::Cursor:
      return "cursor";
    }
    return "effect";
  }

  const EffectPreset* findEffectPreset(const Effects& effects, std::string_view name) {
    const auto preset = std::ranges::find(effects.presets, name, &EffectPreset::name);
    return preset != effects.presets.end() ? &*preset : nullptr;
  }

  std::optional<std::string>
  effectReferenceError(const Effects& effects, std::string_view name, EffectKind kind, bool allowOff) {
    if (name.empty() || (allowOff && name == kEffectOff)) {
      return std::nullopt;
    }
    const EffectPreset* preset = findEffectPreset(effects, name);
    if (preset == nullptr) {
      return std::format("unknown effect '{}'", name);
    }
    if (preset->kind != kind) {
      return std::format(
          "effect '{}' is a {} preset, not a {} preset", name, effectKindName(preset->kind), effectKindName(kind)
      );
    }
    return std::nullopt;
  }
```

Then:
- `git rm src/config/animation_shader.h src/config/animation_shader.cpp`; `git mv tests/unit/animation_shader.cpp tests/unit/shader_source.cpp`.
- `src/config/config.h:2`: `#include "config/effects.h"`; the nine `std::optional<AnimationShaderSource> shader;` members become `std::optional<ShaderSource> shader;`.
- `src/config/config.cpp:1096-1102` (`readShader` lambda): `auto result = readShaderSource(section, "shader", configStore().mutableDiagnostics());`.
- `src/scene/animation_shader.cpp:17`: `std::optional<ShaderSource> source;`, and `:69`: `const std::optional<ShaderSource>* source = nullptr;`.
- `meson.build:505`: `'src/config/animation_shader.cpp',` → `'src/config/effects.cpp',`.
- `tests/meson.build:12`: `['animation-shader', ['unit/animation_shader.cpp'], []],` → `['shader-source', ['unit/shader_source.cpp'], []],`.
- In `tests/unit/shader_source.cpp`: include `"config/effects.h"`; `umbriel::AnimationShaderReadResult` → `umbriel::ShaderReadResult`; `readAnimationShader(section, diagnostics)` → `readShaderSource(section, "shader", diagnostics)`; `kAnimationShaderSourceLimit` → `kShaderSourceLimit`; message substrings "animation shader" → "shader" where the test greps them (the tests grep `must be a string`, `must not be empty`, `cannot read shader file`, `regular file`, `must not be blank`, `NUL`, `exceeds 256 KiB` — all still match).
- `docs/design/animation-shaders.md:8`: `readAnimationShader` → `readShaderSource` (`src/config/effects.cpp`).

- [ ] **Step 4: Run the tests**

Run: `nix develop . --command bash -c 'just test'`
Expected: `effects`, `shader-source`, `config-load` and every other unit test pass.

- [ ] **Step 5: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "refactor(config): move the shader reader into effects.cpp"
```

---

### Task 2.2: Parse `[effects]`, presets, selectors, and `windows_drag`

**Files:**
- Modify: `src/config/config.h` (`OutputRule` :206-242, `WindowRule` :296-398, `ResolvedWindowRule` :401-437, `Config::Animation` :553-648, `Config` :482-900), `src/config/config.cpp` (`parseAnimationSection` :1035-1218, `readOutputs` :1669-1848, `readWindowRules` :2110-2129, `parseInto` :2482-2505), `src/config/change.h`, `src/config/change.cpp`, `src/config/resolve.cpp` (window rule apply block)
- Test: `tests/unit/config_load.cpp`, `tests/unit/config_change.cpp`, `tests/unit/config_resolve.cpp`

**Interfaces:**
- Produces: `Config::effects` (`Effects`), `Config::Animation::<event>::effect` (`std::string`), `Config::Animation::WindowsDrag { bool physics = false; bool operator==(const WindowsDrag&) const = default; } windowsDrag;`, `WindowRule::borderEffect`, `WindowRule::windowEffect`, `ResolvedWindowRule::borderEffect`, `ResolvedWindowRule::windowEffect` (`std::optional<std::string>`), `OutputRule::screenEffect` (`std::optional<std::string>`), `effectPalette(const Config::Colors&)`, `ConfigChange::effects`, `ConfigEffects::effects`.
- Produces (config.cpp, anonymous namespace): `struct EffectReference { std::string context; std::string name; EffectKind kind; bool allowOff; toml::source_region source; std::function<void()> clear; };` collected in `std::vector<EffectReference>` passed to every reader that records one.

- [ ] **Step 1: Write the failing config tests**

Append to `tests/unit/config_load.cpp` (before `int main`):

```cpp
UMBRIEL_TEST(effectPresetsClaimOnlyTheirKindsKeys) {
  const TempConfigTree tree;
  tree.write("pulse.glsl", "vec4 border(vec2 uv) { return umbriel_sample(uv); }");
  tree.write("glow.glsl", "vec4 cursor(vec2 uv) { return umbriel_sample(uv); }");
  tree.write(
      "config.toml",
      "[effects]\nmax_fps = 60\nin_capture = true\n"
      "[effects.preset.pulse]\nkind = \"border\"\nshader = \"pulse.glsl\"\npadding = 12\nspeed = 2.5\nanimated = false\n"
      "palette = true\nradius = 5\n"
      "[effects.preset.pulse.light]\nspread = 40\nintensity = 2\nthreshold = 0.25\n"
      "[effects.preset.glow]\nkind = \"cursor\"\nshader = \"glow.glsl\"\nradius = 96\npadding = 3\n"
  );
  ConfigStore& store = umbriel::configStore();
  store.setRootPath(tree.path("config.toml"), true);
  CHECK(store.reload().success);
  const auto& effects = store.config().effects;
  CHECK_EQ(effects.maxFps, 60);
  CHECK(effects.inCapture);
  CHECK_EQ(effects.presets.size(), size_t{2});
  const umbriel::EffectPreset* pulse = umbriel::findEffectPreset(effects, "pulse");
  CHECK(pulse != nullptr);
  if (pulse != nullptr) {
    CHECK(pulse->kind == umbriel::EffectKind::Border);
    CHECK_EQ(pulse->padding, 12);
    CHECK(pulse->speed == 2.5F);
    CHECK(!pulse->animated);
    CHECK(pulse->palette);
    CHECK(pulse->light.has_value());
    if (pulse->light) {
      CHECK_EQ(pulse->light->spread, 40);
      CHECK(pulse->light->intensity == 2.0F);
      CHECK(pulse->light->threshold == 0.25F);
    }
    CHECK(pulse->shader.file == tree.path("pulse.glsl"));
    CHECK(!pulse->inert());
  }
  const umbriel::EffectPreset* glow = umbriel::findEffectPreset(effects, "glow");
  CHECK(glow != nullptr);
  if (glow != nullptr) {
    CHECK_EQ(glow->radius, 96);
  }
  CHECK(containsDiagnostic(store, "unknown key effects.preset.pulse.radius"));
  CHECK(containsDiagnostic(store, "unknown key effects.preset.glow.padding"));
  CHECK_EQ(std::ranges::count(store.watchPaths(), tree.path("pulse.glsl")), 1);
}

UMBRIEL_TEST(effectPresetsNeedAKindAndKeepTheirNameWithoutAShader) {
  const TempConfig file;
  ConfigStore& store = umbriel::configStore();
  store.setRootPath(file.path(), true);
  file.write("[effects.preset.nokind]\nshader = \"x.glsl\"\n[effects.preset.missing]\nkind = \"screen\"\nshader = \"absent.glsl\"\n[effects.preset.off]\nkind = \"screen\"\n");
  CHECK(store.reload().success);
  CHECK(umbriel::findEffectPreset(store.config().effects, "nokind") == nullptr);
  CHECK(containsDiagnostic(store, "ignoring effects.preset.nokind (kind must be animation|border|window|screen|cursor)"));
  const umbriel::EffectPreset* missing = umbriel::findEffectPreset(store.config().effects, "missing");
  CHECK(missing != nullptr && missing->inert());
  CHECK(containsDiagnostic(store, "cannot read shader file"));
  CHECK(umbriel::findEffectPreset(store.config().effects, "off") == nullptr);
  CHECK(containsDiagnostic(store, "ignoring effects.preset.off ('off' is reserved)"));
}

UMBRIEL_TEST(effectSelectorsLoadAtEveryLevel) {
  const TempConfigTree tree;
  tree.write("a.glsl", "vec4 border(vec2 uv) { return umbriel_sample(uv); }");
  tree.write("w.glsl", "vec4 window(vec2 uv) { return umbriel_sample(uv); }");
  tree.write("s.glsl", "vec4 screen(vec2 uv) { return umbriel_sample(uv); }");
  tree.write("c.glsl", "vec4 cursor(vec2 uv) { return umbriel_sample(uv); }");
  tree.write("o.glsl", "vec4 animation(vec2 uv) { return umbriel_sample(uv); }");
  tree.write(
      "config.toml",
      "[effects]\nborder = \"ring\"\nwindow = \"lines\"\nscreen = \"vig\"\ncursor = \"glow\"\n"
      "[effects.preset.ring]\nkind = \"border\"\nshader = \"a.glsl\"\noverlay = \"lines\"\n"
      "[effects.preset.lines]\nkind = \"window\"\nshader = \"w.glsl\"\n"
      "[effects.preset.vig]\nkind = \"screen\"\nshader = \"s.glsl\"\n"
      "[effects.preset.glow]\nkind = \"cursor\"\nshader = \"c.glsl\"\n"
      "[effects.preset.open]\nkind = \"animation\"\nshader = \"o.glsl\"\n"
      "[[window_rule]]\nmatch.app_id = \"^foot$\"\nborder_effect = \"off\"\nwindow_effect = \"lines\"\n"
      "[output.\"HEADLESS-1\"]\nscreen_effect = \"off\"\n"
      "[animation.windows_in]\neffect = \"open\"\n"
      "[animation.windows_drag]\nphysics = true\n"
  );
  ConfigStore& store = umbriel::configStore();
  store.setRootPath(tree.path("config.toml"), true);
  CHECK(store.reload().success);
  const auto& config = store.config();
  CHECK_EQ(config.effects.border, std::string("ring"));
  CHECK_EQ(config.effects.window, std::string("lines"));
  CHECK_EQ(config.effects.screen, std::string("vig"));
  CHECK_EQ(config.effects.cursor, std::string("glow"));
  CHECK_EQ(config.windowRules.size(), size_t{1});
  CHECK(config.windowRules[0].borderEffect == "off");
  CHECK(config.windowRules[0].windowEffect == "lines");
  CHECK_EQ(config.outputs.size(), size_t{1});
  CHECK(config.outputs[0].screenEffect == "off");
  CHECK_EQ(config.animation.windowsIn.effect, std::string("open"));
  CHECK(config.animation.windowsDrag.physics);
  CHECK(!containsDiagnostic(store, "unknown key"));
  const umbriel::EffectPreset* ring = umbriel::findEffectPreset(config.effects, "ring");
  CHECK(ring != nullptr && ring->overlay == "lines");
}

UMBRIEL_TEST(effectReferencesAreValidatedAfterEverySectionIsRead) {
  const TempConfigTree tree;
  tree.write("a.glsl", "vec4 border(vec2 uv) { return umbriel_sample(uv); }");
  tree.write(
      "config.toml",
      // Forward reference: the selector precedes the preset in the file and the preset comes from an include.
      "[effects]\nborder = \"ring\"\nwindow = \"ring\"\nscreen = \"nope\"\n"
      "[include]\nfiles = [\"presets.toml\"]\n"
      "[[window_rule]]\nmatch.app_id = \"^foot$\"\nborder_effect = \"nope\"\nwindow_effect = \"\"\n"
      "[output.\"HEADLESS-1\"]\nscreen_effect = \"ring\"\n"
      "[animation.windows_out]\neffect = \"ring\"\n"
  );
  tree.write(
      "presets.toml",
      "[effects.preset.ring]\nkind = \"border\"\nshader = \"a.glsl\"\noverlay = \"nope\"\n"
      "[effects.preset.ring2]\nkind = \"border\"\nshader = \"a.glsl\"\noverlay = \"ring\"\n"
  );
  ConfigStore& store = umbriel::configStore();
  store.setRootPath(tree.path("config.toml"), true);
  CHECK(store.reload().success);
  const auto& config = store.config();
  CHECK_EQ(config.effects.border, std::string("ring"));
  CHECK(config.effects.window.empty());
  CHECK(containsDiagnostic(store, "ignoring effects.window (effect 'ring' is a border preset, not a window preset)"));
  CHECK(config.effects.screen.empty());
  CHECK(containsDiagnostic(store, "ignoring effects.screen (unknown effect 'nope')"));
  CHECK(!config.windowRules[0].borderEffect);
  CHECK(containsDiagnostic(store, "ignoring window_rule.border_effect (unknown effect 'nope')"));
  CHECK(config.windowRules[0].windowEffect == "");
  CHECK(!config.outputs[0].screenEffect);
  CHECK(containsDiagnostic(store, "ignoring output.HEADLESS-1.screen_effect (effect 'ring' is a border preset, not a screen preset)"));
  CHECK(config.animation.windowsOut.effect.empty());
  CHECK(containsDiagnostic(store, "ignoring animation.windows_out.effect (effect 'ring' is a border preset, not a animation preset)"));
  const umbriel::EffectPreset* ring = umbriel::findEffectPreset(config.effects, "ring");
  CHECK(ring != nullptr && ring->overlay.empty());
  CHECK(containsDiagnostic(store, "ignoring effects.preset.ring.overlay (unknown effect 'nope')"));
  // An overlay must name a window preset: a border preset is the wrong kind.
  const umbriel::EffectPreset* ring2 = umbriel::findEffectPreset(config.effects, "ring2");
  CHECK(ring2 != nullptr && ring2->overlay.empty());
  CHECK(containsDiagnostic(store, "ignoring effects.preset.ring2.overlay (effect 'ring' is a border preset, not a window preset)"));
}

UMBRIEL_TEST(effectPaletteFollowsTheColorsSectionOrder) {
  umbriel::Config config;
  config.colors.accentPrimary = {1, 0, 0, 1};
  config.colors.accentSecondary = {0, 1, 0, 1};
  config.colors.warning = {0, 0, 1, 1};
  config.colors.error = {1, 1, 0, 1};
  const auto palette = umbriel::effectPalette(config.colors);
  CHECK(palette[0] == config.colors.accentPrimary);
  CHECK(palette[1] == config.colors.accentSecondary);
  CHECK(palette[2] == config.colors.warning);
  CHECK(palette[3] == config.colors.error);
}

UMBRIEL_TEST(effectReloadsFlagOnlyEffectDependentState) {
  const TempConfigTree tree;
  tree.write("a.glsl", "vec4 border(vec2 uv) { return umbriel_sample(uv); }");
  tree.write("config.toml", "[effects.preset.ring]\nkind = \"border\"\nshader = \"a.glsl\"\n[effects]\nborder = \"ring\"\n");
  ConfigStore& store = umbriel::configStore();
  store.setRootPath(tree.path("config.toml"), true);
  CHECK(store.reload().success);
  const auto same = store.reload();
  CHECK(same.success);
  CHECK(!same.effects.effects);
  CHECK(!same.effects.any());
  tree.write("a.glsl", "vec4 border(vec2 uv) { return umbriel_sample(uv) * 0.5; }");
  const auto edited = store.reload();
  CHECK(edited.success);
  CHECK(edited.effects.effects);
  CHECK(edited.change.effects);
  CHECK(!edited.effects.animation);
  tree.write("config.toml", "[effects.preset.ring]\nkind = \"border\"\nshader = \"a.glsl\"\n[effects]\nborder = \"ring\"\n[output.\"HEADLESS-1\"]\nscreen_effect = \"off\"\n");
  const auto output = store.reload();
  CHECK(output.success);
  CHECK(output.effects.effects);
  CHECK(!output.effects.outputState);
}
```

Append to `tests/unit/config_resolve.cpp` next to the existing decoration-override test (around line 588):

```cpp
UMBRIEL_TEST(windowRuleEffectOverridesUseLastWriterWins) {
  umbriel::Config config;
  umbriel::WindowRule first;
  first.appIdPattern = "foot";
  first.appIdRegex = std::regex("foot");
  first.borderEffect = "pulse";
  first.windowEffect = "lines";
  umbriel::WindowRule second = first;
  second.borderEffect = "off";
  second.windowEffect.reset();
  config.windowRules = {first, second};
  const auto resolved = umbriel::resolveWindowRules(config, "foot", std::nullopt, std::nullopt, ContentType::None, {}, 0);
  CHECK(resolved.borderEffect == "off");
  CHECK(resolved.windowEffect == "lines");
}
```

(Match how neighbouring tests in that file build rules; if they use a helper such as `makeRule(...)`, use it.)

- [ ] **Step 2: Run the tests to verify they fail**

Run: `nix develop . --command bash -c 'meson compile -C build-debug config-load-test 2>&1 | tail -5'`
Expected: compile errors (`Config::effects`, `windowsDrag`, `borderEffect`, `effectPalette` undeclared).

- [ ] **Step 3: Add the config members**

`src/config/config.h`:

- `OutputRule` (after `float sdrWhite = 203.0F;`): `std::optional<std::string> screenEffect; // "off" disables the default`.
- `WindowRule` (after `std::optional<bool> shadow;`):
  ```cpp
    // Override [effects] border and window for windows this rule matches; "off" disables the default.
    std::optional<std::string> borderEffect;
    std::optional<std::string> windowEffect;
  ```
  and in its `operator==` chain append `&& borderEffect == other.borderEffect && windowEffect == other.windowEffect` before the final `;`.
- `ResolvedWindowRule` (after `std::optional<bool> shadow;`): `std::optional<std::string> borderEffect; std::optional<std::string> windowEffect;`.
- `Config::Animation`: in each of the nine event structs replace nothing yet (the `shader` member stays until Stage 3) and add `std::string effect;` right after `shader`. Add after `layers`:
  ```cpp
      struct WindowsDrag {
        // Deform the window like an elastic sheet while it is dragged by the pointer.
        bool physics = false;
        bool operator==(const WindowsDrag&) const = default;
      } windowsDrag;
  ```
- `Config`: after `} animation;` add `Effects effects;`.
- After `struct Config { ... };` add:
  ```cpp
    // Palette order shaders see through umbriel_palette_at: accent_primary, accent_secondary, warning, error.
    [[nodiscard]] inline std::array<std::array<float, 4>, 4> effectPalette(const Config::Colors& colors) {
      return {colors.accentPrimary, colors.accentSecondary, colors.warning, colors.error};
    }
  ```

`src/config/change.h`: add `bool effects = false;` to `ConfigChange` (after `workspaceRules`, and `|| effects` in `any()`) and to `ConfigEffects` (after `internalUi`, and `|| effects` in `any()`).

`src/config/change.cpp`:
- `ConfigChange::between`: `.effects = before.effects != after.effects,`; `everything()`: `.effects = true`; `summary()`: `add(effects, "effects")`.
- Add next to `sameOutputTearingPolicy`:
  ```cpp
    bool sameOutputScreenEffect(const OutputRule* before, const OutputRule* after) {
      static const OutputRule defaults;
      const OutputRule& lhs = before != nullptr ? *before : defaults;
      const OutputRule& rhs = after != nullptr ? *after : defaults;
      return lhs.screenEffect == rhs.screenEffect;
    }

    bool sameAnimationEffects(const Config::Animation& before, const Config::Animation& after) {
      return before.windowsIn.effect == after.windowsIn.effect && before.windowsOut.effect == after.windowsOut.effect
          && before.windowsMove.effect == after.windowsMove.effect && before.workspaces.effect == after.workspaces.effect
          && before.overview.effect == after.overview.effect && before.scratchpad.effect == after.scratchpad.effect
          && before.border.effect == after.border.effect && before.dimUnfocused.effect == after.dimUnfocused.effect
          && before.layers.effect == after.layers.effect && before.windowsDrag == after.windowsDrag;
    }
  ```
- `ConfigEffects::between`: add
  ```cpp
      const bool effectsChanged = before.effects != after.effects
          || outputProjectionChanged(before, after, sameOutputScreenEffect)
          || !sameAnimationEffects(before.animation, after.animation)
          || before.windowRules != after.windowRules;
  ```
  and `.effects = effectsChanged,` in the returned initializer; `everything()` gets `.effects = true`; `summary()` gets `add(effects, "effects")`.

`sameOutputState` must not compare `screenEffect` (it does not: it lists fields explicitly), so `outputState` stays false on a `screen_effect` change — the test asserts this.

`src/config/resolve.cpp` (after the `if (rule.shadow)` block):
```cpp
      if (rule.borderEffect) {
        resolved.borderEffect = rule.borderEffect;
      }
      if (rule.windowEffect) {
        resolved.windowEffect = rule.windowEffect;
      }
```

- [ ] **Step 4: Parse the sections**

In `src/config/config.cpp`, inside the anonymous namespace before `parseAnimationSection`:

```cpp
    // A selector recorded while parsing and checked once every section is
    // read, so forward and cross-include references resolve.
    struct EffectReference {
      std::string context;
      std::string name;
      EffectKind kind;
      bool allowOff;
      toml::source_region source;
      std::function<void()> clear;
    };

    // Reads a string selector into `target` and records it for validation.
    void readEffectSelector(
        Section& keys, std::string_view key, std::string_view context, EffectKind kind, bool allowOff,
        std::string& target, std::vector<EffectReference>& references
    ) {
      const toml::node* node = keys.take(key);
      if (node == nullptr) {
        return;
      }
      const auto value = node->value<std::string>();
      if (!value) {
        warnAt(node->source(), "ignoring {} (expected string)", context);
        return;
      }
      target = *value;
      references.push_back({
          .context = std::string(context),
          .name = *value,
          .kind = kind,
          .allowOff = allowOff,
          .source = node->source(),
          .clear = [&target] { target.clear(); },
      });
    }

    void readEffectSelector(
        Section& keys, std::string_view key, std::string_view context, EffectKind kind, bool allowOff,
        std::optional<std::string>& target, std::vector<EffectReference>& references
    ) {
      const toml::node* node = keys.take(key);
      if (node == nullptr) {
        return;
      }
      const auto value = node->value<std::string>();
      if (!value) {
        warnAt(node->source(), "ignoring {} (expected string)", context);
        return;
      }
      target = *value;
      references.push_back({
          .context = std::string(context),
          .name = *value,
          .kind = kind,
          .allowOff = allowOff,
          .source = node->source(),
          .clear = [&target] { target.reset(); },
      });
    }

    void readEffects(Section& root, Config& loaded, std::vector<EffectReference>& references) {
      root.sub("effects", [&](Section& s) {
        Effects& effects = loaded.effects;
        s.integer("max_fps", 0, 240, effects.maxFps).boolean("in_capture", effects.inCapture);
        readEffectSelector(s, "border", "effects.border", EffectKind::Border, false, effects.border, references);
        readEffectSelector(s, "window", "effects.window", EffectKind::Window, false, effects.window, references);
        readEffectSelector(s, "screen", "effects.screen", EffectKind::Screen, false, effects.screen, references);
        readEffectSelector(s, "cursor", "effects.cursor", EffectKind::Cursor, false, effects.cursor, references);
        const toml::node* node = s.take("preset");
        if (node == nullptr) {
          return;
        }
        const auto* presets = node->as_table();
        if (presets == nullptr) {
          warnAt(node->source(), "ignoring effects.preset (expected table)");
          return;
        }
        for (const auto& [key, entry] : *presets) {
          const std::string name(key.str());
          const std::string context = "effects.preset." + name;
          const auto* table = entry.as_table();
          if (table == nullptr) {
            warnAt(entry.source(), "ignoring {} (expected table)", context);
            continue;
          }
          if (name == kEffectOff) {
            warnAt(key.source(), "ignoring {} ('off' is reserved)", context);
            continue;
          }
          Section keys(*table, context, configStore().mutableDiagnostics());
          const toml::node* kindNode = keys.take("kind");
          const std::optional<EffectKind> kind =
              kindNode != nullptr ? parseEffectKind(kindNode->value<std::string>().value_or("")) : std::nullopt;
          if (!kind) {
            warnAt(
                kindNode != nullptr ? kindNode->source() : key.source(),
                "ignoring {} (kind must be animation|border|window|screen|cursor)", context
            );
            keys.freeform();
            continue;
          }
          EffectPreset preset;
          preset.name = name;
          preset.kind = *kind;
          auto shader = readShaderSource(keys, "shader", configStore().mutableDiagnostics());
          for (auto& path : shader.watchPaths) {
            configStore().addWatchPath(std::move(path));
          }
          if (shader.source) {
            preset.shader = std::move(*shader.source);
          } else if (keys.node("shader") == nullptr) {
            warnAt(key.source(), "{} has no shader; the preset is inert", context);
          }
          keys.boolean("palette", preset.palette);
          switch (*kind) {
          case EffectKind::Border: {
            double speed = preset.speed;
            keys.integer("padding", 0, 1024, preset.padding)
                .real("speed", 0.0, 10.0, speed)
                .boolean("animated", preset.animated);
            preset.speed = static_cast<float>(speed);
            readEffectSelector(
                keys, "overlay", context + ".overlay", EffectKind::Window, false, preset.overlay, references
            );
            keys.sub("light", [&](Section& light) {
              BorderLight settings;
              double intensity = settings.intensity;
              double threshold = settings.threshold;
              light.integer("spread", 1, 256, settings.spread)
                  .real("intensity", 0.0, 4.0, intensity)
                  .real("threshold", 0.0, 1.0, threshold);
              settings.intensity = static_cast<float>(intensity);
              settings.threshold = static_cast<float>(threshold);
              preset.light = settings;
            });
            break;
          }
          case EffectKind::Cursor:
            keys.integer("radius", 0, 4096, preset.radius);
            break;
          case EffectKind::Animation:
          case EffectKind::Window:
          case EffectKind::Screen:
            break;
          }
          loaded.effects.presets.push_back(std::move(preset));
        }
      });
    }

    // Every recorded reference is checked against the final preset table.
    void validateEffectReferences(const Config& loaded, std::vector<EffectReference>& references) {
      for (EffectReference& reference : references) {
        if (const auto error = effectReferenceError(loaded.effects, reference.name, reference.kind, reference.allowOff)) {
          warnAt(reference.source, "ignoring {} ({})", reference.context, *error);
          reference.clear();
        }
      }
    }
```

Problem to handle: `readEffectSelector`'s `clear` captures a reference to `preset.overlay` while `preset` is a local that is later `std::move`d into the vector — the captured reference dangles. Fix: record preset overlay references *after* the push, by index: replace the overlay call with a deferred registration:

```cpp
            // The preset moves into the vector; register the overlay reference by index after the push.
            const toml::node* overlayNode = keys.take("overlay");
            std::optional<std::pair<std::string, toml::source_region>> overlay;
            if (overlayNode != nullptr) {
              if (const auto value = overlayNode->value<std::string>()) {
                preset.overlay = *value;
                overlay = {*value, overlayNode->source()};
              } else {
                warnAt(overlayNode->source(), "ignoring {}.overlay (expected string)", context);
              }
            }
```
and after `loaded.effects.presets.push_back(std::move(preset));`:
```cpp
          if (overlay) {
            const size_t index = loaded.effects.presets.size() - 1;
            references.push_back({
                .context = context + ".overlay",
                .name = overlay->first,
                .kind = EffectKind::Window,
                .allowOff = false,
                .source = overlay->second,
                .clear = [&loaded, index] { loaded.effects.presets[index].overlay.clear(); },
            });
          }
```
The same dangling hazard applies to window rules (`rule` is a local pushed into `loaded.windowRules`) and outputs (`rule` pushed into `loaded.outputs`): use the index form there too (`[&loaded, index] { loaded.windowRules[index].borderEffect.reset(); }`), registering after the push. `[effects]` top-level selectors and animation events live in `loaded` directly, so the reference form is safe for those.

Wire the readers:

- `parseAnimationSection(Section& s, Config::Animation& animation, std::vector<EffectReference>& references)`: in each of the nine event blocks, after `readShader(section, animation.<field>);` add `readEffectSelector(section, "effect", "animation.<event>.effect", EffectKind::Animation, false, animation.<field>.effect, references);`. Add the new block after `layers`:
  ```cpp
        s.sub("windows_drag", [&](Section& section) { section.boolean("physics", animation.windowsDrag.physics); });
  ```
  and thread `references` through `readAnimation`.
- `readOutputs`: after the `.boolean("direct_scanout", rule.directScanout);` chain, read the node into a local `std::optional<std::pair<std::string, toml::source_region>> screenEffect` (same pattern as overlay), set `rule.screenEffect`, and after `loaded.outputs.push_back(std::move(rule));` register the reference with `.context = "output." + name + ".screen_effect"`, `.kind = EffectKind::Screen`, `.allowOff = true`, and `.clear = [&loaded, index] { loaded.outputs[index].screenEffect.reset(); }`.
- `readWindowRules`: after the `.boolean("shadow", rule.shadow);` chain, the same for `border_effect` (kind Border) and `window_effect` (kind Window), both `allowOff = true`, contexts `window_rule.border_effect` / `window_rule.window_effect`, registered after the rule is pushed (find the push at the end of the rule loop; the `valid` flag drops rules — only register when the rule was pushed).
- `parseInto`: declare `std::vector<EffectReference> effectReferences;` before `Section root(...)`, pass it to `readAnimation`, `readOutputs`, `readWindowRules`, add `readEffects(root, loaded, effectReferences);` right after `readColors(root, loaded);`, and after `warnScrollButtonBinds(loaded);` add `validateEffectReferences(loaded, effectReferences);` (still inside the block so the root `Section` is alive).

Add `#include <functional>` to config.cpp.

- [ ] **Step 5: Run the tests**

Run: `nix develop . --command bash -c 'just test'`
Expected: the six new `config-load` tests, the `config-resolve` test, and `packagedAnimationDefaultsMatchCompiledDefaults` pass; everything else stays green.

- [ ] **Step 6: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(config): effect presets, selectors, and post-load reference validation"
```

---

### Task 2.3: Duplicate presets across files are an Error

**Files:**
- Modify: `src/config/config_merge.h` (`MergeResult`), `src/config/config_merge.cpp` (`expandFile` :279-341)
- Test: `tests/unit/config_load.cpp`

**Interfaces:**
- Produces: `MergeResult::presetFiles` (`std::map<std::string, std::string>`, preset name → defining file path as written by `path.string()`).

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/config_load.cpp`:

```cpp
UMBRIEL_TEST(duplicateEffectPresetsAcrossIncludesAreRejected) {
  const TempConfigTree tree;
  tree.write("a.glsl", "vec4 border(vec2 uv) { return umbriel_sample(uv); }");
  tree.write("theme.toml", "[effects.preset.ring]\nkind = \"border\"\nshader = \"a.glsl\"\n");
  tree.write("config.toml", "[include]\nfiles = [\"theme.toml\"]\n[effects]\nborder = \"ring\"\n");
  ConfigStore& store = umbriel::configStore();
  store.setRootPath(tree.path("config.toml"), true);
  CHECK(store.reload().success);
  const uint64_t generation = store.generation();
  const umbriel::Config previous = store.config();

  tree.write(
      "config.toml",
      "[include]\nfiles = [\"theme.toml\"]\n[effects]\nborder = \"ring\"\n"
      "[effects.preset.ring]\nkind = \"border\"\nshader = \"a.glsl\"\npadding = 4\n"
  );
  const auto duplicate = store.reload();
  CHECK(!duplicate.success);
  CHECK(containsDiagnostic(store, "effects.preset.ring is also defined in"));
  CHECK(containsDiagnostic(store, "theme.toml"));
  CHECK(store.config() == previous);
  CHECK_EQ(store.generation(), generation);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `nix develop . --command bash -c 'meson test -C build-debug config-load --print-errorlogs 2>&1 | grep -A3 duplicateEffectPresets'`
Expected: FAIL — the second load succeeds (TOML tables merge silently).

- [ ] **Step 3: Record preset origins in `expandFile`**

`src/config/config_merge.h`: add to `MergeResult`:
```cpp
    // Defining file of every effects.preset.<name> table, for duplicate detection across files.
    std::map<std::string, std::string> presetFiles;
```
(add `#include <map>`).

`src/config/config_merge.cpp`, in `expandFile` after the file's own `[include]` targets have been expanded and before its `deepMerge` (an included file registers its presets first, so the including file's duplicate is the one reported, naming the included file):

```cpp
  // A preset defined in two files would merge key by key into one table. Refuse
  // it, naming both files, before deepMerge can hide the second definition.
  if (const auto* effects = parsed.get_as<toml::table>("effects")) {
    if (const auto* presets = effects->get_as<toml::table>("preset")) {
      for (const auto& [key, value] : *presets) {
        const std::string name(key.str());
        const auto [origin, inserted] = result.presetFiles.try_emplace(name, path.string());
        if (!inserted) {
          emit(
              result, ConfigDiagnostic::Severity::Error, &key.source(),
              std::format("effects.preset.{} is also defined in {}", name, origin->second)
          );
        }
      }
    }
  }
```
`recordPresetOrigins` records the conflict as a `Severity::Error` diagnostic in `MergeResult` without setting `hadError`; `parseInto` reports it through the `errorAt` path, so startup continues with defaults and a banner (`DefaultsAllowed`, as for duplicate device and workspace names) and a reload keeps the previous configuration.

- [ ] **Step 4: Run the tests and `validate-config`**

Run: `nix develop . --command bash -c 'just test'`
Expected: `duplicateEffectPresetsAcrossIncludesAreRejected` passes; `validate-config` passes.

- [ ] **Step 5: Document in `docs/user/configuration.md`**

In the `## Include` section, change "Duplicate device or workspace selectors are still errors when they come from different files." to "Duplicate device or workspace selectors, and effect presets defined in two files, are still errors when they come from different files."

- [ ] **Step 6: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(config): reject effect presets defined in two files"
```

---

### Task 2.4: `EffectLedger` (pure) and `EffectRegistry`

**Files:**
- Create: `src/scene/effect_ledger.h`, `src/scene/effect_registry.h`, `src/scene/effect_registry.cpp`
- Modify: `src/scene/animation_shader.h`, `src/scene/animation_shader.cpp`, `src/server/server.h:145-200,540-560`, `src/server/server.cpp:334,651`, `src/server/server_events.cpp:498-500,748`, `meson.build` (`core_sources`)
- Test: `tests/unit/effects.cpp` (ledger cases)

**Interfaces:**
- Produces: `EffectLedger`, `EffectRegistry`, `effectRegistry()` exactly as the index glossary. Stage-2 scope of the registry: `prepare`, `clear`, `renderer`, `preset`, `presetConfig`, `animationShader`, `lifecycleShader`, `fillTimeUniforms`, `active`, `ledger`, `setSuspended`. (`deformationShader`, `applyOutputEffects`, `pointerMoved`, `cursorEffectActive` come in Stages 6-7.)
- Consumes: `fx_effect_shader_create/unref/set_shape_preserving`, `config()`, `Config::effects`, `effectPalette`.

- [ ] **Step 1: Write the failing ledger tests**

Append to `tests/unit/effects.cpp` (before `main`):

```cpp
#include "scene/effect_ledger.h"

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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `nix develop . --command bash -c 'meson compile -C build-debug effects-test 2>&1 | tail -3'`
Expected: compile error, `scene/effect_ledger.h` not found.

- [ ] **Step 3: Write the ledger**

Create `src/scene/effect_ledger.h`:

```cpp
#pragma once

#include <algorithm>
#include <vector>

namespace umbriel {

  // What decides whether an effect instance asks for frames: it is drawn on an
  // output, its program reads umbriel_time, and its clock is advancing. Frozen
  // snapshots and effects with `animated = false` or `speed = 0` arrive with
  // advancing = false.
  struct EffectInstanceState {
    const void* output = nullptr;
    bool visible = false;
    bool readsTime = false;
    bool advancing = false;
  };

  // Per-output counts hot paths check before any lookup. Owners are opaque
  // identities (a View, an output effect), so this stays free of scene types.
  class EffectLedger {
  public:
    void update(const void* owner, const EffectInstanceState& state) {
      const auto entry = std::ranges::find(m_entries, owner, &Entry::owner);
      if (entry == m_entries.end()) {
        m_entries.push_back({owner, state});
      } else {
        entry->state = state;
      }
    }
    void remove(const void* owner) { std::erase_if(m_entries, [owner](const Entry& entry) { return entry.owner == owner; }); }
    void removeOutput(const void* output) {
      std::erase_if(m_entries, [output](const Entry& entry) { return entry.state.output == output; });
    }
    void setSuspended(bool suspended) { m_suspended = suspended; }
    [[nodiscard]] bool suspended() const { return m_suspended; }
    // Instances on `output` that need effect-only frames right now.
    [[nodiscard]] unsigned eligible(const void* output) const {
      if (m_suspended) {
        return 0;
      }
      return static_cast<unsigned>(std::ranges::count_if(m_entries, [output](const Entry& entry) {
        return entry.state.output == output && entry.state.visible && entry.state.readsTime && entry.state.advancing;
      }));
    }
    // Owners carrying a persistent effect at all, visible or not.
    [[nodiscard]] unsigned active() const { return static_cast<unsigned>(m_entries.size()); }

  private:
    struct Entry {
      const void* owner;
      EffectInstanceState state;
    };
    std::vector<Entry> m_entries;
    bool m_suspended = false;
  };

} // namespace umbriel
```

Run: `nix develop . --command bash -c 'meson test -C build-debug effects --print-errorlogs'` — Expected: the three ledger tests pass.

- [ ] **Step 4: Write the registry**

Create `src/scene/effect_registry.h`:

```cpp
#pragma once

#include "config/effects.h"
#include "scene/animation_shader.h"
#include "scene/effect_ledger.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>

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
    // umbriel_time (animation clock seconds x speed, 0 when frozen) when `shader` reads it, and for palette presets the
    // [colors] palette. A program that ignores time never gets a changing uniform, so it never damages per frame.
    void fillTimeUniforms(
        fx_animation_parameters& parameters, float seconds, const EffectPreset& preset, const fx_effect_shader* shader
    ) const;

    [[nodiscard]] bool active() const { return m_ledger.active() > 0; }
    [[nodiscard]] EffectLedger& ledger() { return m_ledger; }
    void setSuspended(bool suspended) { m_ledger.setSuspended(suspended); }

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
  };

  // The Server's registry. Set in Server's constructor before any view exists.
  [[nodiscard]] EffectRegistry& effectRegistry();

} // namespace umbriel
```

Create `src/scene/effect_registry.cpp`:

```cpp
#include "scene/effect_registry.h"

#include "config/config.h"
#include "core/log.h"
#include "server/server.h"

#include <algorithm>

extern "C" {
#include <umbrielfx/render/effect.h>
}

namespace umbriel {
  namespace {
    constexpr Logger kLog("effects");
    EffectRegistry* s_registry = nullptr;

    // Entering transitions fade in with progress and leaving ones fade out. Only alpha changes, uniformly, so the
    // window's shape and its analytic shadow are unaffected.
    constexpr const char* kBuiltinFade = R"(vec4 animation(vec2 uv) {
    float alpha = umbriel_direction < 0.0 ? 1.0 - umbriel_clamped_progress : umbriel_clamped_progress;
    return umbriel_sample(uv) * alpha;
})";

    // The config event an animation slot binds through `effect =`, or null for slots without one.
    struct EventBinding {
      const std::string* effect = nullptr;
      bool enabled = false;
      const char* name = "";
    };
    EventBinding eventBinding(const Config::Animation& settings, AnimationEvent event) {
      switch (event) {
      case AnimationEvent::DimUnfocused:
        return {&settings.dimUnfocused.effect, settings.dimUnfocused.enabled, "dim_unfocused"};
      case AnimationEvent::Border:
        return {&settings.border.effect, settings.border.enabled, "border"};
      case AnimationEvent::WindowsMove:
        return {&settings.windowsMove.effect, settings.windowsMove.enabled, "windows_move"};
      case AnimationEvent::WindowsIn:
        return {&settings.windowsIn.effect, settings.windowsIn.enabled, "windows_in"};
      case AnimationEvent::WindowsOut:
        return {&settings.windowsOut.effect, settings.windowsOut.enabled, "windows_out"};
      case AnimationEvent::Scratchpad:
        return {&settings.scratchpad.effect, settings.scratchpad.enabled, "scratchpad"};
      case AnimationEvent::Layers:
        return {&settings.layers.effect, settings.layers.enabled, "layers"};
      case AnimationEvent::Workspaces:
        return {&settings.workspaces.effect, settings.workspaces.enabled, "workspaces"};
      case AnimationEvent::Overview:
        return {&settings.overview.effect, settings.overview.enabled, "overview"};
      case AnimationEvent::Window:
      case AnimationEvent::Overlay:
      case AnimationEvent::BorderEffect:
      case AnimationEvent::Drag:
        return {};
      }
      return {};
    }

    fx_effect_kind toFxKind(EffectKind kind) {
      switch (kind) {
      case EffectKind::Animation:
        return FX_EFFECT_ANIMATION;
      case EffectKind::Border:
        return FX_EFFECT_BORDER;
      case EffectKind::Window:
        return FX_EFFECT_WINDOW;
      case EffectKind::Screen:
        return FX_EFFECT_SCREEN;
      case EffectKind::Cursor:
        return FX_EFFECT_CURSOR;
      }
      return FX_EFFECT_ANIMATION;
    }
  } // namespace

  EffectRegistry& effectRegistry() { return *s_registry; }

  EffectRegistry::EffectRegistry(Server& server) : m_server(&server) { s_registry = this; }

  EffectRegistry::~EffectRegistry() {
    clear();
    if (s_registry == this) {
      s_registry = nullptr;
    }
  }

  void EffectRegistry::clear() {
    m_programs.clear();
    m_builtinFade.reset();
    m_renderer = nullptr;
  }

  void EffectRegistry::referencedNames(std::vector<std::string>& names) const {
    const Config& settings = config();
    const auto add = [&](std::string_view name) {
      if (!name.empty() && name != kEffectOff && std::ranges::find(names, name) == names.end()) {
        names.emplace_back(name);
      }
    };
    add(settings.effects.border);
    add(settings.effects.window);
    add(settings.effects.screen);
    add(settings.effects.cursor);
    for (const WindowRule& rule : settings.windowRules) {
      add(rule.borderEffect.value_or(""));
      add(rule.windowEffect.value_or(""));
    }
    for (const OutputRule& rule : settings.outputs) {
      add(rule.screenEffect.value_or(""));
    }
    for (unsigned slot = 0; slot < FX_ANIMATION_SLOTS; ++slot) {
      const EventBinding binding = eventBinding(settings.animation, static_cast<AnimationEvent>(slot));
      if (binding.effect != nullptr) {
        add(*binding.effect);
      }
    }
    // A referenced border preset pulls its overlay in.
    for (size_t i = 0; i < names.size(); ++i) {
      if (const EffectPreset* preset = findEffectPreset(settings.effects, names[i])) {
        add(preset->overlay);
      }
    }
  }

  void EffectRegistry::compile(const EffectPreset& preset) {
    Entry& entry = m_programs[preset.name];
    if (entry.shader != nullptr && entry.kind == preset.kind && entry.code == preset.shader.code) {
      return;
    }
    entry.kind = preset.kind;
    entry.code = preset.shader.code;
    entry.shader.reset();
    if (preset.inert()) {
      return;
    }
    const std::string label = preset.shader.file.empty() ? "effects.preset." + preset.name : preset.shader.file.string();
    entry.shader = {
        fx_effect_shader_create(m_renderer, toFxKind(preset.kind), preset.shader.code.c_str(), label.c_str()),
        fx_effect_shader_unref
    };
    if (entry.shader == nullptr) {
      kLog.error("effect preset '{}' ({}) failed to compile; rendering plainly", preset.name, effectKindName(preset.kind));
    }
  }

  void EffectRegistry::prepare(wlr_renderer* renderer) {
    if (renderer != m_renderer) {
      // Programs belong to one GL context. A new renderer starts from nothing.
      m_programs.clear();
      m_builtinFade.reset();
      m_renderer = renderer;
    }
    const Config& settings = config();
    std::vector<std::string> names;
    referencedNames(names);
    std::erase_if(m_programs, [&](const auto& item) { return std::ranges::find(names, item.first) == names.end(); });
    for (const std::string& name : names) {
      if (const EffectPreset* preset = findEffectPreset(settings.effects, name)) {
        compile(*preset);
      }
    }
    // Slide keeps per-buffer alpha: its opacity curve differs from the lifecycle progress.
    const bool fadeNeeded = settings.animation.enabled
        && ((settings.animation.windowsIn.enabled && settings.animation.windowsIn.style != "slide")
            || (settings.animation.windowsOut.enabled && settings.animation.windowsOut.style != "slide"));
    if (fadeNeeded && m_builtinFade == nullptr) {
      m_builtinFade = {
          fx_effect_shader_create(m_renderer, FX_EFFECT_ANIMATION, kBuiltinFade, "animation.builtin_fade"),
          fx_effect_shader_unref
      };
      fx_effect_shader_set_shape_preserving(m_builtinFade.get(), true);
    } else if (!fadeNeeded) {
      m_builtinFade.reset();
    }
  }

  fx_effect_shader* EffectRegistry::preset(std::string_view name, EffectKind kind) const {
    const auto entry = m_programs.find(name);
    return entry != m_programs.end() && entry->second.kind == kind ? entry->second.shader.get() : nullptr;
  }

  const EffectPreset* EffectRegistry::presetConfig(std::string_view name) const {
    return findEffectPreset(config().effects, name);
  }

  fx_effect_shader* EffectRegistry::animationShader(AnimationEvent event) const {
    const Config::Animation& settings = config().animation;
    const EventBinding binding = eventBinding(settings, event);
    if (binding.effect == nullptr || !settings.enabled || !binding.enabled || binding.effect->empty()) {
      return nullptr;
    }
    return preset(*binding.effect, EffectKind::Animation);
  }

  fx_effect_shader* EffectRegistry::lifecycleShader(AnimationEvent event) const {
    if (fx_effect_shader* custom = animationShader(event)) {
      return custom;
    }
    const Config::Animation& settings = config().animation;
    const bool builtin = settings.enabled
        && ((event == AnimationEvent::WindowsIn && settings.windowsIn.enabled && settings.windowsIn.style != "slide")
            || (event == AnimationEvent::WindowsOut && settings.windowsOut.enabled
                && settings.windowsOut.style != "slide"));
    return builtin ? m_builtinFade.get() : nullptr;
  }

  void EffectRegistry::fillTimeUniforms(
      fx_animation_parameters& parameters, float seconds, const EffectPreset& preset, const fx_effect_shader* shader
  ) const {
    if (fx_effect_shader_reads(shader, "umbriel_time")) {
      if (fx_uniform* time = fx_parameters_add_uniform(&parameters, "umbriel_time", FX_UNIFORM_FLOAT, 1)) {
        time->floats[0] = seconds;
      }
    }
    if (!preset.palette) {
      return;
    }
    const auto palette = effectPalette(config().colors);
    if (fx_uniform* colors = fx_parameters_add_uniform(&parameters, "umbriel_palette", FX_UNIFORM_VEC4, 4)) {
      for (size_t i = 0; i < palette.size(); ++i) {
        std::ranges::copy(palette[i], &colors->floats[i * 4]);
      }
    }
    if (fx_uniform* count = fx_parameters_add_uniform(&parameters, "umbriel_palette_count", FX_UNIFORM_INT, 1)) {
      count->ints[0] = static_cast<int32_t>(palette.size());
    }
  }

} // namespace umbriel
```

Wire the scene adapter and Server:

- `src/scene/animation_shader.cpp`: `animationShader(renderer, event)` becomes: try `effectRegistry().animationShader(event)` first; if null, fall through to the existing legacy per-slot `shader` cache (unchanged until Stage 3). `lifecycleShader`: `if (auto* custom = animationShader(renderer, event)) return custom; return effectRegistry().lifecycleShader(event);` — and delete the local `builtinFade`/`builtinFadeShader` (the registry owns the fade). `prepareAnimationShaders(renderer)`: `effectRegistry().prepare(renderer); for each slot (void)lifecycleShader(renderer, slot);` (the loop keeps the legacy cache warm). `clearAnimationShaderCache()`: `cache = {}; effectRegistry().clear();`.
- `src/server/server.h`: `#include "scene/effect_registry.h"`, add `[[nodiscard]] EffectRegistry& effects() { return m_effects; }` (and a `const` overload) next to `cursor()`, and an embedded member `EffectRegistry m_effects;` placed just after `std::unique_ptr<WineColorManager> m_wineColorManager;` (before `m_outputs` and the view registry, so it outlives views). Embedding rather than `unique_ptr` keeps the no-effects path free of any allocation the current code does not make; the registry's maps and vectors stay empty until a preset is referenced.
- `src/server/server.cpp` constructor initializer list: `m_effects(*this)` (the constructor only stores the pointer and installs the `effectRegistry()` accessor). At `:334` keep `prepareAnimationShaders(m_renderer);` — the free function now prepares the registry.
- `src/server/server.cpp:651`: `clearAnimationShaderCache();` stays (it clears the registry before `wlr_renderer_destroy`); the member's destructor then runs on already-cleared state.
- The built-in fade keeps its `std::shared_ptr` with `fx_effect_shader_unref` as deleter exactly as `src/scene/animation_shader.cpp:30-32` holds it today; that control block is not new.
- `src/server/server_events.cpp:498`: `if (effects.animation || effects.effects) { prepareAnimationShaders(m_renderer); }`.
- `src/server/server_events.cpp:748`: unchanged (`prepareAnimationShaders(m_renderer)` prepares the registry for the new renderer).
- `meson.build` `core_sources`: add `'src/scene/effect_registry.cpp',` after `'src/scene/animation_shader.cpp',`.
- Includes: `src/scene/animation_shader.cpp` adds `#include "scene/effect_registry.h"`; `server.cpp`/`server_events.cpp` add it too.

- [ ] **Step 5: Build, unit tests, animation checks**

Run: `nix develop . --command bash -c 'just build && just test && just check 18 19 20 330 600'`
Expected: all pass. 600 confirms recovery still re-prepares (the registry is re-prepared through `prepareAnimationShaders`).

- [ ] **Step 6: A harness smoke check for the registry**

Append to `tests/harness/checks/180_animation_shaders.sh` nothing; instead confirm manually that a referenced preset compiles at startup and an unreferenced one does not:

```bash
cd /home/barrulus/dev/umbriel && nix develop . --command bash -c '
cat > /tmp/effects-smoke.toml <<EOF
[general]
xwayland = false
[effects.preset.used]
kind = "screen"
shader = "/tmp/used.glsl"
[effects.preset.unused]
kind = "screen"
shader = "/tmp/unused.glsl"
[effects]
screen = "used"
EOF
echo "vec4 screen(vec2 uv) { return umbriel_sample(uv); }" > /tmp/used.glsl
echo "this is not GLSL" > /tmp/unused.glsl
WLR_BACKENDS=headless WLR_LOG=debug timeout 3 ./build-debug/umbriel -c /tmp/effects-smoke.toml 2>&1 | grep -E "Compiling screen shader|rejected" '
```
Expected: one `Compiling screen shader: /tmp/used.glsl` line and no `rejected` line (the unreferenced broken preset never compiles). (Adjust `-c` to the CLI's config flag if it differs: `./build-debug/umbriel --help`.)

- [ ] **Step 7: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(scene): effect registry compiles referenced presets per renderer"
```

---

### Task 2.5: Stage gate

- [ ] **Step 1: Full verification**

Run: `cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'just format && git diff --exit-code && just lint && just test && just check'`
Expected: clean format, lint, unit tests, and harness (known flakes only).
