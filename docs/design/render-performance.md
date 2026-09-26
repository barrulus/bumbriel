# Render performance

Every output frame resolves to one of two costs: a page flip of the client's own
buffer, or a GLES composite into Umbriel's output buffer. Which one happens
decides how much GPU time a fullscreen client is left with, so it is the first
thing to establish when a frame rate is lower than expected.

## Scanout eligibility

`scene_entry_try_direct_scanout`
([`wlr_scene.c:4670`](../../umbrielfx/types/scene/wlr_scene.c)) is attempted only
when the render list holds exactly one entry, no color transform applies, no
gamma LUT upload is pending, SDR capture is off, and damage highlighting is off
(`:5353-5357`).

The fork adds three conditions upstream does not have:

- A transient animation slot anywhere in the scene vetoes scanout (`:4678`).
  The same predicate also disables visibility and opaque culling (`:5236`,
  `:687`) and forces whole-output damage (`:5303-5305`). It is scene-global,
  not per output and not per subtree, so one animating node changes the cost
  of every frame on every output until it settles.
- A persistent effect visible on the output vetoes scanout there (`:4679`).
  `render_data.persistent_visible` (`:5259-5279`) is set when a render-list
  entry sits under a node carrying a window, overlay, or border-effect slot, or
  when the output has a screen or cursor effect. Other outputs keep scanout;
  [Effects](effects.md#state-scanout-damage-and-culling) has the damage and
  culling rules.
- A node's `visible` region must equal its full rect (`:4686-4698`), because
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
  (`wlr_scene.c:4533-4542`) compares against `wlr_scene_set_background_color`,
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
dropped at `:4572-4575` regardless of color, scale, or the fractional guard. So
backdrop rects only ever cost a client that presents an alpha-channel format
without declaring an opaque region. `vkmark` is such a client, which is worth
knowing before using it to investigate this.

Fractional scale does not by itself change the buffer size. A client that
implements `wp_fractional_scale_v1` and viewporter presents at the full mode
size and sets a logical destination, so no plane scaling is involved. A client
that ignores the protocol presents at the logical size, and then
`scene_entry_try_direct_scanout` stages a `buffer_dst_box` larger than the
buffer and asks the backend to accept primary-plane scaling
(`wlr_scene.c:4765-4787`). Hyprland rejects that case outright
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

## Measurement caveats

- Confirm the surface is presented before trusting any uncapped number. A `fifo`
  run that does not pin to the output's refresh rate means the surface is not
  reaching the screen: Umbriel culls invisible nodes outright
  (`wlr_scene.c:4525`), and a culled surface has its buffers released
  immediately, which looks like an excellent frame rate.
- A connected profiler reschedules every output frame as soon as the previous
  one lands (`wlr_scene.c:4283-4286`), so the compositor renders continuously
  instead of on damage. Zone costs stay comparable; frame rate and idle
  behavior do not. Take frame rates from an external overlay with the profiler
  disconnected.
- Scanout state is logged only on transitions, as `Direct scan-out enabled` or
  `disabled` with no output name (`wlr_scene.c:5375`). `prev_scanout` starts
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
