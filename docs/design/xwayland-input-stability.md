# Xwayland input stability

Umbriel supports X11 clients through wlroots' Xwayland server: managed X11
windows are views, and the compositor tells the X server where each one sits
on screen. X11 games are sensitive to compositor-side geometry churn, and
several Umbriel invariants exist to keep it away from them. Breaking any of
these reintroduces a bug class where an X11 game's mouse input dies in part
of the screen while everything looks correct.

## Output membership follows real geometry

wlroots emits enter/leave from scene-node visibility with a 10% overlap
threshold, and scale-aware clients size and map input from the outputs a
surface entered. Two situations cross that threshold:

- Any window whose layout box reaches past its output: a scrolling column
  scrolled off the shared edge, an unanimated snap move that jumps a node,
  a workspace slide, a close-fade snapshot.
- Interactive drags legitimately span monitors; every boundary graze enters
  the neighbor mid-drag. This is accepted as inherent to a drag (the drop's
  final enter settles it), see below.

Defense:

**Per-output clipped scene roots** (`Output::viewRoot`, `fullscreenRoot`,
`pinnedRoot`): each output owns three scene trees carrying
a `wlr_scene_tree_set_clip` of that output's layout box, and every workspace
tree, fullscreen tree, pinned view and close snapshot hangs under them.
`umbrielfx` folds an ancestor tree clip into the visibility walk
(`_scene_nodes_in_box` accumulates the clip, `scene_node_update_iterator`
intersects `node->visible` with it), so a node that reaches past its own
output has no visible region there at all: no rendering, no damage, no
membership, no enter event. There is no window position or move sequence that
can graze a neighbor, so no per-move mitigation is needed and none exists.
The drag tree is deliberately outside those roots, which is what lets a
dragged window span both outputs.

A rejected approach, for whoever considers it next: pinning scene nodes to a
single output inside `umbrielfx` (filtering `active_outputs` in
`update_node_update_outputs`) does suppress the enter/leave churn, but it
divorces output membership from rendering. `wlr_scene_output_build_state`
asserts on buffers rendered on an output they are not members of (SIGABRT
when a pinned window is dragged fully onto another output), and anything
keyed on `primary_output` (drop attribution, frame scheduling, dmabuf
feedback) silently misbehaves. Membership must stay derived from real scene
geometry, which is exactly why the clip changes that geometry instead of
filtering its result.

## Native focus fully leaves the X11 domain

wlroots deactivates the focused Xwayland surface by moving X core input focus
to `PointerRoot`. Its XWM also publishes a private no-focus window through
`_NET_ACTIVE_WINDOW`. That preserves X11 keyboard grabs and popups, but an X11
launcher can continue treating itself as eligible for controller input while a
native Wayland game has seat focus. Clearing only `_NET_ACTIVE_WINDOW` is not
enough: the X core focus target must leave `PointerRoot` too.

Umbriel handles an Xwayland-to-native keyboard transition in this order:

1. Deliver `wl_keyboard.enter` to the native surface.
2. Arm a one-millisecond Wayland event-loop timer, placing the X11 update on
   the next dispatch after the server flushes the native enter.
3. Recheck that seat focus is still a non-Xwayland surface.
4. Set X core input focus to `None`, set `_NET_ACTIVE_WINDOW` to `None`, and
   flush the XWM connection.

The delayed ordering matches xwayland-satellite's cross-process handoff. It is
also the focus state Hyprland uses for native clients. Performing the X clear
before the native enter can leave the native game unable to receive control. A
stale timer cannot clear a newly restored X11 focus because the callback
rechecks the seat, and Xwayland-to-Xwayland refreshes never schedule it.

Using `None` means X11 keyboard grabs are unavailable while a native client is
focused, which is why wlroots normally prefers `PointerRoot`. Umbriel limits
that tradeoff to a real Xwayland-to-native transfer and lets wlroots restore
normal X focus when the seat returns to Xwayland. This is the same externally
observable state as the previously working xwayland-satellite path.

## X11 games can retain stale input after a windowed resize round trip

A fake-fullscreen game (borderless window at output size) that receives a
compositor-imposed windowed size, then returns to fullscreen, can keep a stale
mouse mapping: X geometry, stacking, focus, and event delivery all recover,
but hover and clicks die outside the transient size. This is upstream Wine
behavior; the boundary of the dead zone always equals whatever transient size
the compositor sent.

Umbriel therefore avoids incidental windowed resize round trips while keeping
deliberate fullscreen exits authoritative:

1. **Float toggle** (`View::setFloating(true)`) keeps the current size when
   floating a fullscreen window (no transient resize), and records
   `m_refullscreenOnTile`.
2. **Re-tile** (`View::setFloating(false)`) restores fullscreen before the
   layout attach when that flag is set, so arrange sizes the column to the
   full output the client already has. A client that chose windowed mode
   itself clears the flag (`setFullscreen(false)` from any other path) and
   re-tiles as a regular column.
3. **Fullscreen exit** (`View::setFullscreen(false)`): a tiled view clears
   fullscreen and sets its restored column size in the same client configure.
   This applies equally to native Wayland and X11
   views. There is no timer and no size-0x0 probe, so an X11 client that keeps
   its fullscreen-sized buffer cannot make the compositor undo the action.
   Client requests that arrive later still use the normal fullscreen request
   path.

Real migrations (dropping a window on a differently scaled output) still
resize by necessity and can still break fragile games. A deliberate
fullscreen exit followed later by fullscreen entry also necessarily crosses
a windowed size. Those are upstream constraints, not transitions the
compositor can mask without ignoring the requested state.

## Verifying changes here

The clip primitive itself is covered by
[`tests/unit/scene_clip.cpp`](../../tests/unit/scene_clip.cpp): two headless outputs, a
buffer straddling the shared edge, and assertions on the enter/leave stream,
the primary output and hit testing as the clip is set, nested, and cleared.

Compositor-level containment is covered by
[`tests/harness/checks/output/two_output_containment.sh`](../../tests/harness/checks/output/two_output_containment.sh),
which declares `# harness: outputs=2` so the harness boots it a two-output
instance, and compares real framebuffers while a strip overflows the shared
edge. Run it as `just check output/two_output_containment`.

The shared fullscreen-exit ordering is covered by
`just check layout/fullscreen_exit_configure`: the first windowed configure
must already contain the restored tile size. That check does not exercise the
X11 resize path or multi-output X coordinate spaces, so the X11 path still
needs a running session with Steam or another X11 game. A fullscreen exit must
start the windowed resize immediately and remain windowed after the animation
settles. For the protected float and re-tile round trip, the signature to
reject is a ConfigureNotify pair through a non-fullscreen size; a passive
`StructureNotifyMask` monitor on `DISPLAY=:0` shows it directly.

The X11-to-native focus handoff is covered by
[`tests/harness/checks/focus/xwayland_native_handoff.sh`](../../tests/harness/checks/focus/xwayland_native_handoff.sh).
It starts the harness's private Xwayland server, transfers focus from a real
X11 window to a native client, and requires X core focus and
`_NET_ACTIVE_WINDOW` to both become `None`. It also verifies that the native
client receives keys, the old X11 client does not, and returning to X11 restores
both focus channels and key delivery. Run it as
`just check focus/xwayland_native_handoff`.
