# Effects Stage 6: Screen and cursor effects

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `screen` preset (default or `[output.X] screen_effect`) shades the whole output after the scene; a `cursor` preset shades a `radius` square around the pointer (or the whole output) before the software cursor; both detach under session lock, keep per-output feedback history, obey `in_capture`, and request frames only while visible and advancing.

**Architecture:** The per-output `scene_output_effects` addon from Stage 5 grows screen and cursor slots with their own parameters and histories; `build_state` runs them in place after the scene and before `wlr_output_add_software_cursors_to_render_pass`. `Output::applyOutputEffects()` resolves the presets and pushes time; `Cursor` forwards the pointer only while a cursor effect is active; the registry mirrors both as ledger instances.

**Tech Stack:** C23 umbrielfx, C++23, harness (`grim`, `lock-client`, `pointer-client`), a new `toplevel-capture-client` for isolated toplevel captures.

**Spec:** `docs/superpowers/specs/2026-09-25-effects-design.md` §2 (Pointer, Session lock, Capture), §3 (Output effects, cursor kind), §4 (screen, cursor), §6 (770 plus its two extensions). Index: `2026-09-25-effects-00-index.md`.

## Global Constraints

- **Reuse first:** Before adding a utility, helper, or method, inspect existing implementations and callers. Reuse or narrowly extend the owning helper; if none fits, record why in this plan and give shared behavior one owner.
- **Comments and documentation (maintainer policy):** Comments and documentation explain what the code currently does and any non-obvious constraint a reader needs. Do not narrate history, migrations, rejected alternatives, or "why we don't do X"; git holds that. This applies to Markdown, `meson.build`, and code comments alike. Keep them short; if a comment is longer than the code it describes, cut it down. The comments in this plan's snippets are written to that policy and can be kept; prose in the plan that explains a decision is for the executor, not for the code.
- **Directed reuse for this stage:** Use the existing `findOutputRule(config(), identity())` from `src/config/resolve.h` for output selection, preserving descriptor precedence. Reuse the common in-place pass, effect uniform binder, animation clock, pointer visibility state, and shared damage/history helpers. Follow the existing foreign-toplevel client and Meson protocol-generation patterns.
- **Every task gate:** Before marking a task complete or committing, audit its diff for duplicate helpers and for comments or doc text that narrate history, migrations, or alternatives, or that outgrow the code they describe.

See the index. Stage-specific:

- Screen: whole output after the scene; history resets on transform, format, or program change.
- Cursor: default only (no per-window/per-output override); `radius` crop, 0 = whole output; motion damages the old and new squares; history resets when the pointer hides, leaves the output, or the session locks; hardware cursors unaffected.
- `Cursor` does exactly one boolean check per motion when no cursor effect is configured.
- Time uniforms are pushed only to programs that read `umbriel_time` (so a time-free screen effect never forces whole-output damage per frame).

---

### Task 6.1: Output effect slots in umbrielfx

**Files:**
- Modify: `umbrielfx/include/umbrielfx/render/effect.h` (three public functions), `umbrielfx/types/scene/wlr_scene.c` (`scene_output_effects`, `build_state`, damage helpers), `umbrielfx/render/fx_renderer/effect_shader.c` (nothing new; the cursor preamble already declares `umbriel_pointer`), `umbrielfx/render/fx_renderer/fx_pass.c` (`draw_animation_texture` binds `umbriel_pointer` from `composite->pointer`)
- Test: `umbrielfx/tests/effects.c` (`output-effects` case)

**Interfaces:**
- Produces (public):
  ```c
  void wlr_scene_output_set_screen_effect(struct wlr_scene_output* output, struct fx_effect_shader* shader, const struct fx_animation_parameters* parameters);
  void wlr_scene_output_set_cursor_effect(struct wlr_scene_output* output, struct fx_effect_shader* shader, const struct fx_animation_parameters* parameters, int radius);
  // Layout coordinates. `visible` false hides the cursor effect and resets its history.
  void wlr_scene_output_set_effect_pointer(struct wlr_scene_output* output, double lx, double ly, bool visible);
  ```
- `fx_effect_composite` gains `const float* pointer;` (uv of the pointer in the drawn rectangle, NULL when not a cursor kind).
- `scene_output_effects` gains: `struct fx_effect_shader* screen; struct fx_animation_parameters screen_parameters; struct fx_animation_history screen_history; struct fx_effect_shader* cursor; struct fx_animation_parameters cursor_parameters; struct fx_animation_history cursor_history; int cursor_radius; double pointer_x, pointer_y; bool pointer_visible;`.

- [ ] **Step 1: Write the failing test**

Add to `umbrielfx/tests/effects.c`:

```c
static bool test_output_effects(struct fixture *fixture) {
	struct wlr_scene *scene = wlr_scene_create();
	struct wlr_scene_output *scene_output = wlr_scene_output_create(scene, fixture->output);
	const float blue[4] = { 0, 0, 1, 1 };
	wlr_scene_rect_create(&scene->tree, TEST_WIDTH, TEST_HEIGHT, blue);
	struct fx_effect_shader *screen = fx_effect_shader_create(fixture->renderer, FX_EFFECT_SCREEN,
		"vec4 screen(vec2 uv) { return umbriel_sample(uv).bgra; }", "screen");
	struct fx_effect_shader *cursor = fx_effect_shader_create(fixture->renderer, FX_EFFECT_CURSOR,
		"vec4 cursor(vec2 uv) { return distance(uv, umbriel_pointer) < 0.5 ? vec4(0.0, 1.0, 0.0, 1.0) : umbriel_sample(uv); }", "cursor");
	bool ok = check(screen != NULL && cursor != NULL, "screen and cursor programs compile");
	struct fx_animation_parameters parameters = { .progress = 1, .linear_progress = 1, .direction = 1 };
	wlr_scene_output_set_screen_effect(scene_output, screen, &parameters);
	wlr_scene_output_set_cursor_effect(scene_output, cursor, &parameters, 2);
	wlr_scene_output_set_effect_pointer(scene_output, 12, 12, true);
	struct wlr_output_state state;
	struct wlr_buffer *rendered = fixture_render_scene(fixture, scene_output, &state);
	ok &= check(rendered != NULL, "renders");
	if (rendered != NULL) {
		uint8_t far[4], at_pointer[4];
		ok &= fixture_read_pixel(fixture, rendered, 2, 2, far);
		ok &= fixture_read_pixel(fixture, rendered, 12, 12, at_pointer);
		ok &= check(far[2] > 250 && far[0] < 5, "the screen effect swapped the whole output to red");
		ok &= check(at_pointer[1] > 250, "the cursor effect paints around the pointer after the screen effect");
		wlr_buffer_unlock(rendered);
	}
	wlr_output_state_finish(&state);
	// A hidden pointer removes the cursor square.
	wlr_scene_output_set_effect_pointer(scene_output, 12, 12, false);
	rendered = fixture_render_scene(fixture, scene_output, &state);
	if (rendered != NULL) {
		uint8_t at_pointer[4];
		ok &= fixture_read_pixel(fixture, rendered, 12, 12, at_pointer);
		ok &= check(at_pointer[1] < 5 && at_pointer[2] > 250, "a hidden pointer has no cursor effect");
		wlr_buffer_unlock(rendered);
	}
	wlr_output_state_finish(&state);
	wlr_scene_output_set_screen_effect(scene_output, NULL, NULL);
	wlr_scene_output_set_cursor_effect(scene_output, NULL, NULL, 0);
	fx_effect_shader_unref(screen);
	fx_effect_shader_unref(cursor);
	wlr_scene_node_destroy(&scene->tree.node);
	return ok;
}
```
Register `output-effects`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd /home/barrulus/dev/umbriel && nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx effects-output-effects --print-errorlogs'`
Expected: compile error (the three functions are undeclared).

- [ ] **Step 3: Implement**

`wlr_scene.c`:

```c
static void output_effects_damage_cursor(struct scene_output_effects* effects) {
  if (effects->cursor == NULL || !effects->pointer_visible) {
    return;
  }
  struct wlr_scene_output* output = effects->output;
  int width, height;
  wlr_output_effective_resolution(output->output, &width, &height);
  pixman_region32_t damage;
  if (effects->cursor_radius <= 0) {
    pixman_region32_init_rect(&damage, 0, 0, width, height);
  } else {
    const int r = effects->cursor_radius;
    pixman_region32_init_rect(&damage, (int)floor(effects->pointer_x - output->x) - r, (int)floor(effects->pointer_y - output->y) - r, 2 * r + 1, 2 * r + 1);
  }
  scale_region(&damage, output->output->scale, true);
  output_to_buffer_coords(&damage, output->output);
  scene_output_damage(output, &damage);
  pixman_region32_fini(&damage);
}

void wlr_scene_output_set_screen_effect(struct wlr_scene_output* output, struct fx_effect_shader* shader, const struct fx_animation_parameters* parameters) {
  struct scene_output_effects* effects = scene_output_effects_get(output, shader != NULL);
  if (effects == NULL) {
    return;
  }
  if (effects->screen == shader && (shader == NULL || parameters_equal(parameters, &effects->screen_parameters))) {
    return;
  }
  if (effects->screen != shader) {
    fx_animation_history_reset(&effects->screen_history);
  }
  fx_effect_shader_unref(effects->screen);
  effects->screen = fx_effect_shader_ref(shader);
  if (parameters != NULL) {
    effects->screen_parameters = *parameters;
  }
  scene_output_damage_whole(output);
}

void wlr_scene_output_set_cursor_effect(struct wlr_scene_output* output, struct fx_effect_shader* shader, const struct fx_animation_parameters* parameters, int radius) {
  struct scene_output_effects* effects = scene_output_effects_get(output, shader != NULL);
  if (effects == NULL) {
    return;
  }
  if (effects->cursor == shader && effects->cursor_radius == radius && (shader == NULL || parameters_equal(parameters, &effects->cursor_parameters))) {
    return;
  }
  output_effects_damage_cursor(effects); // old square
  if (effects->cursor != shader) {
    fx_animation_history_reset(&effects->cursor_history);
  }
  fx_effect_shader_unref(effects->cursor);
  effects->cursor = fx_effect_shader_ref(shader);
  effects->cursor_radius = radius;
  if (parameters != NULL) {
    effects->cursor_parameters = *parameters;
  }
  output_effects_damage_cursor(effects); // new square
}

void wlr_scene_output_set_effect_pointer(struct wlr_scene_output* output, double lx, double ly, bool visible) {
  struct scene_output_effects* effects = scene_output_effects_get(output, false);
  if (effects == NULL || effects->cursor == NULL) {
    return;
  }
  int width, height;
  wlr_output_effective_resolution(output->output, &width, &height);
  const bool inside = lx >= output->x && ly >= output->y && lx < output->x + width && ly < output->y + height;
  const bool shown = visible && inside;
  if (effects->pointer_visible == shown && effects->pointer_x == lx && effects->pointer_y == ly) {
    return;
  }
  output_effects_damage_cursor(effects); // old square
  if (effects->pointer_visible && !shown) {
    fx_animation_history_reset(&effects->cursor_history);
  }
  effects->pointer_x = lx;
  effects->pointer_y = ly;
  effects->pointer_visible = shown;
  output_effects_damage_cursor(effects); // new square
}

static bool output_effects_active(const struct scene_output_effects* effects) {
  return effects != NULL && (effects->screen != NULL || (effects->cursor != NULL && effects->pointer_visible));
}

static void render_output_effect(struct scene_output_effects* effects, struct fx_effect_shader* shader,
    const struct fx_animation_parameters* parameters, struct fx_animation_history* history, const struct wlr_box* logical_box,
    const float* pointer, const struct render_data* data) {
  if (shader == NULL || shader->renderer != fx_get_render_pass(data->render_pass)->buffer->renderer) {
    return;
  }
  struct wlr_box box = *logical_box;
  transform_output_box(&box, data);
  const struct fx_effect_composite composite = {
      .shader = shader, .parameters = parameters, .box = box, .logical_box = *logical_box,
      .transform = data->transform, .capture_clip = &data->damage, .output_clip = &data->damage,
      .history = history, .output = data->output->output, .update_history = true, .role = data->role,
      .pointer = pointer,
  };
  fx_render_pass_effect_in_place(fx_get_render_pass(data->render_pass), &composite);
}

static void render_output_effects(struct scene_output_effects* effects, const struct render_data* data) {
  if (effects == NULL || data->effect_capture) {
    return;
  }
  const struct wlr_box whole = {.width = data->logical.width, .height = data->logical.height};
  render_output_effect(effects, effects->screen, &effects->screen_parameters, &effects->screen_history, &whole, NULL, data);
  if (effects->cursor != NULL && effects->pointer_visible) {
    struct wlr_box box = whole;
    if (effects->cursor_radius > 0) {
      const int r = effects->cursor_radius;
      box = (struct wlr_box){
          .x = (int)floor(effects->pointer_x - data->output->x) - r,
          .y = (int)floor(effects->pointer_y - data->output->y) - r,
          .width = 2 * r + 1, .height = 2 * r + 1,
      };
    }
    const float pointer[2] = {
        box.width > 0 ? (float)((effects->pointer_x - data->output->x - box.x) / box.width) : 0,
        box.height > 0 ? (float)((effects->pointer_y - data->output->y - box.y) / box.height) : 0,
    };
    render_output_effect(effects, effects->cursor, &effects->cursor_parameters, &effects->cursor_history, &box, pointer, data);
  }
}
```
Destroying the addon (`scene_output_effects_destroy`) unrefs both shaders and finishes both histories; the `create` path initialises both histories. `draw_animation_texture`: when `composite->pointer != NULL` bind `umbriel_pointer` (VEC2).

Output effects join Stage 1's sampling-aware invalidation. A screen program may read any texel of the output and a cursor program any texel of its square, so damage touching either box grows to it. Extend `expand_damage_to_effects` with a second pass after the animation loop, inside the same `do { } while (grew)`:
```c
      struct scene_output_effects* output_effects = scene_output_effects_get(scene_output, false);
      if (output_effects != NULL) {
        if (output_effects->screen != NULL) {
          const struct wlr_box whole = {.width = scene_output->output->width, .height = scene_output->output->height};
          grew |= damage_grow_to_box(scene_output, damage, &whole, commit);
        }
        if (output_effects->cursor != NULL && output_effects->pointer_visible) {
          struct wlr_box square;
          output_effects_cursor_box(output_effects, data, &square); // the logical square from render_output_effects, transformed
          grew |= damage_grow_to_box(scene_output, damage, &square, commit);
        }
      }
```
The function's actual Stage 1 signature is `expand_damage_to_effects(struct wlr_scene_output* scene_output, struct scene_effects* effects, const struct render_data* data, pixman_region32_t* damage, bool commit)` with `effects` possibly NULL. Replace its early return with `if ((effects == NULL || effects->persistent == 0) && output_effects == NULL) return false;` (look `output_effects` up once at the top of the function) and guard the animation loop with `if (effects != NULL)` so a NULL `effects` never reaches `wl_list_for_each(..., &effects->animations, ...)`. Factor the cursor square computation out of `render_output_effects` into `output_effects_cursor_box(effects, data, box)` (logical box, then `transform_output_box`) so both callers share it.

`build_state`:
- `persistent_visible` and `in_place_visible` both OR in `output_effects_active(output_effects)` (move the `output_effects` lookup above the scan): screen and cursor slots veto scanout and, like window/overlay slots, trigger the unfiltered composition.
- The unfiltered-composition condition uses `in_place_visible` (Stage 5), which now includes the output slots.
- After the dmabuf-feedback loop that follows `render_animated_range(...)` and before the highlight loop: `render_output_effects(output_effects, &render_data);`. The software cursor call stays where it is (after), so both effects sit under it.
- Scanout: `persistent_visible` already vetoes.

`fx_effect_shader`'s cursor kind samples `umbriel_sample` for the copy; `render_output_effect` uses the in-place path so the screen effect's output feeds the cursor effect.

- [ ] **Step 4: Run tests**

Run: `nix develop . --command bash -c 'meson test -C build-debug --suite umbrielfx --print-errorlogs'`
Expected: `effects-output-effects` passes with every earlier case.

- [ ] **Step 5: Commit**

```bash
git add -A umbrielfx && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(umbrielfx): screen and cursor output effects"
```

---

### Task 6.2: Compositor: output effects, pointer forwarding, lock, frames

**Files:**
- Modify: `src/output/output.h`, `src/output/output.cpp` (`applyOutputEffects`, `handleFrame`), `src/scene/effect_registry.h/.cpp` (`fillTimeUniforms` shader parameter, `applyOutputEffects`, `pointerMoved`, `cursorEffectActive`, `setSuspended`), `src/input/cursor.cpp` (`processMotion` :1381, `hideCursor` :265, `noteActivity` :220), `src/server/server_events.cpp` (lock/unlock: reapply outputs), `src/view/effects.cpp` (pass shaders to `fillTimeUniforms`)
- Test: `tests/unit/effects.cpp` (`resolveScreenEffectName` pure)

**Interfaces:**
- Produces (`src/view/effects.h`, pure): `[[nodiscard]] std::string resolveScreenEffectName(const Effects& effects, const OutputRule* rule);` — `rule->screenEffect` replaces the default; `"off"` → `""`.
- `EffectRegistry::fillTimeUniforms(fx_animation_parameters&, float seconds, const EffectPreset&, const fx_effect_shader* shader)` (already in this shape since Stage 2) — adds `umbriel_time` only when the program reads it.
- `EffectRegistry`: `void applyOutputEffects();` (every output), `void pointerMoved(double lx, double ly, bool visible);`, `[[nodiscard]] bool cursorEffectActive() const;` (`m_cursorActive`, set by `prepare`: cursor preset named, compiled, not suspended).
- `Output`: `void applyOutputEffects();` extended; `void refreshOutputEffectTime(uint64_t clockMsec);` called each frame while active; private `char m_cursorEffectOwner{};`.

- [ ] **Step 1: Write the failing pure test**

Append to `tests/unit/effects.cpp`:
```cpp
UMBRIEL_TEST(screenEffectNameFollowsTheOutputOverride) {
  umbriel::Effects effects;
  effects.screen = "vig";
  CHECK_EQ(umbriel::resolveScreenEffectName(effects, nullptr), std::string("vig"));
  umbriel::OutputRule rule;
  CHECK_EQ(umbriel::resolveScreenEffectName(effects, &rule), std::string("vig"));
  rule.screenEffect = "off";
  CHECK(umbriel::resolveScreenEffectName(effects, &rule).empty());
  rule.screenEffect = "crt";
  CHECK_EQ(umbriel::resolveScreenEffectName(effects, &rule), std::string("crt"));
}
```
Run: `nix develop . --command bash -c 'meson compile -C build-debug effects-test 2>&1 | tail -2'` — Expected: compile error.

- [ ] **Step 2: Implement**

`effects_rules.cpp`:
```cpp
  std::string resolveScreenEffectName(const Effects& effects, const OutputRule* rule) {
    if (rule == nullptr || !rule->screenEffect) {
      return effects.screen;
    }
    return *rule->screenEffect == kEffectOff ? std::string() : *rule->screenEffect;
  }
```

`Output`:
```cpp
  void Output::applyOutputEffects() {
    EffectRegistry& registry = m_server->effects();
    // Nothing configured: touch nothing (no addon, no scene calls, no clock read) — the Stage 5 gate.
    const Effects& settings = config().effects;
    if (settings.presets.empty() && !settings.inCapture && registry.active() == 0) {
      wlr_scene_output_set_screen_effect(m_sceneOutput, nullptr, nullptr);   // early-returns without an addon
      wlr_scene_output_set_cursor_effect(m_sceneOutput, nullptr, nullptr, 0);
      registry.removeInstance(this);
      registry.removeInstance(&m_cursorEffectOwner);
      return;
    }
    wlr_scene_output_set_effect_capture_policy(m_sceneOutput, settings.inCapture);
    const OutputRule* rule = findOutputRule(config(), identity());
    const std::string screenName = resolveScreenEffectName(config().effects, rule);
    const EffectPreset* screenPreset = screenName.empty() ? nullptr : registry.presetConfig(screenName);
    fx_effect_shader* screen = screenPreset != nullptr && !registry.ledger().suspended() ? registry.preset(screenName, EffectKind::Screen) : nullptr;
    const std::string& cursorName = config().effects.cursor;
    const EffectPreset* cursorPreset = cursorName.empty() ? nullptr : registry.presetConfig(cursorName);
    fx_effect_shader* cursor = cursorPreset != nullptr && !registry.ledger().suspended() ? registry.preset(cursorName, EffectKind::Cursor) : nullptr;
    const float seconds = static_cast<float>(m_server->animationClockMsec()) / 1000.0F;
    if (screen != nullptr) {
      fx_animation_parameters parameters{};
      registry.fillTimeUniforms(parameters, seconds, *screenPreset, screen);
      wlr_scene_output_set_screen_effect(m_sceneOutput, screen, &parameters);
      registry.updateInstance(this, {.output = this, .visible = m_output->enabled, .readsTime = fx_effect_shader_reads(screen, "umbriel_time"), .advancing = clockAdvancing()});
    } else {
      wlr_scene_output_set_screen_effect(m_sceneOutput, nullptr, nullptr);
      registry.removeInstance(this);
    }
    if (cursor != nullptr) {
      fx_animation_parameters parameters{};
      registry.fillTimeUniforms(parameters, seconds, *cursorPreset, cursor);
      wlr_scene_output_set_cursor_effect(m_sceneOutput, cursor, &parameters, cursorPreset->radius);
      const Cursor* pointer = m_server->cursor();
      const bool onOutput = pointer != nullptr && wlr_output_layout_output_at(m_server->outputLayout(), pointer->wlr()->x, pointer->wlr()->y) == m_output;
      registry.updateInstance(&m_cursorEffectOwner, {.output = this, .visible = m_output->enabled && onOutput && pointer->visible(), .readsTime = fx_effect_shader_reads(cursor, "umbriel_time"), .advancing = clockAdvancing()});
    } else {
      wlr_scene_output_set_cursor_effect(m_sceneOutput, nullptr, nullptr, 0);
      registry.removeInstance(&m_cursorEffectOwner);
    }
  }
```
`clockAdvancing()` is a small private helper: `#ifdef UMBRIEL_TEST_IPC return !m_server->animationClockFrozen(); #else return true; #endif`. `Cursor::visible()` is a new public accessor returning `!m_cursorHidden`. Include `config/resolve.h` and use `findOutputRule(config(), identity())`, as the other `Output` policy methods do; this preserves descriptor-specific rules over connector fallbacks.

`Output::refreshOutputEffectTime()`: called from `handleFrame` right after `tickAnimations` when `m_server->effects().active()`: re-runs only the time push — factor `applyOutputEffects` so the parameter fill is a lambda used by both, or simply call `applyOutputEffects()` there (it is cheap: a few string compares and two `set_*` calls that early-return when unchanged).

`Cursor`: at the top of `processMotion` (`:1382`, after `updateHotCorner();`):
```cpp
    if (m_server->effects().cursorEffectActive()) {
      m_server->effects().pointerMoved(m_cursor->x, m_cursor->y, !m_cursorHidden);
    }
```
and the same call in `hideCursor()` after `m_cursorHidden = true;` and in `noteActivity()` after `m_cursorHidden = false;` (guarded the same way).

`EffectRegistry::pointerMoved`: for each output `wlr_scene_output_set_effect_pointer(output->sceneOutput(), lx, ly, visible)` and update the cursor instance's `visible` (`onOutput && visible`) via `output->applyOutputEffects()` only when the containing output changed or visibility toggled (keep `m_pointerOutput`/`m_pointerVisible` in the registry to detect that; the per-motion cost is otherwise one loop of `set_effect_pointer`). `cursorEffectActive()` returns `m_cursorActive`, computed in `prepare()`: `!cursor.empty() && preset(cursor, Cursor) != nullptr && !m_ledger.suspended()`. `applyOutputEffects()` in the registry loops outputs; `prepare()` calls it last (after `ensureLightLayer`).

Lock: `Server::activateSessionLock` → `m_effects->setSuspended(true); m_effects->applyOutputEffects();` and `unlockSession` → `setSuspended(false); applyOutputEffects();` plus the eligible-frame kick from Stage 4. `setSuspended` also recomputes `m_cursorActive`.

`Server::removeOutput` (`server_events.cpp` ~1874): `m_effects->removeOutput(output)` (from Stage 4) before the output is destroyed.

- [ ] **Step 3: Build and existing checks**

Run: `nix develop . --command bash -c 'just build && just test && just check 52 54 60 750 760'`
Expected: green (pointer, lock, output checks unchanged; no cursor effect configured → one boolean per motion).

- [ ] **Step 4: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "feat(output): screen and cursor effects with pointer forwarding and lock detachment"
```

---

### Task 6.3: `toplevel-capture-client`

**Files:**
- Create: `tests/harness/clients/toplevel_capture_client.cpp`
- Modify: `tests/meson.build` (harness clients list; protocol XML generation for `ext-image-capture-source-v1` and `ext-image-copy-capture-v1` from `wayland-protocols`, following how `foreign_toplevel_client.cpp` gets `ext-foreign-toplevel-list-v1`), `tests/harness/check.sh` (export `UMBRIEL_TOPLEVEL_CAPTURE_CLIENT`)

**Interfaces:**
- Produces: `toplevel-capture-client <title>` prints one line `r g b` (0-255 means of the 8x8 centre of one captured frame of the toplevel whose title matches) and exits 0; exits 1 with a message on any protocol error or a 5 s timeout.

- [ ] **Step 1: Write the client**

Model the toplevel lookup on `tests/harness/clients/foreign_toplevel_client.cpp` (bind `ext_foreign_toplevel_list_v1`, wait for the handle whose `title` event matches). Then:

1. Bind `ext_foreign_toplevel_image_capture_source_manager_v1` and `ext_image_copy_capture_manager_v1` and `wl_shm`.
2. `source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(manager, handle)`.
3. `session = ext_image_copy_capture_manager_v1_create_session(copy_manager, source, EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS /* 0: no cursors */)`; handle `buffer_size` (width, height) and `shm_format` events; on `done`, create a `wl_shm` buffer of that size in the first offered format (expect `ARGB8888`/`XRGB8888`).
4. `frame = ext_image_copy_capture_session_v1_create_frame(session)`; `attach_buffer`, `damage_buffer(0,0,INT32_MAX,INT32_MAX)`, `capture`; on `ready`, compute the mean of the 8x8 centre pixels (respect `transform` events: the harness uses untransformed outputs for this client) and print `r g b`; on `failed`, exit 1 with the reason.
5. Use `wl_display_dispatch` in a loop with a 5 s deadline via `poll`.

Register in `tests/meson.build` next to `foreign_toplevel_client` (same dependency pattern, plus the two extra protocol headers generated with `wayland_scanner` from `wayland_protocols_dir / 'staging/ext-image-capture-source/ext-image-capture-source-v1.xml'` and `'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml'`), add it to `harness_client_targets`, and export `UMBRIEL_TOPLEVEL_CAPTURE_CLIENT` in `check.sh` next to the other client paths.

- [ ] **Step 2: Smoke it**

Run: `nix develop . --command bash -c 'just check 770' ` after Task 6.4 writes the check (the client is exercised there). For an early smoke: `just check 204` does not exist upstream; instead run `meson compile -C build-debug harness-clients` and confirm `build-debug/tests/toplevel-capture-client` exists.

- [ ] **Step 3: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "test(harness): toplevel image-copy capture client"
```

---

### Task 6.4: Harness checks `770_effect_screen_cursor` and `771_effect_capture_feedback`

**Files:**
- Create: `tests/harness/checks/770_effect_screen_cursor.sh`, `tests/harness/checks/771_effect_capture_feedback.sh`

The spec extends 770 twice (frames, feedback); the feedback extension needs its own compositor instance and its own budget, so it is `771` (CONTRIBUTING: split checks that grow past a few seconds).

- [ ] **Step 1: `770_effect_screen_cursor.sh`**

```bash
#!/usr/bin/env bash
# harness: outputs=2
# Screen and cursor presets together: the screen effect inverts HEADLESS-1 only (HEADLESS-2 opts out with "off"), the
# cursor square follows the pointer and stops requesting frames when hidden or off-output, the lock detaches both, and
# grim sees them only with in_capture = true.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-screen-cursor.png"
readonly OUTPUT_W=1280
readonly OUTPUT_H=720
cat > "$UMBRIEL_RUNTIME_DIR/invert.glsl" <<'GLSL'
vec4 screen(vec2 uv) { vec4 c = umbriel_sample(uv); return vec4(vec3(c.a) - c.rgb, c.a); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/glow.glsl" <<'GLSL'
// The red term is invisible at 8 bits but keeps umbriel_time active, so the program requests frames.
vec4 cursor(vec2 uv) { return vec4(0.001 * sin(umbriel_time), 1.0, 0.0, 1.0); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false
[colors]
backdrop = "#000000FF"
[input.cursor]
hide_timeout_ms = 0
[effects]
screen = "invert"
cursor = "glow"
in_capture = true
[effects.preset.invert]
kind = "screen"
shader = "invert.glsl"
[effects.preset.glow]
kind = "cursor"
shader = "glow.glsl"
radius = 40
[output."HEADLESS-2"]
screen_effect = "off"
EOF
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null

# grim captures through screencopy, so every visual assertion runs with in_capture = true; the final section flips it
# to false and asserts the capture goes plain. Pointer at (300, 300) on HEADLESS-1.
"$UMBRIEL_POINTER_CLIENT" "$OUTPUT_W" "$OUTPUT_H" move 300 300 > /dev/null
"$UMBRIEL" settle > /dev/null
grim -s 1 -o HEADLESS-1 "$IMAGE"
# Inverted black backdrop is white away from the pointer, green within 40 px of it.
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 900 600)
if (( r < 240 || g < 240 || b < 240 )); then
  echo "the screen effect did not invert HEADLESS-1: $r $g $b"
  exit 1
fi
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 300 300)
if (( g < 240 || r > 20 )); then
  echo "the cursor effect did not paint around the pointer: $r $g $b"
  exit 1
fi
grim -s 1 -o HEADLESS-2 "$IMAGE"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 900 600)
if (( r > 15 || g > 15 || b > 15 )); then
  echo "screen_effect = off did not disable the default on HEADLESS-2: $r $g $b"
  exit 1
fi
# The cursor square follows the pointer.
"$UMBRIEL_POINTER_CLIENT" "$OUTPUT_W" "$OUTPUT_H" move 700 200 > /dev/null
"$UMBRIEL" settle > /dev/null
grim -s 1 -o HEADLESS-1 "$IMAGE"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 700 200)
(( g > 240 )) || { echo "the cursor square did not follow the pointer"; exit 1; }
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 300 300)
(( g < 250 || r > 200 )) || { echo "the old cursor square was not repainted"; exit 1; }

# Frames: the time-reading cursor effect requests frames while visible on HEADLESS-1; moving to HEADLESS-2 stops them.
frames() { "$UMBRIEL" effect-frames --json | jq -r --arg n "$1" '.outputs[] | select(.name == $n) | .effect_frames'; }
before=$(frames HEADLESS-1)
sleep 0.3 # real time: effect-only frames arrive on the output's own timer
(( $(frames HEADLESS-1) > before )) || { echo "a visible cursor effect requested no frames"; exit 1; }
"$UMBRIEL_POINTER_CLIENT" "$OUTPUT_W" "$OUTPUT_H" move 1500 300 > /dev/null
"$UMBRIEL" settle > /dev/null
before=$(frames HEADLESS-1)
sleep 0.3 # real time: an off-output cursor instance must request no frames
(( $(frames HEADLESS-1) == before )) || { echo "an off-output cursor effect kept requesting frames on HEADLESS-1"; exit 1; }
# A hidden pointer stops frames too: back on HEADLESS-1, let the hide timeout elapse.
"$UMBRIEL_POINTER_CLIENT" "$OUTPUT_W" "$OUTPUT_H" move 300 300 > /dev/null
sed -i 's/^hide_timeout_ms = 0$/hide_timeout_ms = 100/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
sleep 0.3 # real time: the pointer hides after hide_timeout_ms
"$UMBRIEL" settle > /dev/null
before=$(frames HEADLESS-1)
sleep 0.3 # real time: a hidden cursor instance must request no frames
(( $(frames HEADLESS-1) == before )) || { echo "a hidden cursor effect kept requesting frames"; exit 1; }
sed -i 's/^hide_timeout_ms = 100$/hide_timeout_ms = 0/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL_POINTER_CLIENT" "$OUTPUT_W" "$OUTPUT_H" move 300 300 > /dev/null
"$UMBRIEL" settle > /dev/null

# The lock detaches both effects; unlock restores them.
readonly LOCK_LOG="$UMBRIEL_RUNTIME_DIR/lock-client.log"
readonly LOCK_FIFO="$UMBRIEL_RUNTIME_DIR/lock-control"
mkfifo "$LOCK_FIFO"
exec {lock_fd}<> "$LOCK_FIFO"
"$UMBRIEL_LOCK_CLIENT" <&"$lock_fd" > "$LOCK_LOG" 2>&1 &
for _ in $(seq 100); do grep -q '^locked$' "$LOCK_LOG" && break; sleep 0.05; done
grep -q '^locked$' "$LOCK_LOG" || { echo "the session never locked"; exit 1; }
"$UMBRIEL" settle > /dev/null
grim -s 1 -o HEADLESS-1 "$IMAGE"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 900 600)
# The lock client draws its own surface; whatever it draws, it must not be the inverted white nor green.
(( g < 240 || r > 20 )) || { echo "effects stayed attached under the session lock: $r $g $b"; exit 1; }
echo unlock >&"$lock_fd"
for _ in $(seq 100); do grep -q '^unlocked$' "$LOCK_LOG" && break; sleep 0.05; done
"$UMBRIEL_POINTER_CLIENT" "$OUTPUT_W" "$OUTPUT_H" move 300 300 > /dev/null
"$UMBRIEL" settle > /dev/null
grim -s 1 -o HEADLESS-1 "$IMAGE"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 900 600)
(( r > 240 )) || { echo "the screen effect did not return after unlock: $r $g $b"; exit 1; }

# in_capture = false: grim sees the plain frame while the display keeps the effects (asserted through the frame
# counter continuing to run, since the display cannot be sampled without a capture).
sed -i 's/^in_capture = true$/in_capture = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
grim -s 1 -o HEADLESS-1 "$IMAGE"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 900 600)
(( r < 15 && g < 15 )) || { echo "with in_capture = false grim still saw the screen effect: $r $g $b"; exit 1; }
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 300 300)
(( g < 15 )) || { echo "with in_capture = false grim still saw the cursor effect: $r $g $b"; exit 1; }
echo "screen and cursor effects, the per-output override, pointer tracking, frame gating, lock detachment, and capture policy verified"
```
Check that `[input.cursor] hide_timeout_ms` is the canonical key (`docs/user/input.md`); `0` must mean "never hide" — if 0 hides immediately, use a large value. The lock section's assertion samples whatever the lock client draws; if the lock client's surface happens to be white, sample a pixel outside its surface or assert on the cursor square only.

- [ ] **Step 2: `771_effect_capture_feedback.sh`**

```bash
#!/usr/bin/env bash
# A feedback animation enclosing a window effect keeps separate histories per composition role: with in_capture = false
# every captured frame excludes the window effect, and the display's feedback matches a run without any capture at the
# same clock steps. Isolated toplevel captures follow the same policy.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-capture-feedback.png"
cat > "$UMBRIEL_RUNTIME_DIR/accumulate.glsl" <<'GLSL'
// Each frame adds a little red to the previous result: the value depends on how many frames the history has seen.
vec4 animation(vec2 uv) { vec4 p = umbriel_sample_previous(uv); return vec4(min(p.r + 0.1, 1.0), 0.0, umbriel_sample(uv).b, 1.0); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/green.glsl" <<'GLSL'
vec4 window(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }
GLSL
readonly BASE="$UMBRIEL_RUNTIME_DIR/feedback-base.toml"
cp "$UMBRIEL_CONFIG" "$BASE"
write_config() {
  cat "$BASE" > "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<EOF

[colors]
backdrop = "#000000FF"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[animation]
duration_ms = 4000
curve = "linear"
[animation.windows_in]
style = "none"
effect = "accumulate"
[effects]
window = "green"
in_capture = $1
[effects.preset.accumulate]
kind = "animation"
shader = "accumulate.glsl"
[effects.preset.green]
kind = "window"
shader = "green.glsl"
[[window_rule]]
match.title = "^feedback$"
default_floating = true
EOF
  "$UMBRIEL" msg config-reload > /dev/null
}
spawn() {
  FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" feedback 300 200 > "$UMBRIEL_RUNTIME_DIR/feedback.log" 2>&1 &
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "feedback")')
    [[ -n $window ]] && break
    sleep 0.025
  done
  [[ -n $window ]]
  read -r x y w h id < <(jq -r '"\(.x) \(.y) \(.w) \(.h) \(.id)"' <<< "$window")
}
# The client is blue. The window effect paints it green; the enclosing accumulate program keeps only red (history) and
# blue (its input). So: an unfiltered frame shows blue > 0 (client seen), a filtered one shows blue = 0 (green window
# seen), and red counts how many display frames the history has accumulated.
centre_red() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$((x + w / 2))" "$((y + h / 2))" | cut -d' ' -f1; }
centre_blue() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$((x + w / 2))" "$((y + h / 2))" | cut -d' ' -f3; }

# Reference run with effects included in captures: the same clock steps and the same three grim calls as the capture
# run below, so both runs draw the same number of display frames (grim itself requests a frame). Only the last reading
# is kept.
write_config true
"$UMBRIEL" clock-freeze
spawn
for _ in $(seq 2); do "$UMBRIEL" clock-advance 100 > /dev/null; done
grim "$IMAGE"
"$UMBRIEL" clock-advance 100 > /dev/null
grim "$IMAGE"
for _ in $(seq 2); do "$UMBRIEL" clock-advance 100 > /dev/null; done
grim "$IMAGE"
reference=$(centre_red)
"$UMBRIEL" msg "window-close:$id" > /dev/null
"$UMBRIEL" clock-advance 8000 > /dev/null
"$UMBRIEL" settle > /dev/null

# Capture run with effects excluded from captures: the same steps; every grim is a pending capture that stops between
# the calls and restarts.
write_config false
spawn
for _ in $(seq 2); do "$UMBRIEL" clock-advance 100 > /dev/null; done
grim "$IMAGE"
first_capture_blue=$(centre_blue)
"$UMBRIEL" clock-advance 100 > /dev/null
grim "$IMAGE"
second_capture_blue=$(centre_blue)
for _ in $(seq 2); do "$UMBRIEL" clock-advance 100 > /dev/null; done
grim "$IMAGE"
third_capture_blue=$(centre_blue)
if (( first_capture_blue < 200 || second_capture_blue < 200 || third_capture_blue < 200 )); then
  echo "captured frames included the window effect (client blue hidden): $first_capture_blue $second_capture_blue $third_capture_blue"
  exit 1
fi
# The display kept accumulating through its own history; the capture role kept its own. Read the display through an
# in_capture = true reload, which does not touch display history.
sed -i 's/^in_capture = false$/in_capture = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-advance 1 > /dev/null
grim "$IMAGE"
display_red=$(centre_red)
if (( display_red < reference - 12 || display_red > reference + 12 )); then
  echo "display feedback diverged from the capture-free run: $display_red vs $reference"
  exit 1
fi
# Isolated toplevel capture: the capture scene holds only client surfaces (no enclosing animation), so it shows the
# green window effect when included and the plain blue client when excluded.
read -r _ g b < <(timeout 10 "$UMBRIEL_TOPLEVEL_CAPTURE_CLIENT" feedback)
(( g > 240 )) || { echo "isolated capture with in_capture = true lacks the window effect: g=$g b=$b"; exit 1; }
sed -i 's/^in_capture = true$/in_capture = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-advance 1 > /dev/null
read -r _ g b < <(timeout 10 "$UMBRIEL_TOPLEVEL_CAPTURE_CLIENT" feedback)
(( b > 200 && g < 15 )) || { echo "isolated capture with in_capture = false included the window effect: g=$g b=$b"; exit 1; }
echo "capture-role feedback isolation and isolated toplevel capture policy verified: display $display_red vs $reference"
```
The accumulate program adds 0.1 red (≈25 levels) per rendered display frame. On a frozen clock each `clock-advance` draws exactly one frame and each `grim` requests one more, so both runs render the same eight display frames and the ±12 tolerance separates "history isolated" (equal) from "history contaminated or reset" (at least one step off). `write_config` rewrites the whole config from `$BASE`, so the second run starts from fresh state; the window from the first run is closed before it.

- [ ] **Step 3: Run under stress and gate the stage**

Run: `nix develop . --command bash -c 'just check 770 771 && just check-stress 770 8 && just check-stress 771 8 && just format && git diff --exit-code && just lint && just test && just check'`
Expected: green.

- [ ] **Step 4: Commit**

```bash
git add -A && GIT_AUTHOR_NAME=barrulus GIT_AUTHOR_EMAIL=b@rry.im GIT_COMMITTER_NAME=barrulus GIT_COMMITTER_EMAIL=b@rry.im git commit -m "test(harness): screen, cursor, and capture feedback checks"
```
