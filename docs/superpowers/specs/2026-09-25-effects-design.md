# Effects (PR 1) design

Branch: `feat/shader-engine`, from upstream `8346073`.
Source of requirements: the maintainer's `EFFECTS-INTEGRATION.md` (PR 1 section)
and follow-up on persistent-effect cost and the in-place window contract.
This spec is a working document. It references the fork and must be removed from
the branch before the pull request is opened.

## Goal

Land every effect kind (`animation`, `border`, `window`, `screen`, `cursor`),
border light, window overlays, and drag physics in the maintainer's approved
shape. Pools and runtime actions are PR 2 and out of scope.

Every effect is off by default. A configuration that sets no effect keys renders,
performs, and behaves exactly as upstream does.

## Deliberate deviation from the maintainer document

Call this out in the PR description:

`[animation.windows_drag] physics = true` instead of `wobble = true`. Nothing
in code, config, or docs is called "wobble"; the feature is "drag physics".

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
- Owns the existing built-in fade, prepared whenever the existing lifecycle
  animation settings require it, independently of custom effects and drag
  physics. Default opening and closing animations keep their upstream path.
- Owns the built-in deformation program, prepared only when drag physics and
  the animation master switch are enabled, before the first drag render.
- Owns the palette uniforms.
- Exposes the active count (views and outputs carrying a persistent effect) and
  a per-output count of visible effect instances whose programs read
  `umbriel_time` and whose effective clocks are advancing. Hot paths check the
  counts before any lookup. Instance eligibility is refreshed when attachment,
  visibility, clock, or suspension state changes.

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
- Frames: `Output::handleFrame` requests effect-only frames only for an output
  with an eligible instance in the registry's per-output count, capped by
  `max_fps`. A program reading time is not sufficient: `animated = false`,
  `speed = 0`, frozen snapshot time, a frozen animation clock, disabled or
  off-output nodes, and hidden or off-output cursor effects do not request
  frames. The timer is created lazily and disarmed when no eligible instances
  remain. Becoming eligible schedules the first frame. Client damage, pointer
  motion damage, and existing animation/drag timelines keep their own frame
  scheduling; `max_fps` caps only additional effect-only frames.
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

### Window sampling contract

At rest, with no enclosing animation capture, a window effect reads the output
framebuffer after the window is drawn. It sees and may rewrite the desktop
visible through a translucent window, allowing backdrop-aware effects.
Inside an enclosing animation capture (drag physics, open, move, workspace,
overview), it reads the capture target instead: window content is shaded and
composited over the live desktop. The desktop beneath is not captured and moved
with the window. Shader authors must account for this change in available
backdrop when an enclosing animation begins or ends. The same contract applies
to a border's window overlay.

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

Emission damage includes the light's full spread, clipped to each affected
output. Light buffers and blur passes are needed only on outputs with visible
emission; the shared light tree does not activate effects on other outputs.

### State, scanout, capture

- The module-static `scene_animations` list becomes per-scene addon state with
  separate counts for transient animation slots and persistent effect slots.
  Persistent border, window, and overlay slots never contribute to
  `scene_has_animations()`, even when their time uniforms advance. Screen and
  cursor effects remain per-output addons. Counts are O(1) guards, not a shared
  rendering-policy switch. No static clocks.
- Existing transient animation slots, including drag while active, retain their
  existing conservative policy. Sharing an attachment or program type does not
  let persistent slots keep that scene-wide policy active after a transition
  ends. Classification follows the slot's purpose, not the shader's time use.
- Persistent border and window effects (including overlays) track their drawn
  bounds and output intersections. Their damage is confined to those bounds;
  border bounds include padding and light includes its emission spread. Moving,
  changing, or removing an effect damages its old and new bounds. An input or
  backdrop change that can affect arbitrary sampling invalidates the affected
  effect's drawn box, not the whole output or scene.
- Persistent effects disable opaque-culling assumptions only for their own
  affected node/subtree. Preserve the source content and backdrop needed by the
  pass, and do not use pre-effect opacity to cull pixels it may expose. Unrelated
  nodes retain normal opaque-region and visibility processing, including on
  the same output. Effect padding participates in visibility bounds.
- Persistent effects block direct scanout and require effect offscreen buffers
  only on outputs where their drawn result is visible. Track this separately
  from time-driven frame eligibility: a frozen visible effect still needs
  composition. Recompute output membership on visibility, geometry, clipping,
  and output-layout changes, and release effect buffers when no local consumer
  remains. Existing blur and transient-animation buffer requirements remain.
- Screen and cursor effects block scanout only on their affected output and
  damage their own drawn rectangles (whole output for screen or cursor radius
  zero). They do not change other outputs' culling or buffer requirements.

Audit every use of the old scene-wide predicate independently:

| Path | Persistent-effect requirement |
| --- | --- |
| `scene_entry_try_direct_scanout` | Veto only for an effect visible on that output. |
| Whole-output damage in `wlr_scene_output_build_state` | Use affected drawn boxes; persistent presence alone never forces whole-output damage. |
| `scene_node_opaque_region` and render-list visibility calculation | Apply exceptions to the affected node/subtree; preserve normal culling elsewhere. |
| `fx_render_pass_init_offscreen_buffers` | Add effect demand only on outputs with a visible local consumer. |
| `fx_renderer_clear_animation_buffers` | Account for local persistent consumers so their buffers are retained while needed and released afterwards. |

Capture policy:

- With `in_capture = false`, while a capture is pending on an output, a second
  composition without screen, cursor, window, and overlay passes feeds the
  capture (the fork's `effect_capture` buffer and dmabuf texture). Border
  effects stay. Isolated toplevel captures get window slots only with
  `in_capture = true`.
- Feedback history is isolated by composition role: display and unfiltered
  capture have separate histories in addition to the existing node, slot,
  output, and renderer identity. Isolated toplevel captures also have their own
  capture-target identity. This applies to retained border and animation slots,
  including ancestor animations enclosing a suppressed window or overlay.
  An unfiltered pass must never read display history, which may already contain
  filtered pixels, or write or promote display history.
- Capture histories are allocated lazily only for shaders using previous-result
  sampling. The first captured frame, and unavailable history, use that pass's
  current input. Each role promotes its own history only after successful
  submission, at most once per rendered frame; auxiliary shadow passes may
  read that role's history but never advance it. Capture histories reset on
  capture-policy changes and the existing transition, program, output-transform,
  format, and renderer changes, and are released when the capture ends.

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

With no effect configured (merge gate): no additional compilation, allocations,
addons, scene nodes, timers, or clock reads relative to upstream. Existing
built-in lifecycle fades and their animation machinery remain available with
drag physics disabled. `Cursor` does one boolean check; `DragPhysics` is never
touched; scanout, damage, culling, reload, and renderer recovery follow unchanged
paths.

With effects active, documented in the design page: border (capture + program
per frame on the focused window; time-driven frames capped by `max_fps`; light
adds a pyramid and blur on outputs with visible emission), window (region copy
and a program pass per window per frame), screen and cursor (one output-sized
or radius-sized pass; direct scanout off on that output), drag physics (only
while held or settling),
persistent border/window effects (damage within drawn bounds, culling exceptions
only for affected nodes, scanout veto and offscreen buffers only on outputs
where visible), capture with `in_capture = false` (extra composition only on
frames with a pending capture, plus separate feedback buffers for capture passes
that sample previous results).

Verification per `docs/design/render-performance.md`: Tracy and
`vkmark --fullscreen` (immediate and fifo, panel hidden and visible) on upstream
`main` versus this branch with no effects; results must match. Repeat with one
effect of each kind and record numbers. Refresh stale `wlr_scene.c` line
references.

### Persistent-effect isolation verification

Use two outputs, A and B, with a persistent border effect visible only on A.
Disable transient animations for these checks and toggle the effect through
configuration reloads; runtime actions remain PR 2. Keep B's workload unchanged.
Verify each path separately; scanout success alone does not establish damage,
culling, or buffer isolation.
For the culling and offscreen-buffer runs, keep B compositing with a small
ordinary overlay so scanout cannot bypass the paths being measured.

- **Scanout:** keep A unable to scan out throughout, using an ordinary desktop
  with several visible nodes. Put an eligible fullscreen client on B and first
  confirm `Direct scan-out enabled` at debug log level. Repeatedly toggle the
  effect on A: no `Direct scan-out disabled` may follow. The existing log has no
  output name, so A's fixed ineligibility is necessary to attribute it to B.
- **Damage:** in a separate run with `WLR_SCENE_DEBUG_DAMAGE=highlight`, give B
  a small continuous update, such as a blinking terminal cursor or tiny animated
  client. With the effect on A, B's highlighted damage must remain confined to
  the small updating area. Highlight mode itself blocks scanout, so do not use
  this run for the scanout assertion.
- **Culling:** add a Tracy counter for render-list length labelled by output.
  Give B a fixed scene containing occluded nodes and confirm its render-list
  length and normal culling are unchanged while toggling A's effect. Also check
  that unrelated nodes on A retain normal culling.
- **Offscreen buffers:** add a Tracy zone or counter around
  `fx_render_pass_init_offscreen_buffers`, labelled by output. With blur and
  transient animations disabled and no effects on B, toggling A's effect must
  not introduce offscreen-buffer initialization on B. Observe A's local buffer
  demand and release separately from the render-list counter.

Repeat the isolation checks with a window effect. Verify movement across the
output boundary, effect removal, and border light spread: only outputs
intersecting the visible result acquire effect costs, and stale pixels are
cleared in the old bounds. Record results in the design performance notes.

## 6. Tests, docs, examples

### Unit (`tests/unit/`)

- `config_load.cpp`: kind parsing, per-kind key rejection, unknown and mismatched
  references, `off`, duplicate definitions across files, `overlay` kind, palette
  order, removed `shader` key, `windows_drag.physics`.
- `effects.cpp` (new): `ViewEffects` resolution and gating as pure functions;
  frame eligibility for advancing versus frozen instances, per-output
  visibility, cursor visibility, and session suspension.
- `drag_physics.cpp` (new): pinned grab, bounded displacement and velocity, no
  folding, settling after release and while held. Behaviour, not constants.

### umbrielfx

- `umbrielfx/tests/effects.c` (new) with `render_fixture.h` extracted from
  `color.c`: one synthetic shader per kind; entry point, preamble helpers,
  generic uniform binding including a type mismatch, in-place read-back, border
  `expand` and hole clipping, renderer-destroy handling; feedback isolation
  between display and unfiltered capture, first-frame fallback, and promotion
  only after successful submission.

### Harness

Inline synthetic shaders, `umbriel settle`, the animation clock, the pixel probe.

| Check | Asserts |
| --- | --- |
| `750_effect_border` | padding grows the drawn area; hole shows the client; effect and overlay follow focus; `border_effect = "off"`; time freezes with `clock-freeze`; light beyond padding over a neighbour |
| `751_effect_border_transform` | border `uv` and `umbriel_border_distance` on a rotated fractional-scale output |
| `760_effect_window` | translucent window's shader modifies the desktop seen through it at rest; inside an open capture it shades window content over the live desktop; backdrop-aware sampling returns after the capture ends; survives close snapshot; appears on overview cards; `window_effect` override |
| `770_effect_screen_cursor` | screen and cursor together; `screen_effect` override on the second of two outputs; cursor square follows pointer; lock detaches both; `in_capture` false and true via `grim` |
| `780_effect_reload` | missing file gives ordinary pixels and fixing it recovers; unknown and mismatched names reported with the setting off; `[colors]` change reaches the shader |
| `480_drag_physics` | deformation while held; settling after release; handover on close during drag |

Extend `750_effect_border` with effect-only frame-count assertions for a shader
that reads time: after initial damage settles, `animated = false`, `speed = 0`,
and a frozen clock each produce no effect-only frames; enabling an advancing
clock resumes them. Cover frozen snapshots in the eligibility unit tests.
Extend `770_effect_screen_cursor` to assert that hiding the pointer or moving
it off an output stops that cursor instance's effect-only frames after motion
damage settles.

Extend `770_effect_screen_cursor` with a feedback animation enclosing a window
effect. Start capture after display history contains the effect, exercise both
capture policies, and stop and restart capture. With `in_capture = false`,
captured pixels must exclude the window effect on the first and subsequent
frames; display feedback must match a run without capture at the same animation
clock steps. Exercise isolated toplevel capture with the same assertions.

Existing checks: every check using `shader =` moves to a preset plus `effect =`;
183 and 184 use `examples/effects/animation/{reveal,squash}/`;
`600_renderer_recovery` also asserts effects rebind.
Retain default opening/closing fade coverage with no effect selectors and drag
physics disabled, including after reload and renderer recovery.

### Examples

`examples/effects/<kind>/<name>/{shader.glsl,effect.toml}`, installed to
`share/umbriel/effects/`: `animation/reveal`, `animation/squash`,
`border/pulse`, `window/scanlines`, `screen/vignette`, `cursor/glow`. Each
`effect.toml` defines its preset and selects nothing. `examples/shaders/` is
removed. `examples/config.toml` gains commented, disabled examples only.

### Documentation

- `docs/user/effects.md` (new): every key, contract per kind, palette, window
  in-place behaviour. State explicitly that the window shader sees the desktop
  backdrop only when no animation capture encloses the window; inside one,
  shaded window content is composited over the live desktop. Lead with using
  bundled effects through includes and selectors; introduce defining presets
  and writing shaders afterwards.
- Updates: `animation.md`, `window-rules.md`, `outputs.md`, `configuration.md`.
- `docs/design/effects.md` (new): ownership, attachment, slot modes, light,
  capture, cost.
- `animation-shaders.md`: `effect =`, 13 slots. `render-performance.md`: new
  scanout conditions, refreshed line references.
- Anonymous, current-state only, MIT under the repository `LICENSE`.

### User-facing introduction

The named-preset model gives every effect the same two steps: define it once,
then select its name where it should apply. This makes reuse and overrides
consistent, and keeps animation timing separate from appearance. The tradeoff
is an extra definition and reference for someone enabling a single shader.
The user guide should make the common path small by starting with bundled
presets, whose definitions users can include without writing GLSL or preset
tables themselves.

Start `docs/user/effects.md` with this example, using the installed effects
directory for the user's package (shown here for an installation under `/usr`):

```toml
[include]
files = [
  "/usr/share/umbriel/effects/border/pulse/effect.toml",
  "/usr/share/umbriel/effects/animation/reveal/effect.toml",
]

[effects]
border = "pulse"

[animation.windows_in]
effect = "reveal"
```

Explain immediately that including a file only makes its preset available;
the `border` and `effect` selectors activate it. When adding this to an existing
configuration, append to its `[include].files` array and update its existing
tables. Follow with disabling a default using `""`, per-window or per-output
overrides using a preset name or `"off"`, and animation timing. Put the complete
preset-authoring example in a later section. Keep the corresponding example in
`examples/config.toml` commented and disabled.

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
