# Effects Stage 3: Animation migration to `effect =`

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove `[animation.<event>] shader`, route every animation event through `effect = "<name>"` and the registry, move the two bundled animation shaders to `examples/effects/animation/<name>/`, migrate every harness check, and assert that effects rebind after renderer recovery.

**Architecture:** The scene adapter (`src/scene/animation_shader.cpp`) becomes a thin wrapper over `EffectRegistry`; the legacy per-slot cache and `AnimationShaderSource` members disappear. Timing, curves, enable switches, retargeting, and snapshots are untouched: the only change is where the program comes from.

**Tech Stack:** C++23, bash harness, python3 (one-shot migration script kept in the scratchpad, not committed).

**Spec:** `docs/superpowers/specs/2026-09-25-effects-design.md` §1 ("`[animation.<event>] shader` is removed"), §4 (`animation` behaviour), §6 (existing checks, examples). Index: `2026-09-25-effects-00-index.md`.

## Global Constraints

- **Reuse first:** Before adding a utility, helper, or method, inspect existing implementations and callers. Reuse or narrowly extend the owning helper; if none fits, record why in this plan and give shared behavior one owner.
- **Comments and documentation (maintainer policy):** Comments and documentation explain what the code currently does and any non-obvious constraint a reader needs. Do not narrate history, migrations, rejected alternatives, or "why we don't do X"; git holds that. This applies to Markdown, `meson.build`, and code comments alike. Keep them short; if a comment is longer than the code it describes, cut it down. The comments in this plan's snippets are written to that policy and can be kept; prose in the plan that explains a decision is for the executor, not for the code.
- **Directed reuse for this stage:** Keep the existing animation adapter, transition identities, lifecycle fallback, and snapshot machinery. Put shared preset/time/palette binding in one helper used by every animation attachment path, including close snapshots; do not implement it only in the live-view adapter.
- **Every task gate:** Before marking a task complete or committing, audit its diff for duplicate helpers and for comments or doc text that narrate history, migrations, or alternatives, or that outgrow the code they describe.

See the index. Stage-specific:

- `shader` under `[animation.<event>]` is reported as `unknown key animation.<event>.shader` (ordinary `Section` diagnostic) and does nothing.
- Running events keep their program; reloads affect the next event (unchanged scene behaviour — `wlr_scene_node_set_animation` keeps the previous program for the same `transition_id`).
- `windows_in`/`windows_out` without an effect keep the built-in fade.
- Examples ship as `examples/effects/animation/{reveal,squash}/{shader.glsl,effect.toml}` installed to `share/umbriel/effects/animation/<name>/`. Each `effect.toml` defines its preset and selects nothing.

---

### Task 3.1: Remove the legacy `shader` key and the per-slot cache

**Files:**
- Modify: `src/config/config.h:561-640` (drop the nine `std::optional<ShaderSource> shader;` members), `src/config/config.cpp:1096-1102` (delete the `readShader` lambda and its nine calls), `src/scene/animation_shader.h`, `src/scene/animation_shader.cpp`, `tests/unit/config_load.cpp:2677-2724`, `docs/design/animation-shaders.md:6-31` (one paragraph)
- Test: `tests/unit/config_load.cpp`

**Interfaces:**
- Consumes: `effectRegistry().animationShader(event)`, `lifecycleShader(event)`, `prepare(renderer)`, `clear()`.
- Produces: `src/scene/animation_shader.h` keeps its public functions (`animationShader`, `lifecycleShader`, `prepareAnimationShaders`, `clearAnimationShaderCache`, `updateAnimationShader` x2) with unchanged signatures.

- [ ] **Step 1: Rewrite the config test**

Replace `animationShadersResolveIncludedFilesAcrossAllEventsAndTrackContentChanges` (`tests/unit/config_load.cpp:2677-2724`) with:

```cpp
UMBRIEL_TEST(animationEffectsResolveIncludedPresetsAcrossAllEventsAndTrackContentChanges) {
  const TempConfigTree tree;
  const std::array sections{"windows_in", "windows_out", "windows_move",  "workspaces", "overview",
                            "scratchpad", "border",      "dim_unfocused", "layers"};
  std::string theme = "[effects.preset.reveal]\nkind = 'animation'\nshader = 'effect.glsl'\n";
  for (const char* section : sections) {
    theme += std::format("[animation.{}]\neffect = 'reveal'\n", section);
  }
  tree.write("config.toml", "[include]\nfiles = ['theme/animation.toml']\n");
  tree.write("theme/animation.toml", theme);
  tree.write("theme/effect.glsl", "first shader");
  tree.write("effect.glsl", "wrong source directory");
  ConfigStore& store = umbriel::configStore();
  store.setRootPath(tree.path("config.toml"), true);
  CHECK(store.reload().success);
  const auto& animation = store.config().animation;
  const std::array effects{&animation.windowsIn.effect,  &animation.windowsOut.effect,   &animation.windowsMove.effect,
                           &animation.workspaces.effect, &animation.overview.effect,     &animation.scratchpad.effect,
                           &animation.border.effect,     &animation.dimUnfocused.effect, &animation.layers.effect};
  for (const auto* effect : effects) {
    CHECK_EQ(*effect, std::string("reveal"));
  }
  const umbriel::EffectPreset* reveal = umbriel::findEffectPreset(store.config().effects, "reveal");
  CHECK(reveal != nullptr);
  if (reveal != nullptr) {
    CHECK_EQ(reveal->shader.code, std::string("first shader"));
    CHECK(reveal->shader.file == tree.path("theme/effect.glsl"));
  }
  CHECK(!containsDiagnostic(store, "unknown key"));
  CHECK_EQ(std::ranges::count(store.watchPaths(), tree.path("theme/effect.glsl")), 1);

  tree.write("theme/effect.glsl", "edited shader");
  const auto edited = store.reload();
  CHECK(edited.success);
  CHECK(edited.effects.effects);
  CHECK(!edited.effects.animation);
  reveal = umbriel::findEffectPreset(store.config().effects, "reveal");
  CHECK(reveal != nullptr && reveal->shader.code == "edited shader");

  tree.write("theme/replacement.glsl", "replacement shader");
  tree.write(
      "theme/animation.toml",
      "[effects.preset.reveal]\nkind = 'animation'\nshader = 'replacement.glsl'\n[animation.windows_in]\neffect = 'reveal'\n"
  );
  CHECK(store.reload().success);
  CHECK(std::ranges::find(store.watchPaths(), tree.path("theme/effect.glsl")) == store.watchPaths().end());
  CHECK(store.config().animation.layers.effect.empty());
}

UMBRIEL_TEST(removedAnimationShaderKeyIsUnknown) {
  const TempConfig file;
  ConfigStore& store = umbriel::configStore();
  store.setRootPath(file.path(), true);
  file.write("[animation.windows_in]\nshader = \"reveal.glsl\"\n");
  CHECK(store.reload().success);
  CHECK(containsDiagnostic(store, "unknown key animation.windows_in.shader"));
  CHECK(store.config().animation.windowsIn.effect.empty());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'meson test -C build-debug config-load --print-errorlogs 2>&1 | grep -E "removedAnimationShaderKey|FAIL"'`
Expected: `removedAnimationShaderKeyIsUnknown` FAILs (`shader` is still claimed).

- [ ] **Step 3: Delete the legacy path**

- `src/config/config.h`: remove the nine `std::optional<ShaderSource> shader;` members.
- `src/config/config.cpp`: delete the `readShader` lambda (`:1096-1102`) and the nine `readShader(section, animation.<field>);` lines.
- `src/scene/animation_shader.cpp`: delete `CacheEntry`, `cache`, `kBuiltinFade`, `BuiltinEntry`, `builtinFade`, `builtinFadeShader`, and the `EVENT` macro switch. The file becomes:

```cpp
#include "scene/animation_shader.h"

#include "scene/effect_registry.h"

#include <algorithm>

extern "C" {
#include <umbrielfx/render/effect.h>
}

namespace umbriel {
  namespace {
    static_assert(static_cast<unsigned>(AnimationEvent::Overview) + 1 == FX_ANIMATION_SLOTS);
    static_assert(static_cast<unsigned>(AnimationEvent::Window) == FX_SLOT_WINDOW);
    static_assert(static_cast<unsigned>(AnimationEvent::BorderEffect) == FX_SLOT_BORDER_EFFECT);
    static_assert(static_cast<unsigned>(AnimationEvent::Drag) == FX_SLOT_DRAG);
    static_assert(static_cast<unsigned>(AnimationEvent::WindowsIn) == FX_SLOT_WINDOWS_IN);
    static_assert(static_cast<unsigned>(AnimationEvent::WindowsOut) == FX_SLOT_WINDOWS_OUT);

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
      EffectRegistry& registry = effectRegistry();
      fx_effect_shader* shader = value.animating() ? lifecycleShader(renderer, event) : nullptr;
      // A preset bound to this event gets the shared uniforms too: umbriel_time (when it reads it) and, for
      // palette = true, the [colors] palette. The built-in fade has no preset and reads neither.
      if (shader != nullptr) {
        if (const EffectPreset* preset = registry.animationPreset(event)) {
          registry.fillTimeUniforms(parameters, registry.clockSeconds(), *preset, shader);
        }
      }
      wlr_scene_node_set_animation(node, static_cast<unsigned>(event), shader, &parameters);
    }
  } // namespace

  fx_effect_shader* animationShader(wlr_renderer* /*renderer*/, AnimationEvent event) {
    return effectRegistry().animationShader(event);
  }

  fx_effect_shader* lifecycleShader(wlr_renderer* /*renderer*/, AnimationEvent event) {
    return effectRegistry().lifecycleShader(event);
  }

  void prepareAnimationShaders(wlr_renderer* renderer) { effectRegistry().prepare(renderer); }

  void clearAnimationShaderCache() { effectRegistry().clear(); }

  // updateAnimationShader overloads: unchanged from the current file.
} // namespace umbriel
```
(Keep the two `updateAnimationShader` definitions exactly as they are today.) Update the header comments in `src/scene/animation_shader.h` to say "The preset bound to `event` through `effect =`, or null" and "the registry compiles on preparation, never here". The `renderer` parameter stays so call sites do not churn; the registry asserts nothing about it.

Two small registry additions (`src/scene/effect_registry.h/.cpp`) that the adapter uses:
```cpp
    // The preset bound to an animation event through `effect =`, or null (also null for the built-in fade).
    [[nodiscard]] const EffectPreset* animationPreset(AnimationEvent event) const;
    // The animation clock in seconds, read only when a program needs it.
    [[nodiscard]] float clockSeconds() const;
```
```cpp
  const EffectPreset* EffectRegistry::animationPreset(AnimationEvent event) const {
    const EventBinding binding = eventBinding(config().animation, event);
    return binding.effect != nullptr && !binding.effect->empty() ? findEffectPreset(config().effects, *binding.effect) : nullptr;
  }

  float EffectRegistry::clockSeconds() const { return static_cast<float>(m_server->animationClockMsec()) / 1000.0F; }
```
The clock is read only inside `if (shader != nullptr)` with a preset bound, so a configuration without presets never reads it here (spec §5).

- `docs/design/animation-shaders.md:6-14`: replace the first paragraph of "Configuration and compilation" with: "Animation events bind a preset by name: `[animation.<event>] effect = "<name>"` must name an `[effects.preset.<name>]` with `kind = "animation"`. `readShaderSource` (`src/config/effects.cpp`) reads the preset's `shader` path: inline GLSL is not accepted, paths resolve relative to the declaring TOML file including included files, missing files stay watched, contents take part in configuration equality, and blank/NUL text, nonregular files, and inputs larger than 256 KiB are rejected. Nonblocking opens prevent FIFOs hanging config reload." And the second paragraph's first sentence becomes "`EffectRegistry` (`src/scene/effect_registry.cpp`) caches one program per referenced preset, kind, exact source, and renderer."

- [ ] **Step 4: Build and unit tests**

Run: `nix develop . --command bash -c 'just build && just test'`
Expected: both new tests pass; `shader-source` still passes (it tests the reader through `Section`, independent of the animation section).

- [ ] **Step 5: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(animation): bind animation events to presets through effect ="
```
(The harness is red between this commit and Task 3.3; the stage commit sequence is fine because Task 3.3 lands in the same push, but do not run the full `just check` until then.)

---

### Task 3.2: Move `reveal` and `squash` to `examples/effects/animation/`

**Files:**
- Move: `examples/shaders/reveal.glsl` → `examples/effects/animation/reveal/shader.glsl`, `examples/shaders/squash.glsl` → `examples/effects/animation/squash/shader.glsl`
- Create: `examples/effects/animation/reveal/effect.toml`, `examples/effects/animation/squash/effect.toml`
- Modify: `meson.build:712-716` (the shaders `install_data` block)

- [ ] **Step 1: Move the files and write the preset tables**

```bash
cd /home/barrulus/dev/umbriel
mkdir -p examples/effects/animation/reveal examples/effects/animation/squash
git mv examples/shaders/reveal.glsl examples/effects/animation/reveal/shader.glsl
git mv examples/shaders/squash.glsl examples/effects/animation/squash/shader.glsl
rmdir examples/shaders 2>/dev/null || true
```

`examples/effects/animation/reveal/effect.toml`:
```toml
# Wipe the window in from the left while opening, out to the left while closing.
# Include this file, then select it: [animation.windows_in] effect = "reveal"
[effects.preset.reveal]
kind = "animation"
shader = "shader.glsl"
```

`examples/effects/animation/squash/effect.toml`:
```toml
# A restrained squash-and-settle for window movement and resizing.
# Include this file, then select it: [animation.windows_move] effect = "squash"
[effects.preset.squash]
kind = "animation"
shader = "shader.glsl"
```

`meson.build:712-716`: replace the shaders block with a loop over effect directories so Stage 8 only extends the list:

```meson
# Bundled effect presets: each directory holds the preset's shader and an
# effect.toml that defines it without selecting it. Users include the TOML and
# select the name. Kept as data, not embedded: they are examples to copy.
foreach effect : ['animation/reveal', 'animation/squash']
  install_data(
    'examples/effects' / effect / 'shader.glsl',
    'examples/effects' / effect / 'effect.toml',
    install_dir: get_option('datadir') / 'umbriel' / 'effects' / effect,
  )
endforeach
```

- [ ] **Step 2: Verify the install layout**

Run: `nix develop . --command bash -c 'just configure >/dev/null && DESTDIR=/tmp/umbriel-install meson install -C build-debug --no-rebuild >/dev/null && find /tmp/umbriel-install -path "*share/umbriel/effects*" | sort'`
Expected:
```
.../share/umbriel/effects
.../share/umbriel/effects/animation
.../share/umbriel/effects/animation/reveal
.../share/umbriel/effects/animation/reveal/effect.toml
.../share/umbriel/effects/animation/reveal/shader.glsl
.../share/umbriel/effects/animation/squash
.../share/umbriel/effects/animation/squash/effect.toml
.../share/umbriel/effects/animation/squash/shader.glsl
```
and no `share/umbriel/shaders`. Then `rm -rf /tmp/umbriel-install`.

- [ ] **Step 3: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "build(examples): ship bundled animation presets under share/umbriel/effects"
```

---

### Task 3.3: Migrate every harness check from `shader =` to presets

**Files:**
- Modify: the 24 checks listed in the harness survey (`171, 180, 181, 182, 183_animation_shader_lifetime, 184, 185, 186, 187, 188, 189, 190, 192, 193, 195, 196, 198, 200, 202_tiled_close_shader_visibility, 205_tiled_close_configure_barrier, 206_tiled_close_fixed_snapshot, 207, 208_dwindle_many_close_timing, 330`)

- [ ] **Step 1: Run the one-shot rewrite**

Save this to the scratchpad (not the repository) as `migrate_shader_keys.py` and run it over `tests/harness/checks/*.sh`:

```python
#!/usr/bin/env python3
# Rewrites `[animation.<event>] ... shader = X` into a preset table placed
# before the section header plus `effect = "<name>"` in the section.
import pathlib
import re
import sys

HEADER = re.compile(r'^\[animation\.([A-Za-z0-9_$]+)\]\s*$')
STOP = re.compile(r'^\[|^(EOF|TOML|GLSL)\s*$')
SHADER = re.compile(r'^shader = (.*)$')

def preset_name(event, value):
    base = value.strip().strip('"\'').split('/')[-1]
    base = re.sub(r'\.glsl$', '', base)
    base = re.sub(r'[^A-Za-z0-9]+', '_', base).strip('_')
    return f'{event}_{base}'.replace('$', '')

for path in map(pathlib.Path, sys.argv[1:]):
    lines = path.read_text().splitlines(keepends=True)
    out, i, changed = [], 0, False
    while i < len(lines):
        header = HEADER.match(lines[i])
        if not header:
            out.append(lines[i]); i += 1; continue
        j = i + 1
        section = []
        while j < len(lines) and not STOP.match(lines[j]):
            section.append(lines[j]); j += 1
        found = [(k, SHADER.match(l).group(1)) for k, l in enumerate(section) if SHADER.match(l)]
        if found:
            k, value = found[-1]
            name = preset_name(header.group(1), value)
            out.append(f'[effects.preset.{name}]\nkind = "animation"\nshader = {value}\n')
            section[k] = f'effect = "{name}"\n'
            changed = True
        out.append(lines[i]); out.extend(section); i = j
    if changed:
        path.write_text(''.join(out))
        print(path)
```

Run: `python3 /tmp/claude-1000/.../migrate_shader_keys.py tests/harness/checks/*.sh` (use the session scratchpad path). Expected: exactly the 24 files print.

- [ ] **Step 2: Review every diff by hand**

`git diff --stat tests/harness/checks` shows 24 files. Read each hunk and fix these known cases:

- `181_animation_shader_events.sh` parameterises the section as `[animation.$1]` in a function; the generated preset name `$1_fixture_1` must become `${1}_fixture_1` in both the table header and the `effect =` line so bash expands it (the heredoc there is unquoted).
- `182_animation_shader_composition.sh`: three sections; the invalid `fixture-3.glsl` preset (`windows_in_fixture_3`) must still produce the `effect preset '<name>' (animation) failed to compile; rendering plainly` log line (the registry's diagnostic) — with the registry this happens at reload (`prepare`), which the check's `grep` after `config-reload` already covers.
- `183_animation_shader_lifetime.sh` and `184_animation_squash.sh`: `EXAMPLES` / `SHADER` paths change to the new layout:
  - 183: `readonly EXAMPLES="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/effects/animation" && pwd)"`, `cp "$EXAMPLES/reveal/shader.glsl" "$SOURCE"`, and the move preset's value `"$EXAMPLES/squash/shader.glsl"`.
  - 184: `readonly SHADER="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/effects/animation" && pwd)/squash/shader.glsl"`.
- Any check that writes two different `[animation.<event>]` tables for the *same* event in one heredoc (a restore-then-override pattern) gets two presets with different names; that is fine.
- Any check that appends a `[animation.<event>]` table in a *second* heredoc to override the first must not redefine a preset name already defined in the same config file (a TOML duplicate-table error): rename the second preset (`_2` suffix). Check by running the affected check; a rejected config shows up as `config reload failed` in `$UMBRIEL_LOG`.

- [ ] **Step 3: Run the migrated checks**

Run: `nix develop . --command bash -c 'just check 171 180 181 182 183 184 185 186 187 188 189 190 192 193 195 196 198 200 202 205 206 207 208 330'`
Expected: every check passes. Investigate any failure by reading `$UMBRIEL_LOG` from the failing run (`just check <n> -v`) for `unknown key` or `ignoring animation.*.effect` lines: those mean the rewrite produced an invalid table for that file.

- [ ] **Step 4: Confirm nothing references the old key or path**

Run: `grep -rn 'shader = ' tests/harness/checks | grep -v 'effects.preset' ; grep -rn 'examples/shaders' tests docs examples meson.build src`
Expected: no output from the first (every `shader =` now sits under a preset); the second lists only `docs/user/animation.md` (rewritten in Stage 8).

- [ ] **Step 5: Commit**

```bash
git add -A tests/harness && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "test(harness): select animation presets through effect ="
```

---

### Task 3.4: `600_renderer_recovery` asserts effects rebind and the default fade survives

**Files:**
- Modify: `tests/harness/checks/600_renderer_recovery.sh`

- [ ] **Step 1: Extend the check**

Append after the existing `"$UMBRIEL" windows > /dev/null` line and before the final `grep -c` assertion:

```bash
# A preset bound before the loss must render through the new renderer's program,
# and a window without any selector must still open through the built-in fade.
cat > "$UMBRIEL_RUNTIME_DIR/rebind.glsl" <<'GLSL'
vec4 animation(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
duration_ms = 2000
curve = "linear"
[animation.windows_in]
style = "none"
[effects.preset.rebind]
kind = "animation"
shader = "rebind.glsl"
[animation.windows_move]
effect = "rebind"
EOF
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" renderer-recover > /dev/null
for _ in $(seq 100); do
  if [[ $(tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -c "renderer recreated") -ge 2 ]]; then
    break
  fi
  sleep 0.02
done
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/recovery.png"
"$UMBRIEL" clock-freeze
"$UMBRIEL_UNMAP_CLIENT" recovery-fade 600 400 > "$UMBRIEL_RUNTIME_DIR/recovery-fade.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "recovery-fade")')
  [[ -n $window ]] && break
  sleep 0.025
done
[[ -n $window ]]
x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
y=$(jq -r '.y + (.h / 2 | floor)' <<< "$window")
# Halfway through the built-in fade the client's blue shows at roughly half strength over the black backdrop.
"$UMBRIEL" clock-advance 1000
grim "$IMAGE"
read -r _ _ blue < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$x" "$y")
if (( blue < 40 || blue > 140 )); then
  echo "the built-in opening fade did not run after recovery: blue=$blue"
  exit 1
fi
"$UMBRIEL" clock-advance 3000
# Moving the window binds the rebind preset: the moved window renders solid green through the recompiled program.
id=$(jq -r .id <<< "$window")
"$UMBRIEL" msg "window-focus:$id" > /dev/null
"$UMBRIEL" msg window-toggle-floating > /dev/null
"$UMBRIEL" clock-advance 500
grim "$IMAGE"
green=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'g > 0.9 && r < 0.1 && b < 0.1')
if (( green < 1000 )); then
  echo "the animation preset did not rebind after renderer recovery: $green green pixels"
  exit 1
fi
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle > /dev/null
```

Update the final assertion from `-ne 1` to `-ne 2` (two recoveries now) and the closing echo to `"renderer loss unwound, recreated the renderer, drew another frame, and rebound effects"`. Confirm the action name for floating toggle with `./build-debug/umbriel actions | grep -i float` and adjust `window-toggle-floating` if the canonical name differs; `default_floating` on a window rule plus `window-move` is an alternative that also triggers `windows_move`.

- [ ] **Step 2: Run it under stress**

Run: `nix develop . --command bash -c 'just check 600 && just check-stress 600 8'`
Expected: passes 9 times.

- [ ] **Step 3: Commit and stage gate**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "test(harness): assert effects rebind after renderer recovery"
nix develop . --command bash -c 'just format && git diff --exit-code && just lint && just test && just check'
```
Expected: clean; full harness green except known flakes.
