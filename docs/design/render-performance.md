# Render performance

Every output frame resolves to one of two costs: a page flip of the client's own
buffer, or a GLES composite into Umbriel's output buffer. Which one happens
decides how much GPU time a fullscreen client is left with, so it is the first
thing to establish when a frame rate is lower than expected.

## Scanout eligibility

`scene_entry_try_direct_scanout`
([`wlr_scene.c:4674`](../../umbrielfx/types/scene/wlr_scene.c)) is attempted only
when the render list holds exactly one entry, no color transform applies, no
gamma LUT upload is pending, SDR capture is off, and damage highlighting is off
(`:5360-5364`).

The fork adds three conditions upstream does not have:

- A transient animation slot anywhere in the scene vetoes scanout (`:4682`).
  The same predicate also disables visibility and opaque culling (`:5240`,
  `:687`) and forces whole-output damage (`:5310-5312`). It is scene-global,
  not per output and not per subtree, so one animating node changes the cost
  of every frame on every output until it settles.
- A persistent effect visible on the output vetoes scanout there (`:4683`).
  `render_data.persistent_visible` (`:5266-5286`) is set when a render-list
  entry sits under a node carrying a window, overlay, or border-effect slot, or
  when the output has a screen or cursor effect. Other outputs keep scanout;
  [Effects](effects.md#state-scanout-damage-and-culling) has the damage and
  culling rules.
- A node's `visible` region must equal its full rect (`:4690-4702`), because
  scanout bypasses `node->visible`. An ancestor tree clip therefore forces
  composition even when nothing overlaps the node.

Umbriel holds `wlr_output_lock_attach_render` while an output animates
([`output.cpp:1185-1188`](../../src/output/output.cpp)) and vetoes tearing for
the same frames (`:1228-1229`). Persistent effects trigger neither.

`direct_scanout = false` on an output, or `WLR_SCENE_DISABLE_DIRECT_SCANOUT=1`
process-wide, forces composition. Both are documented in the
[output guide](../user/outputs.md#direct-scanout) and exist for drivers that
mispresent scanned-out buffers.

## What puts a second entry in the render list

Occlusion culling drops nodes beneath a fully opaque one, so a fullscreen client
whose buffer is opaque normally leaves a single entry. Opacity comes from the
buffer's format or from the client's declared opaque region
(`wlr_scene.c:732-738`). What survives culling in practice:

- A per-surface blur node, created whenever the surface does not declare an
  opaque region covering its content box
  ([`surface_blur.cpp:63-67`](../../src/scene/surface_blur.cpp)).
  `surfaceTransparent` at `:39-44` consults only `wlr_surface::opaque_region`,
  so a client presenting an alpha-channel format without declaring one gets a
  blur node even though it never blends.
- A backdrop rect whose color does not match the scene background. The skip
  (`wlr_scene.c:4537-4546`) compares against `wlr_scene_set_background_color`,
  which the output clear also paints, so a matching rect renders nothing the
  clear would not. Measured on a headless output with one fullscreen client:
  7 entries with `#000000FF` and 7 with `#26233aFF`, where before the clear
  took the configured color it was 7 and 8.
- A backdrop rect of any color, on a fractionally scaled output, once anything
  is above it. The skip additionally requires `render_list->size == 0`, and the
  list is built top down, so "empty" means nothing is drawn over this rect. A
  fullscreen window is, so the skip cannot fire. That condition is upstream
  wlroots, added in `e34cc235`: fractional scaling expands the repaint region
  in `scale_output_damage()`, so a neighboring node can leak a pixel into the
  area a skipped rect would have covered (`swaywm/sway#8233`). Relaxing it
  reintroduces that leak. Confirmed on `3840x2160` at scale `1.25` with a black
  backdrop: `2 render list entries: 1 buffer, 1 rect`.
- Any visible layer-shell surface on the output.
- The overview, the session lock, the configuration banner, the cheatsheet, and
  the quit confirmation.

Occlusion makes every backdrop condition above moot. A fullscreen client whose
buffer the scene sees as opaque empties the rect's `visible` region, and it is
dropped at `:4576-4579` regardless of color, scale, or the fractional guard. So
backdrop rects only ever cost a client that presents an alpha-channel format
without declaring an opaque region. `vkmark` is such a client, which is worth
knowing before using it to investigate this.

Fractional scale does not by itself change the buffer size. A client that
implements `wp_fractional_scale_v1` and viewporter presents at the full mode
size and sets a logical destination, so no plane scaling is involved. A client
that ignores the protocol presents at the logical size, and then
`scene_entry_try_direct_scanout` stages a `buffer_dst_box` larger than the
buffer and asks the backend to accept primary-plane scaling
(`wlr_scene.c:4769-4791`). Hyprland rejects that case outright
(`bufferSize != m_pixelSize`, its `Monitor.cpp:1995`) but has no
fractional-scale condition of its own, because it has no background node to
skip and decides eligibility from window state instead of render-list
cardinality.

## Per-frame work outside the render pass

`Output::handleFrame` (`output.cpp:1124`) runs before any damage test:
`flushDirty`, `Server::tickAnimations`, `flushPendingViewOpacities` over every
view, and `WineColorManager::applySurfaceDescriptions`, which walks every
`wlr_scene_buffer` in the scene with a map lookup per buffer
([`wine_color_manager.cpp:1061-1099`](../../src/server/wine_color_manager.cpp)).

`wlr_scene_output_send_frame_done` at the end of that function is unconditional
and must stay so (`output.cpp:1394`). Mailbox and FIFO clients block on
`wl_surface.frame`, so skipping it on the nothing-to-render path stalls them
permanently.

## Renderer loss recovery

UmbrielFX checks `glGetGraphicsResetStatusKHR` when a buffer render pass begins.
A reset emits the renderer's `lost` signal, but the listener only queues a
one-shot event-loop idle callback. Recovery cannot destroy the renderer from
the listener itself: `wl_signal_emit_mutable` still owns temporary listeners in
that signal, and the failed render call still has the renderer on its stack.
Loss notifications observed before the idle callback runs are coalesced into
that one recovery.

Once the lost-signal callback and active render call have unwound, the idle
callback creates a renderer and allocator, moves the lost listener, rebinds the
compositor and every output, schedules fresh frames, then destroys the old
objects. Shutdown cancels a pending recovery, and a failure to queue or
construct the replacement terminates the compositor without attempting
synchronous teardown.

[`600_renderer_recovery.sh`](../../tests/harness/checks/600_renderer_recovery.sh)
emits two lost notifications in one dispatch and requires exactly one completed
replacement followed by a drawn frame. The headless check covers signal
lifetime, coalescing, and renderer rebinding. It does not exercise a hardware
or driver reset, so reset detection and recovery on a physical GPU still
require a running-session check.

## Zones

A `tracy` build carries CPU zones at those boundaries through
[`src/core/tracy.h`](../../src/core/tracy.h): `Output::handleFrame`,
`Output::flushDirty`, `Output::render`, `Server::tickAnimations`, and
`WineColorManager::applySurfaceDescriptions`. umbrielfx carries paired CPU and
GPU zones across its render pass. Every `wlr_scene_output_build_state` exit
emits a frame mark, including the scanout one, so a capture separates
scanned-out frames from composited ones: a scanned-out frame has a frame mark
and no render-pass zones. Build and capture instructions are in
[CONTRIBUTING.md](../../CONTRIBUTING.md#profiling).

umbrielfx adds these zones for effect costs:

| Zone | Where | Fires | Text |
| --- | --- | --- | --- |
| `render list` | `wlr_scene_output_build_state`, [`wlr_scene.c:5247-5257`](../../umbrielfx/types/scene/wlr_scene.c) | Every render-list build, including the scanout ones | Output name and render-list length, such as `HEADLESS-2 4` |
| `fx_render_pass_init_offscreen_buffers` | `:5550-5559` | Only when the output initializes offscreen buffers: blur, a transient slot, or `persistent_visible` | Output name |
| `scene_node_bounds` | `scene_node_update`, `:1510-1512` | Every enabled-path node update | |
| `scene_node_drawn_expand` | `:1443-1449` | Node updates while the scene has effect state | |
| `draw_animation_texture` (CPU and GPU) | [`fx_pass.c:676`](../../umbrielfx/render/fx_renderer/fx_pass.c) | Every effect program draw: animation slots, border, window, screen, and cursor effects, border light, and the shadow of a slot program | |

`tracy-csvexport -u` prints each CPU zone event with its text in the `value`
column, and `-g` prints each GPU zone event. Every zone text ends with a
`Success On Line` line that `TRACY_ZONE_END` appends, so read the first line.
A zone's time includes that formatting, which matters only for the sub-microsecond
update zones.

## Workloads

An uncapped client's frame rate on Wayland is bounded by how fast the
compositor releases superseded buffers, not by how fast it presents. A client in
immediate or mailbox mode reaches tens of thousands of frames per second while
the output still presents at its refresh rate, so almost every frame it counts
was discarded. Measured on a 165 Hz output, a 7800 XT, and a visible window:

```
vkmark -s 1920x1080                            shading  60775 FPS   0.016 ms
vkmark -s 1920x1080 -b effect2d:kernel=blur    effect2d 16106 FPS   0.062 ms
vkmark -s 1280x720  --present-mode fifo        shading    165 FPS   6.061 ms
```

So pick the workload for the question:

| Question | Workload | Reading |
| --- | --- | --- |
| Did the commit and buffer-release path regress? | `vkmark --fullscreen --present-mode immediate` | The absolute number, which is per-commit compositor CPU cost and nothing else. Very sensitive, and unrelated to composite cost. |
| Is presentation smooth? | `vkmark --fullscreen --present-mode fifo` | Frametime, not FPS. FPS pins to the refresh rate whenever nothing is missed, so only the frametime spread and the missed-frame count carry information. This is the stutter instrument. |
| Does the XWayland path cost anything? | Not answerable this way | An X11 client's frame counter measures its swaps to Xwayland, which Xwayland absorbs independently of Umbriel's buffer release, so the number is not comparable to a native client's. Measured at an identical `1280x720` surface: `glmark2-es2-wayland` scored 50140 and `glmark2-es2` scored 65347. The X11 path is not 30% faster; it is measuring a different loop. |
| Why is a game slower here than elsewhere? | The game | Nothing lighter is GPU-bound enough for the compositor's GPU share to show up in the client's own frame rate. Read the compositor's cost from its zones instead, and the client's from an external overlay. |

`vkcube` prints no frame rate at all, and its `--c <framecount>` exits after that
many submissions rather than that many presentations, so timing it measures
nothing useful.

Run each with the panel hidden and again with it visible. That pair is the
cheapest test of whether the render list still holds one entry.

### Headless measurements

Measured 2026-09-26 on a laptop with an Intel Arc (ARL) iGPU and an NVIDIA RTX
5060 Laptop GPU, both builds in `tracy` mode: upstream `7fb0ba44` and this
branch. Each instance runs headless with two 1280x720 outputs at 60 Hz, the
renderer pinned to the Intel render node, and `vkmark` 2025.01 on the same
device:

```sh
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDER_DRM_DEVICE=/dev/dri/renderD129 \
  build-tracy/umbriel -c config.toml
vkmark -D <intel-uuid> --fullscreen --present-mode immediate -b shading:duration=5
```

Render node numbers follow probe order; `/sys/class/drm/renderD*/device/driver`
names the driver behind each. `tracy-capture` and `tracy-csvexport` 0.13.1 come
from `nix shell nixpkgs#tracy`, and the client library is the same version,
built as [CONTRIBUTING.md](../../CONTRIBUTING.md#profiling) describes. Frame
rates are taken with the profiler disconnected, three runs each. Zone numbers
are medians over 2 to 4 second captures.

Commit path, fullscreen `vkmark`, no effects:

| Workload | Upstream | Branch |
| --- | --- | --- |
| `immediate`, panel hidden | 5954, 5994, 6098 FPS (0.164-0.168 ms) | 6044, 6086, 6075 FPS (0.164-0.165 ms) |
| `immediate`, panel visible | 6024, 6055, 6009 FPS (0.165-0.166 ms) | 6053, 6062, 6021 FPS (0.165-0.166 ms) |
| `fifo`, panel hidden or visible | 61 FPS, 16.393 ms, every run | 61 FPS, 16.393 ms, every run |

The render list holds one entry with the panel hidden and with it visible,
because the harness panel is a top-layer surface and a fullscreen window covers
it. `umbriel tearing --json` reports the same state on both builds, and neither
logs a `Direct scan-out` line: the headless backend never scans out.
Refresh-locked `fifo` numbers on hardware are not measured here (needs a TTY
session with two physical outputs).

Composition per effect kind, a tiled 800x600 `vkmark` in `immediate` mode with
the pointer over it and the bundled presets from `790_bundled_effects`:

| Effects | `vkmark` FPS | `Output::render` CPU | Program draws per frame | GPU per draw |
| --- | --- | --- | --- | --- |
| None, upstream | 7158, 7066, 7138 | 132-144 µs | | |
| None, branch | 7164, 7151, 7094 | 120-130 µs | | |
| `border = "pulse"` | 6203, 6292, 6264 | 275 µs | 5 | 70 µs |
| `window = "scanlines"` | 6498, 6585, 6577 | 243 µs | 4 | 44 µs |
| `screen = "vignette"` | 6919, 6967, 6852 | 156 µs | 1 | 62 µs |
| `cursor = "glow"` | 7157, 7122, 7152 | 122 µs | 1 | 12 µs |
| All four, plus `reveal` and `squash` | 5944, 5996, 5914 | 305 µs | 9 | 61 µs |
| `reveal` on four opening 600x400 windows, no `vkmark` | | 99 µs | | 65 µs |

The no-effect `Output::render` ranges span three captures per build; single
captures of the same build vary by about 40 µs. GPU times are
`draw_animation_texture` GPU zones.

A physics drag of a large window, on one 3840x2160 output with a 3000x1700
floating window dragged with Super and the left button through 60 pointer
moves over about 3 seconds, blur off, shadows on, running clock:

| `[animation.windows_drag] physics` | Frames in 2 s | `Output::render` CPU | Program draws per frame | GPU per draw |
| --- | --- | --- | --- | --- |
| `true` | 126 | 203 µs | 4 | 2.5 ms (2 to 4 ms) |
| `false` | 127 | 85 µs | 0 | |

The deformation program and the shadow it re-captures run through
`draw_animation_texture`, so their GPU cost is that zone's: about 10 ms per
frame on this iGPU at 4K, inside the 16.7 ms budget, so the output keeps its
60 Hz.

## Persistent-effect isolation

A persistent border or window effect costs only the outputs where its result
is visible. Four runs check that, each path separately, with two outputs: A
carries the effect and B a workload that stays the same. Transient animations
are off (`[animation] enabled = false`), blur is off, and the effect is toggled
by editing the configuration and running `umbriel msg config-reload`.

- **Scanout.** A shows an ordinary desktop with several visible windows, so it
  never scans out. B runs an eligible fullscreen client, such as
  `vkmark --fullscreen --present-mode fifo`, whose `render list` zone reads
  `1`, and `~/.cache/umbriel/umbriel.log` shows `Direct scan-out enabled`.
  Toggling A's effect at least three times must add no
  `Direct scan-out disabled` after that line:
  `sed -n '/Direct scan-out enabled/,$p' umbriel.log | grep -c 'Direct scan-out disabled'`
  prints `0`. The log names no output (`wlr_scene.c:5382`), so A's fixed
  ineligibility is what attributes the lines to B. Highlight mode cannot serve
  this run: scanout requires damage highlighting off (`:5360-5364`), so every
  output composites while it is on.
- **Damage.** With `WLR_SCENE_DEBUG_DAMAGE=highlight`, B shows a small
  continuous update. Screenshots of B with A's effect off, on, and within
  50 ms of each toggle must show highlight only inside that update.
- **Culling.** B holds a tiled `vkmark` and a floating window over it, so it
  composites every frame with occluded content. The `render list` zone's length
  for B must not change across toggles, and A's may change only by the entries
  of the window carrying the effect.
- **Offscreen buffers.** The `fx_render_pass_init_offscreen_buffers` zone must
  never fire for B while A toggles, and must fire for A only while the effect
  is on.

The same runs repeat with a window effect, then with the window moved across
the boundary, the effect removed, and a border light with `spread = 128` next
to the boundary.

Results, measured 2026-09-26 on this branch with the headless setup above. B is
`HEADLESS-2` (layout x 0 to 1280) and A is `HEADLESS-1` (1280 to 2560). A holds
two tiled 400x300 windows, the focused one carrying the effect. For the damage
run, B holds a `foot` window printing a counter every 50 ms.

| Run | Effect on A | Metric | Result |
| --- | --- | --- | --- |
| Scanout | `border = "pulse"` | `Direct scan-out disabled` on B after `enabled` | Not measured here (needs a TTY session with two physical outputs). The headless backend never scans out. |
| Damage | `border = "pulse"` | B's highlight bounds over nine screenshots | 35x13 at 290,10 in all nine: the counter's cells. A shows highlight over the effect's 662x441 drawn bounds while it is on. |
| Damage | `window_effect = "scanlines"` | B's highlight bounds over nine screenshots | 35x13 at 290,10 in all nine. The program reads no time, so A shows no highlight once it is drawn. |
| Culling | `border = "pulse"` | `render list` length over three toggles | B: 6 in all 376 frames. A: 6 with the effect off, 7 with it on. |
| Culling | `window_effect = "scanlines"` | `render list` length over three toggles | B: 6 in all 376 frames. A: 6 with the effect off and on. |
| Offscreen buffers | `border = "pulse"` | Zone events per output over three toggles | B: 0 of 376 frames. A: 201 of 376, the frames with the effect on. |
| Offscreen buffers | `window_effect = "scanlines"` | Zone events per output over three toggles | B: 0 of 376 frames. A: 143 of 319. |
| Boundary move | `window_effect = "scanlines"` on a floating 400x300 window | Zone events per output, 2 s at each position | On A: A 128 of 128, B 0 of 127. Dropped across the boundary at x 1120 to 1520: B 128 of 128, A draws nothing (0 entries), because a dragged window joins the output under the pointer and draws only there. Moved onto B: B 126 of 126, and A has no lit pixel left. |
| Effect removal | `window_effect` reloaded to `"off"` with the window on B | Zone events on B; the window's pixels | B 0 of 128. The window's mean color is its fill, `85 119 170`. |
| Light | `border = "pulse128"`: `pulse` with `light.spread = 128`, window 40 px from the boundary | Zone events per output; B's render list; B's pixels in x 1100 to 1280 | A 128 of 128, B 0 of 129. B's render list gains one entry (7 against 6), and B draws the light's tail: 11796 pixels in that strip at 1 to 2 of 255. |
| Light moved away | Window dragged 700 px right | The same | B 0 of 127, render list 6, and no lit pixels left in the strip. |

So an output the light reaches without the ring pays one render-list entry and
the light draw, and no offscreen buffers.

The descendant-expand walk `scene_node_update` runs while the scene has effect
state costs the same order as its existing `scene_node_bounds` walk:

| Workload | `scene_node_bounds` | `scene_node_drawn_expand` |
| --- | --- | --- |
| Border toggles, 55 updates | 164 ns | 84 ns |
| Window-effect toggles, 15 and 8 updates | 505 ns | 294 ns |
| Physics drag at 4K, 178 updates | 1545 ns | 706 ns |

Medians, each including the zone's end-text formatting.

## Measurement caveats

- Confirm the surface is presented before trusting any uncapped number. A `fifo`
  run that does not pin to the output's refresh rate means the surface is not
  reaching the screen: Umbriel culls invisible nodes outright
  (`wlr_scene.c:4529`), and a culled surface has its buffers released
  immediately, which looks like an excellent frame rate.
- A connected profiler reschedules every output frame as soon as the previous
  one lands (`wlr_scene.c:4287-4290`), so the compositor renders continuously
  instead of on damage. Zone costs stay comparable; frame rate and idle
  behavior do not. Take frame rates from an external overlay with the profiler
  disconnected.
- Scanout state is logged only on transitions, as `Direct scan-out enabled` or
  `disabled` with no output name (`wlr_scene.c:5382`). `prev_scanout` starts
  false, so an output that never scanned out once logs nothing at all rather
  than logging a refusal, and on a multi-output machine the lines cannot be
  attributed. Every build records them, since the log file is unfiltered and
  `wlr_log_init` asks wlroots for every level (`src/main.cpp:303-305`). The
  neighboring `types/output/render.c` lines are wlroots' render lock, which
  Umbriel takes while an output animates, not the scene's decision.
- `umbriel tearing` reports per output `last_commit_tearing`,
  `last_presentation_presented`, and a `fallback_reason` naming whichever
  tearing veto applied. It says nothing about scanout.
- GPU zones need `EXT_disjoint_timer_query`. Without it the GPU timeline is
  empty and the CPU timeline is unaffected.
