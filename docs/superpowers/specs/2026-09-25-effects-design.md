# Effects (PR 1) design

Branch: `feat/shader-engine`, from upstream `8346073`.
Source of requirements: the maintainer's `EFFECTS-INTEGRATION.md` (PR 1 section).
This spec is a working document. It references the fork and must be removed from
the branch before the pull request is opened.

## Goal

Land every effect kind (`animation`, `border`, `window`, `screen`, `cursor`),
border light, window overlays, and drag physics in the maintainer's approved
shape. Pools and runtime actions are PR 2 and out of scope.

Every effect is off by default. A configuration that sets no effect keys renders,
performs, and behaves exactly as upstream does.

## Deliberate deviations from the maintainer document

Each is called out in the PR description.

1. `[animation.windows_drag] physics = true` instead of `wobble = true`. Nothing
   in code, config, or docs is called "wobble"; the feature is "drag physics".
2. The `window` kind is defined precisely as *in place*: its shader reads the
   framebuffer the window has just been drawn into and writes the result back
   through the window's rounded mask. At rest that framebuffer is the output, so
   the shader sees and may rewrite the desktop visible through a translucent
   window. Inside an enclosing capture (drag physics, open, move, workspace,
   overview) it is the capture buffer, so the shader sees the window alone and
   the desktop beneath stays live. This matches existing fork behaviour and the
   document's wording "a window's composited content".

## 1. Configuration

New `src/config/effects.{h,cpp}` absorbs `animation_shader.{h,cpp}`.

```cpp
enum class EffectKind : std::uint8_t { Animation, Border, Window, Screen, Cursor };

struct BorderLight {
  int spread = 80;          // 1–256 logical px
  float intensity = 1.0F;   // 0–4
  float threshold = 0.5F;   // 0–1
};

struct EffectPreset {
  std::string name;
  EffectKind kind;
  ShaderSource shader;      // renamed AnimationShaderSource: code + file, defaulted ==
  bool palette = false;
  int padding = 0;          // border: 0–1024 logical px
  float speed = 1.0F;       // border: 0–10
  bool animated = true;     // border
  std::string overlay;      // border: names a Window preset
  std::optional<BorderLight> light;  // border
  int radius = 0;           // cursor: 0–4096, 0 = whole output
};

struct Effects {
  std::vector<EffectPreset> presets;
  std::string border, window, screen, cursor;  // "" = off
  int maxFps = 0;           // 0–240, 0 follows refresh
  bool inCapture = false;
};
```

- `[effects.preset.<name>]`: `kind` is read as an enum first; only that kind's
  keys are claimed, so inapplicable keys are reported by `Section`'s ordinary
  unknown-key diagnostic. `palette` is accepted on every kind.
- `shader` uses the existing reader, renamed `readShaderSource`: relative to the
  declaring TOML, regular file, nonblank, NUL-free, 256 KiB, nonblocking open,
  watched even when missing. A missing or unreadable file gives a diagnostic and
  leaves the preset inert.
- Selectors: `[effects] border/window/screen/cursor` (name or `""`),
  `[[window_rule]] border_effect / window_effect`, `[output.X] screen_effect`
  (name or `"off"`). The most specific replaces the default by name.
  `ResolvedWindowRule` carries both window names through last-writer-wins.
- `[animation.<event>] shader` is removed (unknown key). `effect = "<name>"`
  replaces it and must name an `animation` preset.
- `[animation.windows_drag]` is new with one key, `physics = false`.
- Post-load validation, so forward and cross-include references work: every
  reference (slots, rule and output overrides, `overlay`, animation `effect`) is
  checked for existence and kind. A failure gives a diagnostic and that setting
  is off. `off` cannot be a preset name.
- Duplicate preset names are an Error naming both files. `expandFile` records
  the defining file of each `effects.preset.<name>` table before `deepMerge`.
- Palette: `accent_primary`, `accent_secondary`, `warning`, `error` from
  `[colors]`. A colour change updates uniforms without recompiling.
- Reload: shader contents take part in `EffectPreset ==`. `ConfigEffects` gains
  an `effects` flag so only effect-dependent state refreshes. Identical reloads
  are inert.

## 2. Ownership in `src/`

### `EffectRegistry` (Server-owned, `src/scene/effect_registry.{h,cpp}`)

- Replaces the file-static cache in `src/scene/animation_shader.cpp`.
- Holds one refcounted `fx_effect_shader*` per referenced preset for the current
  renderer, keyed by kind and exact source. Unreferenced presets are not
  compiled. Failures are cached as null with a diagnostic.
- Prepares at startup, on reload when the `effects` flag is set, and after
  renderer recovery. Never compiles in render callbacks.
- Owns the built-in fade (existing) and the built-in deformation program,
  compiled on first use and only when drag physics is enabled.
- Owns the palette uniforms.
- Exposes the active count (views and outputs carrying a persistent effect) and
  whether any active program reads `umbriel_time`. Hot paths check this first.

### `ViewEffects` (member of `View`, `src/view/effects.{h,cpp}`)

- Resolved border and window preset names: default, then window rule, then
  `"off"`. Re-resolved on rule or config change.
- Border gating: focused, decorated, not urgent, not fullscreen, matching the
  existing active-border rules. The border's `overlay` applies only while the
  border effect does.
- `applyViewEffects(surfaceNode, borderNode)`: the single scene helper called by
  `View::syncAnimationShaders` and `Overview::layoutCard`.
- Close snapshots copy the slots with time frozen through
  `wlr_scene_node_copy_animations_for_snapshot`.

### Policy

- Time: animation clock × `speed`; 0 when `animated = false` or `speed = 0`.
- Frames: while an active program reads time, `Output::handleFrame` requests
  effect-only frames capped by `max_fps`. The timer is created lazily.
- Session lock: no effect-only frames, screen and cursor effects detached, lock
  surface never shaded.
- Pointer: `Cursor` forwards the pointer position only while a cursor effect is
  active (one boolean check per motion).
- Capture: `in_capture` decides whether screen and window effects appear in
  output captures and isolated toplevel captures.
- Drag physics: `src/view/drag_physics.{h,cpp}`, class `DragPhysics`. A 4×4
  spring sheet at a fixed 240 Hz step with grab pinning, bounded displacement
  and velocity, no folding, and settling after release and while held still.
  The move grab begins, feeds, and ends it; `View::tickAnimations` advances it
  on the animation clock. The renderer receives only `umbriel_deformation[16]`.

## 3. umbrielfx

### Program type

```c
enum fx_effect_kind { FX_EFFECT_ANIMATION, FX_EFFECT_BORDER, FX_EFFECT_WINDOW,
                      FX_EFFECT_SCREEN, FX_EFFECT_CURSOR };
struct fx_effect_shader* fx_effect_shader_create(struct wlr_renderer*, enum fx_effect_kind,
                                                 const char* source, const char* label);
struct fx_effect_shader* fx_effect_shader_ref(struct fx_effect_shader*);
void fx_effect_shader_unref(struct fx_effect_shader*);
void fx_effect_shader_set_shape_preserving(struct fx_effect_shader*, bool);
bool fx_effect_shader_reads(const struct fx_effect_shader*, const char* uniform);
```

`fx_animation_shader` is renamed; compilation, preamble, uniform lookup,
refcounting, renderer-destroy handling, and `#line 1` diagnostics exist once.

Shared preamble: `umbriel_sample(uv)`, `umbriel_sample_previous(uv)`,
`umbriel_size` (logical), `umbriel_scale`, `umbriel_time`,
`umbriel_palette_count`, `umbriel_palette_at(t)` (wraps; count 0 returns
transparent black).

| Kind | Entry point | Kind-specific |
| --- | --- | --- |
| animation | `vec4 animation(vec2 uv)` | existing progress, linear progress, direction, seed, clamped progress |
| border | `vec4 border(vec2 uv)` | `umbriel_border_hole`, `umbriel_border_radius`, `umbriel_border_distance(uv)` (signed, negative inside the window) |
| window | `vec4 window(vec2 uv)` | none |
| screen | `vec4 screen(vec2 uv)` | none |
| cursor | `vec4 cursor(vec2 uv)` | `umbriel_pointer` (uv within the drawn rect) |

`uv` is normalized over the drawn rectangle, `(0,0)` top left. For `border` the
rectangle includes `padding`. Results are premultiplied RGBA. No `#version`,
`main`, or precision qualifiers.

### Generic uniforms

The parameters struct keeps the existing animation fields and gains an array of
`{name, type, count, values}`. Time, palette, border geometry, pointer, and
deformation all travel through it. Locations are resolved once per program and
cached. Starting point: `params.c` from the fork's `feat/shader-system` branch.

### Slots

`FX_ANIMATION_SLOTS` becomes 13. Descendants before ancestors, as today.

| Node | Slots, in order |
| --- | --- |
| View surface node (surfaces and subsurfaces only) | `window`, `overlay` |
| Border node | `border_effect`, existing `border` |
| Content tree | existing `dim_unfocused`, `windows_move`, new `drag`, existing `windows_in`, `windows_out`, `scratchpad` |
| Layer / workspace / overview nodes | existing `layers`, `workspaces`, `overview` |

Slot modes:

- **Capture** (existing): the subtree renders offscreen and the program
  composites it. Used by animation, border, and drag slots. Border and drag
  slots accept `expand` (logical px) to grow the drawn rectangle beyond the node
  bounds. Border output is clipped from the client hole.
- **In place** (new): the subtree renders normally into the current target, then
  the program reads that target under the node's rect and writes back through
  the rounded mask. Used by window and overlay slots.

### Output effects

Screen and cursor are addons on `wlr_scene_output`, run after the scene and
before the software cursor, screen then cursor, in place on the output buffer.
The cursor rect is `radius` around the pointer position passed by `src`, or the
whole output for `radius = 0`. Per-output history.

### Border light

A border slot may carry light settings. Emission comes from that slot's result
into a per-scene list. `src` creates the light tree lazily above the drag layer
and below the top layer and registers it with
`wlr_scene_set_effect_light_layer`. Rendering that tree draws the blurred
emission (half-float pyramid, Kawase passes, screen blend). Helper programs are
compiled once per renderer, on first use. Light is suppressed while an enclosing
capture transforms the view, and is not drawn from snapshots. Borders stacked
above the light layer (pinned windows) do not emit.

### State, scanout, capture

- The module-static `scene_animations` list becomes a per-scene addon holding a
  list and a count; `scene_has_effects()` is O(1). No static clocks.
- While the count is above zero the existing conservative policy applies: no
  direct scanout, no opaque culling, full damage. An output with a screen or
  cursor effect never scans out.
- With `in_capture = false`, while a capture is pending on an output, a second
  composition without screen, cursor, window, and overlay passes feeds the
  capture (the fork's `effect_capture` buffer and dmabuf texture). Border
  effects stay. Isolated toplevel captures get window slots only with
  `in_capture = true`.

## 4. Behaviour per kind

- **animation**: `effect` binds the preset into the event's existing slot.
  Timing, curves, enable switches, retargeting, and snapshots are unchanged.
  `windows_in`/`windows_out` without an effect keep the built-in fade. Running
  events keep their program; reloads affect the next event.
- **border**: focused, decorated, non-urgent, non-fullscreen window. Padding
  grows the ring box through `makeBorderRing` on top of per-window
  `border_width`, `outer_border_width`, and `corner_radius` overrides.
  `umbriel_sample` is the native border. Focus change moves the effect and its
  overlay and clears history on the old window. Close snapshots keep it with
  time frozen. Overview cards get it through the shared helper.
- **window**: default or `window_effect`; applies regardless of focus; in place;
  continues through open, close, move, workspace, and overview captures, close
  snapshots, and overview cards; applies to undecorated and fullscreen windows.
- **screen**: default or `screen_effect`; whole output after the scene; detached
  while locked; history resets on transform, format, or program change.
- **cursor**: default only; `radius` crop; motion damages old and new squares;
  history resets when the pointer hides, leaves the output, or the session
  locks; before the software cursor; hardware cursors unaffected.
- **drag physics**: gated by the animation master switch. Starts on pointer drag
  of a window, settles after release. `drag` slot with the deformation program
  and `expand`. Border, overlay, and window effects ride inside the capture.
  Close during drag hands the frozen deformation to the snapshot.
- **Failures**: missing file, compile failure, unknown name, wrong kind give a
  diagnostic and plain rendering for that setting. Fixing a watched file
  recovers on reload.

## 5. Cost

With no effect configured (merge gate): no compilation, addons, light tree,
output addons, timers, or clock reads; `Cursor` does one boolean check;
`DragPhysics` is never touched; scanout, damage, culling, reload, and renderer
recovery follow unchanged paths.

With effects active, documented in the design page: border (capture + program
per frame on the focused window; time-driven frames capped by `max_fps`; light
adds a pyramid and blur per output), window (region copy + program per window
per frame), screen and cursor (one output-sized or radius-sized pass; direct
scanout off on that output), drag physics (only while held or settling), any
persistent effect (scene-wide conservative policy), capture with
`in_capture = false` (extra composition only on frames with a pending capture).

Verification per `docs/design/render-performance.md`: Tracy and
`vkmark --fullscreen` (immediate and fifo, panel hidden and visible) on upstream
`main` versus this branch with no effects; results must match. Repeat with one
effect of each kind and record numbers. Refresh stale `wlr_scene.c` line
references.

## 6. Tests, docs, examples

### Unit (`tests/unit/`)

- `config_load.cpp`: kind parsing, per-kind key rejection, unknown and mismatched
  references, `off`, duplicate definitions across files, `overlay` kind, palette
  order, removed `shader` key, `windows_drag.physics`.
- `effects.cpp` (new): `ViewEffects` resolution and gating as pure functions.
- `drag_physics.cpp` (new): pinned grab, bounded displacement and velocity, no
  folding, settling after release and while held. Behaviour, not constants.

### umbrielfx

- `umbrielfx/tests/effects.c` (new) with `render_fixture.h` extracted from
  `color.c`: one synthetic shader per kind; entry point, preamble helpers,
  generic uniform binding including a type mismatch, in-place read-back, border
  `expand` and hole clipping, renderer-destroy handling.

### Harness

Inline synthetic shaders, `umbriel settle`, the animation clock, the pixel probe.

| Check | Asserts |
| --- | --- |
| `750_effect_border` | padding grows the drawn area; hole shows the client; effect and overlay follow focus; `border_effect = "off"`; time freezes with `clock-freeze`; light beyond padding over a neighbour |
| `751_effect_border_transform` | border `uv` and `umbriel_border_distance` on a rotated fractional-scale output |
| `760_effect_window` | translucent window's shader modifies the desktop seen through it at rest; inside an open capture it sees the window alone; survives close snapshot; appears on overview cards; `window_effect` override |
| `770_effect_screen_cursor` | screen and cursor together; `screen_effect` override on the second of two outputs; cursor square follows pointer; lock detaches both; `in_capture` false and true via `grim` |
| `780_effect_reload` | missing file gives ordinary pixels and fixing it recovers; unknown and mismatched names reported with the setting off; `[colors]` change reaches the shader |
| `480_drag_physics` | deformation while held; settling after release; handover on close during drag |

Existing checks: every check using `shader =` moves to a preset plus `effect =`;
183 and 184 use `examples/effects/animation/{reveal,squash}/`;
`600_renderer_recovery` also asserts effects rebind.

### Examples

`examples/effects/<kind>/<name>/{shader.glsl,effect.toml}`, installed to
`share/umbriel/effects/`: `animation/reveal`, `animation/squash`,
`border/pulse`, `window/scanlines`, `screen/vignette`, `cursor/glow`. Each
`effect.toml` defines its preset and selects nothing. `examples/shaders/` is
removed. `examples/config.toml` gains commented, disabled examples only.

### Documentation

- `docs/user/effects.md` (new): every key, contract per kind, palette, window
  in-place behaviour.
- Updates: `animation.md`, `window-rules.md`, `outputs.md`, `configuration.md`.
- `docs/design/effects.md` (new): ownership, attachment, slot modes, light,
  capture, cost.
- `animation-shaders.md`: `effect =`, 13 slots. `render-performance.md`: new
  scanout conditions, refreshed line references.
- Anonymous, current-state only, MIT under the repository `LICENSE`.

## Commit stages

1. umbrielfx program type, generic uniforms, per-scene state
2. Config and registry
3. Animation migration
4. Border and light
5. Window
6. Screen and cursor
7. Drag physics
8. Examples, docs, performance notes

## Out of scope

- Pools, allocation, runtime actions (PR 2).
- Porting the fork collection to the community repository layout.
- Builtin colour filters, regions, animation pairs, redraw modes, forced
  software cursor.
