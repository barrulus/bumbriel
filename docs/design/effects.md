# Effects

Effects are the GLSL programs selected through `[effects]`, window rules,
output tables, and animation events. Configuration, the shader interface, and
user-visible behavior are in the [Effects guide](../user/effects.md); the
animation slots they share a renderer with are in
[Custom animation shaders](animation-shaders.md). This note records who owns
effect state, where programs attach, and which rendering costs an effect may
change.

## Ownership

### `EffectRegistry`

[`EffectRegistry`](../../src/scene/effect_registry.h) is a `Server` member and
the only place programs are compiled
([`effect_registry.cpp:153-229`](../../src/scene/effect_registry.cpp)).

- `prepare()` runs at startup (`server.cpp:335`), on a reload that sets the
  `animation` or `effects` flag (`server_events.cpp:499-501`), and after
  renderer recovery (`server_events.cpp:764`). No render callback compiles.
- Only referenced presets compile: a top-level selector, a window rule, an
  output's `screen_effect`, an enabled animation event's `effect`, or the
  `overlay` of a referenced border preset (`:121-151`). Entries are keyed by
  preset name and recompile only when the kind or source text changes;
  unreferenced entries are dropped. A failed compile logs one error and stays
  cached as null, so every lookup of that name renders plainly. A new renderer
  discards every program.
- The built-in fade compiles whenever `windows_in` or `windows_out` fades
  through it, independently of presets, and is the only program marked
  shape-preserving (`:204-214`). The drag deformation program compiles only
  here, and only while animations and `windows_drag.physics` are on
  (`:215-226`).
- `fillTimeUniforms` (`:323-343`) adds `umbriel_time` only to a program that
  reads it and the `[colors]` palette only to a preset with `palette = true`.
- `EffectLedger` ([`effect_ledger.h:12-58`](../../src/scene/effect_ledger.h))
  holds one `EffectInstanceState` per owner: the border node, the surface node
  for its window slot and that node's `addons` member for its overlay slot
  (`effects.cpp:151-152`), and the output and its cursor-owner member for its
  screen and cursor slots (`output.cpp:148`, `:160`). Each records its driving
  output,
  whether it is visible there, whether its program reads `umbriel_time`, and
  whether its clock advances. `eligible(output)` counts instances that are all
  three and is 0 while suspended; `active()` counts owners. `updateInstance`
  schedules an output's frame when its eligible count leaves zero
  (`effect_registry.cpp:250-260`).
- `syncLightLayer` (`:264-269`) keeps the light layer only while a compiled
  border preset defines `light`. `cursorEffectActive()` is true only while the
  default cursor preset compiled and the ledger is not suspended.

### `ViewEffects`

[`ViewEffects`](../../src/view/effects.h) is a `View` member.

- `resolve()` takes the resolved window rule's `border_effect` and
  `window_effect` over the `[effects]` defaults; `off` and `""` select nothing
  ([`effects_rules.cpp:6-21`](../../src/view/effects_rules.cpp)).
  `View::applyDynamicRules` calls it (`view.cpp:4783`), so rule and
  configuration changes re-resolve.
- `borderEffectApplies` (`effects_rules.cpp:23-25`) opens the border-effect
  slot only for a focused, decorated, non-urgent, non-fullscreen window. The
  border preset's `overlay` is bound only while that slot is, on the border's
  clock.
- `apply()` ([`effects.cpp:55-153`](../../src/view/effects.cpp)) binds the
  border-effect slot on the border tree and the window and overlay slots on the
  toplevel's surface tree node. The same window slots are bound in the view's
  isolated capture scene when `in_capture` is on and cleared there otherwise
  (`:122-130`).
- `View::syncAnimationShaders` (`view.cpp:1409-1509`) is the one entry point
  for the live view and its overview card: `Overview::syncCardEffects`
  (`overview.cpp:568-581`) passes the card's tree, border, first surface
  buffer, focus gate, and output. Its persistent block runs only when a preset
  is selected or the ledger has owners (`view.cpp:1481`).
- Unmap clears the window slots from both surface trees and removes the view's
  ledger instances (`view.cpp:3257-3263`, `:3281`).

### `Output`

`Output::applyOutputEffects`
([`output.cpp:116-168`](../../src/output/output.cpp)) sets the capture
policy, binds the output's screen preset (its `screen_effect`, else the
default) and the cursor preset, and records both in the ledger. It pushes the
pointer after setting a cursor program, because a new cursor program draws
nothing until a pointer update follows it. It binds no program while the
ledger is suspended, and returns before any scene call when no preset is
defined and the ledger is empty. The cursor square draws only on the output
holding the pointer: a pointer outside the output, or hidden, leaves that
output's cursor slot inactive (`wlr_scene.c:4180-4184`, `:4198-4200`). The
output also owns the effect frame timer ([Frames](#frames)).

### `Cursor`

`Cursor::forwardEffectPointer`
([`cursor.cpp:280-284`](../../src/input/cursor.cpp)) reads
`cursorEffectActive()` and forwards the pointer only when it is true. The
registry pushes the position to every output and re-applies output effects when
the output under the pointer or the pointer's visibility changes
(`effect_registry.cpp:237-248`). A move grab calls drag physics only when
`View::beginDragPhysics` accepted it (`MoveGrab::physics`).

## Attachment

Every node carries up to 13 slots
([`effect.h:18-39`](../../umbrielfx/include/umbrielfx/render/effect.h)).
Descendant slots compose before ancestor slots; on one node, slots compose in
index order. Slots 0-2 are persistent; the rest are transient, including drag.
The class follows the slot, not whether its program reads time.

| Index | Slot | Node | Class | Mode |
| --- | --- | --- | --- | --- |
| 0 | window | toplevel surface tree node, and its counterpart in the isolated capture scene | persistent | in place |
| 1 | overlay | as window | persistent | in place |
| 2 | border effect | view border tree | persistent | capture, `expand` |
| 3 | border | view border tree | transient | capture |
| 4 | dim unfocused | view content tree | transient | capture |
| 5 | windows_move | view content tree | transient | capture |
| 6 | drag | view content tree | transient | capture, `expand` |
| 7 | windows_in | view content tree | transient | capture |
| 8 | windows_out | close snapshot root | transient | capture |
| 9 | scratchpad | view content tree and backdrop targets | transient | capture |
| 10 | layers | layer tree or its close snapshot | transient | capture |
| 11 | workspaces | output workspace view root | transient | capture |
| 12 | overview | per-output overview tree | transient | capture |

An overview card carries the view's slots, except drag, on its tree, border,
and first surface buffer.

`wlr_scene_node_copy_animations_for_snapshot`
([`wlr_scene.c:1762-1781`](../../umbrielfx/types/scene/wlr_scene.c)) copies
each populated slot with its current parameters, moves its feedback history,
and turns light off. Slots from `windows_out` up land in `windows_in`; the rest
keep their index. Nothing updates the copied slots' parameters, so their time
stays frozen, and a snapshot owns no ledger instance, so it never requests
frames.

| Source | Snapshot node | Slots |
| --- | --- | --- |
| View content tree | snapshot root (`view.cpp:2480`) | content-tree slots, then `windows_move` is cleared |
| View surface tree node | snapshot content tree (`view.cpp:2483-2487`) | window, overlay |
| View border tree | each copied border (`border_rect.cpp:39-41`) | border effect, border |
| Card surface buffer, border, tree | copied buffer, copied border, snapshot root (`overview.cpp:1108-1136`) | as the live card |
| Layer tree | snapshot root (`layer_surface.cpp:197`) | layers |

Drag physics binds the built-in deformation program to the content tree's drag
slot with `umbriel_deformation[16]` and an `expand` of the sheet's
displacement bound plus 2 px (`view.cpp:1448-1468`, `:1128`). The sheet spans
the content tree's drawn bounds from `wlr_scene_node_effect_bounds` and is
refit on every tick (`view.cpp:1084-1110`); a re-grab while it settles keeps
the sheet and its transition. `View::animatesOn` includes every output the
drawn box reaches (`view.cpp:1624-1628`). The program is not shape-preserving,
so `render_animation_shadow` (`wlr_scene.c:3454-3524`) captures the content
tree and the drop shadow follows the deformation within the shadow node's own
region. A close mid-drag moves the drag slot to the snapshot root, and
`CloseSnapshot::applyShrink` grows its tree clip by that slot's `expand`
(`server.cpp:1134-1137`), so `popin` and `zoom` keep the frozen deformation.

## Slot modes

**Capture.** `render_animated_range` (`wlr_scene.c:3697-3851`) renders the
node's contiguous descendants into an offscreen buffer per capture slot, runs
their own effects first, then composites through each program over the node
bounds grown by the largest `expand` among the node's border-effect and drag
slots (`fx_slot_expands`). A border-effect composite receives the border's hole
and radii (`scene_border_geometry`, `:3529-3560`), and the preamble cuts the
hole out of its result. A persistent capture slot runs only when this frame's
damage reaches its drawn box (`:3744-3756`); otherwise nothing composites and
its history carries over.

**In place.** After the subtree is drawn, `render_in_place_slots`
(`:3614-3682`) calls `fx_render_pass_effect_in_place`
([`fx_pass.c:1228-1268`](../../umbrielfx/render/fx_renderer/fx_pass.c)),
which copies the current target under the node's rectangle into the output's
`in_place_source` offscreen buffer and runs the program with that copy as
`umbriel_sample`. The result is written back unblended through `umbriel_mask`,
which rounds the rectangle with the corner radii of the node's first surface
buffer. When that buffer has a corner box (content inside client-side margins),
the rectangle is the corner box within the node bounds, with square corners
wherever the bounds cut it; without one, the rectangle is the node bounds,
margins included. A failed copy skips the write-back and logs once. In-place
slots never run in the unfiltered capture composition ([Capture](#capture)).

**Window sampling contract.** An in-place slot reads the current target. At
rest, with no enclosing capture, that is the output framebuffer after the
window is drawn: the program sees, and may rewrite, the desktop visible through
a translucent window. Inside an enclosing capture (drag, open, close, move,
workspace, overview), it is the capture buffer: the program shades the
window's own content, and the result is composited over the live desktop. The
desktop beneath is not captured and not moved with the window. The overlay
slot follows the same contract.

## Border light

- **Layer.** `Server::setEffectLightLayer`
  ([`server.cpp:927-938`](../../src/server/server.cpp)) creates a scene-root
  tree directly above the drag-icon tree and below the top shell layer, and
  registers it with `wlr_scene_set_effect_light_layer`.
- **Proxy.** `scene_light_sync` (`wlr_scene.c:1234-1295`) keeps one
  input-transparent rect per lit border slot in that layer, covering the
  border's bounds grown by `ceil(2 × spread + 8)` logical px plus the slot's
  `expand`. The rect carries visibility, output membership, and damage; the
  renderer draws the light in its place (`:3009-3025`).
- **Emission.** Each display composite of the slot runs `emit_light`
  (`fx_pass.c:969-1038`): the program again, unblended and without a history
  write, into a full-resolution emission texture (half float when the renderer
  can filter it); a threshold pass into level 0 of a half-resolution pyramid
  with the proxy's margin; then Kawase down and up passes over 1 to 6 levels
  chosen from the spread. `fx_render_pass_add_effect_light` (`:1040-1064`)
  screen-blends level 0 over the proxy's box at `intensity`. The pyramid
  belongs to the slot and holds its latest composite; every output showing the
  proxy draws it. The helper program compiles once per renderer, on first use.
- **Suppression.** No proxy exists while the layer is absent, `intensity` or
  `spread` is 0, the border node is disabled, or its root-level ancestor sits
  above the light layer (fullscreen and pinned windows). Under a transient
  ancestor the proxy stays, disabled. Snapshots copy the slot with light off.
  Shadow captures and the unfiltered capture composition do not emit.

## Frames

An instance is eligible while it is visible on its output
(`wlr_scene_node_visible_in_box` against the output's layout box; the output
enabled for a screen slot; the pointer shown on that output for a cursor slot),
its program reads `umbriel_time`, and its clock advances (for a border or
overlay, `animated` and a nonzero `speed`; for all, an unfrozen animation
clock).

`Output::handleFrame` (`output.cpp:1148-1171`) treats a frame as an effect
frame when one was requested, or when an instance there is eligible and the
`max_fps` interval has elapsed (`effectFrameDelayMs`,
[`frame_schedule.h:35-44`](../../src/output/frame_schedule.h); 0 follows the
refresh rate), whatever scheduled the frame. The output's effect time,
`Output::effectSeconds()`, is stamped from the animation clock only on effect
frames, and while nothing on the output is eligible but a persistent preset is
referenced or the ledger has owners (`:1161-1162`), so it advances at most at
`max_fps` and a new instance starts from the present. After the frame,
`Output::armEffectFrame` (`:1030-1047`) requests the next frame at once or arms
a lazily created timer for the rest of the interval; with nothing eligible, or
while the session is locked, the timer is disarmed (`:1382-1386`).

Locking suspends the ledger and re-applies output effects, which unbinds
screen and cursor slots (`Server::activateSessionLock`,
`server_events.cpp:1337-1338`); unlocking resumes it and reschedules outputs
with eligible instances (`:1358-1366`). Border and window slots stay bound, and
the lock surface carries no slot.

A persistent effect never finishes, so it stays out of the animation registry.
`Server::settled()` (`server.cpp:1391-1419`), `Server::animationsActiveFor`,
and the render lock and tearing veto that follows it (`output.cpp:1182-1188`,
`:1228-1229`) see only transient animations: a time-reading effect never
blocks `settle` or holds `wlr_output_lock_attach_render`. The drag sheet ends,
so it is an animation. The `effect-frames` IPC
([Harness-only IPC](README.md#harness-only-ipc)) reports each output's effect
frames and eligible count.

## Capture

`Output::effectCapturePending` (`output.cpp:190-193`) is true while a capture
holds a render lock on the output, `in_capture` is false, and a referenced
in-place preset (window, overlay, screen, or cursor) compiled. It is keyed on
configuration because a close snapshot keeps window slots without a ledger
instance. Capture locks are the external attach-render locks minus the
animation lock and minus export-dmabuf frames on that output (`:176-188`), so
an export-dmabuf client reads the displayed frame.

With a capture pending and an in-place slot or output effect visible on the
output, `wlr_scene_output_build_state` composes twice (`wlr_scene.c:5266-5308`,
`:5611-5626`). The unfiltered composition skips in-place slots, output
effects, and light emission, runs capture composites (the border effect and
every transient slot) with capture-role histories, and draws the software
cursor; `fx_render_pass_save_effect_capture` (`fx_pass.c:2820-2854`) then
copies the target into the output buffer's effect capture. The display
composition then starts again from the background. This pass damages the whole
output. When the save fails, it logs once, and the unfiltered composition
serves display and captures alike for that frame, with no output effects
(`:5650-5653`). `fx_texture_from_dmabuf`
([`fx_texture.c:533-555`](../../umbrielfx/render/fx_renderer/fx_texture.c))
substitutes a valid effect capture for any import of that output buffer, which
is how screencopy and image-copy receive the unfiltered frame.

Feedback history
([`animation_history.h`](../../umbrielfx/internal/render/fx_renderer/animation_history.h))
is kept per animated node's slot, per output, per renderer, and per composition
role: 0 for display, 1 for the unfiltered capture (`wlr_scene.c:3836`). Each
entry holds two buffers, allocated only for programs that call
`umbriel_sample_previous`. A pass reads and promotes only its own role's entry;
a first frame, or a missing entry, reads that pass's current input. Promotion
waits for a successful submission (`fx_pass.c:273-283`), at most once per
frame; shadow captures read history but never promote it. A new transition or
program resets every role; a renderer, output transform, or format change
drops the affected entry's buffers. When a capture ends or the capture policy
changes, the output's capture-role entries and effect captures are released
(`wlr_scene.c:4057-4080`, `:5297-5303`), and
`Output::scheduleEffectCaptureRelease` draws one more frame so that happens
promptly.

Each view's isolated toplevel capture renders its own `wlr_scene`
(`view.cpp:212-215`), so its slots, histories, and policy counts never touch
the desktop scene. Window slots are bound there only with `in_capture = true`.

## State, scanout, damage, and culling

Each scene keeps a `scene_effects` addon on its root (`wlr_scene.c:151-206`)
listing its `scene_animation` addons with separate counts of nodes carrying
transient and persistent slots (`scene_animation_classify`, `:208-223`). The
addon exists while any node carries a slot or a light layer is registered, and
is destroyed with the last of them (`:359-364`, `:1313-1350`). An
output's `scene_output_effects` addon (`:3972-3988`) is created on first use:
a screen or cursor slot, `in_capture = true`, or an unfiltered composition.
`wlr_scene_output_build_state` looks the scene addon up once per frame. A
transient slot anywhere keeps the scene-wide conservative policy on every
output; persistent slots never contribute to it.

`render_data.persistent_visible` (`:5266-5286`) is true when a render-list
entry sits under a node with a persistent slot, or the output has a screen
effect or a shown cursor effect.

| Site | Transient slot in the scene | Persistent effect |
| --- | --- | --- |
| `scene_node_opaque_region` (`:684-769`) | No node is opaque. | A node at or under a node with slots contributes no opaque region; every other node keeps its own. |
| `scene_entry_try_direct_scanout` (`:4674-4684`) | Veto on every output. | Veto only where `persistent_visible`. |
| Animation-buffer release (`:5287-5289`) | Buffers kept. | Kept only where `persistent_visible`; released elsewhere. |
| `calculate_visibility` | Render-list culling off (`:5240`). | Culling stays on. The update pass keeps an occluded node under a persistent effect visible, so it keeps output membership and frame callbacks (`:1097-1105`); an entry whose visible region, grown by the effect's `expand`, reaches the output is kept (`:4562-4579`); the background-color skip exempts nodes under an effect (`:4540`, `:4552`). |
| Whole-output damage (`:5310-5312`) | Every frame. | Never from presence alone. `expand_damage_to_effects` (`:5102-5142`) grows commit (`:5350`) and render (`:5467`) damage to every effect box it touches, until nothing grows, because a program may read any texel of its box. Only the unfiltered capture pass damages the whole output (`:5305-5308`). |
| `fx_render_pass_init_offscreen_buffers` (`:5550-5559`) | Always. | Only where `persistent_visible`. |

A drawn box is the node's bounds grown by its `expand`, plus the light proxy
(`persistent_effect_box`, `:5050-5079`); a screen or cursor box is the output
or the cursor square. Changing a slot (`wlr_scene_node_set_animation`,
`:1623-1720`) updates the whole scene for a transient slot. For a persistent
slot it damages the drawn box before and after the change
(`scene_effect_damage`, `:1532-1553`) and re-runs `scene_node_update` on the
node when a slot appears or disappears. Destroying a node damages its effects'
margins first (`:1573-1581`). Whenever the scene has effect state,
`scene_node_update` (`:1470-1527`) grows its update and damage regions by the
largest `expand` on the node, its ancestors, and its enabled descendants
(`scene_node_drawn_expand`, `:1443-1449`), so moving a frame repaints a child
slot's old margin.

## Cost

With no effect selected and drag physics off:

- `prepare()` compiles nothing but the built-in fade, which the lifecycle
  settings alone decide; no deformation program, light layer, output-effect
  addon, effect timer, or ledger instance exists.
- Per view sync: `ViewEffects::configured()` and the ledger size
  (`view.cpp:1481`). Per output frame: an eligible count over the empty ledger;
  the clock is never read for effect time; `expand_damage_to_effects` returns
  at once; `Output::effectCapturePending` is false, so no second composition
  runs. Per pointer motion: one boolean (`cursor.cpp:281`).
- Drag physics: `Cursor` gates every call on `MoveGrab::physics`, and
  `View::tickAnimations` and `View::hasActiveAnimations` read two `DragPhysics`
  booleans per view per tick, with no writes, scene calls, or allocations.
- Scanout, damage, and culling take only the transient branches in the table
  above, as they do for built-in animations. Reload prepares only with the
  `animation` or `effects` flag; renderer recovery prepares the built-in fade.
- While any slot exists, built-in animations included, `scene_node_update`
  walks the updated node's ancestors and descendants for their largest
  `expand`.

With effects selected:

- **Border:** a capture and one program pass per frame on the focused window;
  effect-only frames for a time-reading program, capped by `max_fps`. Light adds
  a second program evaluation into a full-resolution emission texture, a
  half-resolution pyramid, and its blur on each display composite, and a blend
  on outputs showing the proxy.
- **Window and overlay:** a copy of the target under the window and one program
  pass per window per frame.
- **Screen and cursor:** one in-place pass over the output or the cursor square;
  direct scanout off on that output only.
- **Drag physics:** only while a window is held or settling; the drag slot is
  transient, so the scene-wide policy applies on every output for that time.
  The deformation program runs up to 22 inverse-lookup iterations per pixel.
- **Persistent border, window, and output effects:** damage within drawn boxes,
  culling exceptions only for affected nodes, and the scanout veto and
  offscreen buffers only on outputs where the result is visible.
- **`in_capture = false`:** a second composition on frames with a pending
  capture and a visible in-place slot or output effect, with whole-output
  damage, plus separate capture-role history buffers for programs that sample
  previous results.

## Regression coverage

Unit ([`tests/unit`](../../tests/unit)):

- `effects.cpp`: kind names, reference validation and `off`, name resolution
  and border gating as pure functions, padding, screen overrides, clock
  precision, and ledger eligibility for advancing and frozen instances,
  visibility, suspension, and output removal.
- `drag_physics.cpp`: the pinned grab, trailing motion, bounded displacement
  and velocity, no folding, contraction under resize, settling after release
  and while held, frame-rate independence, and re-grab continuity.
- `border_ring.cpp`: padding grows the ring box around the same hole.
- `config_load.cpp`: per-kind keys, missing kinds and inert presets, selectors
  at every level, deferred reference validation, palette order, duplicate
  presets across includes, the removed `shader` key, `windows_drag.physics`,
  the reload flag, and the bundled presets defining without selecting.
- `config_change.cpp` and `output_frame_schedule.cpp`: which edits raise the
  `effects` flag, and the `max_fps` delay.

umbrielfx (`meson test --suite umbrielfx`, cases `effects-*` from
[`umbrielfx/tests/effects.c`](../../umbrielfx/tests/effects.c)):

- Programs: `kinds`, `reads`, `uniforms`, `expand` (with its feedback history),
  `renderer-destroy`.
- Scene policy: `persistent-scene`, `occlusion`, `transient-policy`,
  `visible-in-box`, `effect-bounds`.
- Damage: `damage-confinement`, `whole-box-invalidation`, `damage-expansion`,
  `margin-damage`, `move-margin-damage`.
- Borders: `border-geometry`, `border-geometry-tree`, `border-light`,
  `border-light-lifecycle`.
- In place: `in-place`, `in-place-feedback`, `in-place-shape`.
- Capture: `capture-policy`, `capture-policy-encoding`, `capture-feedback`,
  `capture-composition`.
- Output effects: `output-effects`.

Harness ([`tests/harness/checks`](../../tests/harness/checks)):

| Check | Asserts |
| --- | --- |
| `750_effect_border` | Padding, the client hole, focus following, per-window `off`, the frozen clock, light over a neighbor, frames only while the clock advances, and the card's close snapshot. |
| `751_effect_border_transform` | Logical `uv` and `umbriel_border_distance` on a rotated, fractionally scaled output. |
| `752_effect_border_frames` | Effect frames follow the animation clock, stop under the session lock, respect `max_fps`, and advance while another animation drives the output. |
| `760_effect_window` | Backdrop sampling at rest, content shading inside an opening capture, the close snapshot, overview cards, `window_effect` overrides, and the overlay following focus. |
| `761_effect_window_capture` | `in_capture` for grim and toplevel captures, a reload flipping both, one live instance after remap, and frames only for time-reading programs. |
| `770_effect_screen_cursor` | Screen override per output, the cursor square following the pointer, frames stopping when it hides or leaves, lock detachment, and both capture policies. |
| `771_effect_capture_feedback` | With `in_capture = false`, captured frames exclude the window effect from the first, display history never composites a capture frame, and display feedback counts the same animation instants as a run without capture, for output and toplevel captures. |
| `780_effect_reload` | Recovery from a missing shader, unknown and mismatched names, `[colors]` reaching a palette without a recompile, and light layer removal and return. |
| `790_bundled_effects` | Every bundled preset compiles when selected, and selecting none keeps the compositor plain. |
| `480_drag_physics` | Deformation while held, settling, rigid overview cards, handover to the close snapshot, and nothing left running with physics off. |
| `600_renderer_recovery` | The built-in fade recompiles once per renderer and an animation preset rebinds after recovery. |
