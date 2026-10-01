# Lifecycle C0 experiment

The internal `src/scene/presentation.{h,cpp}` model exercises transaction
arbitration without introducing public configuration or enabling scene
rendering. Its deterministic unit fixture is
`tests/unit/scene_presentation.cpp`.

## Evidence available

The fixture verifies original lifecycle deadlines despite neighbour reflow;
whole-burst overlap fallback until native obligations settle; ownership of
separate display/unfiltered sources after the original owner disappears;
atomic rejection of missing workspace faces; output-local cancellation;
pointer and multi-touch dismissal sequence pairing; device removal; lock
between dismissal and release; held carousel composition without finite motion;
the complete 1/2/3/4/5/8/64 identity inventory; and gesture reversal/retarget.

Workspace sources must match the frozen inventory exactly. Workspace pairs
require two sources; sets never truncate an inventory. Window source extraction
and compatibility admission still belong to the renderer experiment.

The transition identity comes from `nextAnimationTransitionId()` or the existing
native event; callers must not reuse released identities. The model owns neither
a second identity generator nor an animation clock. Seat-owned dismissal state
outlives output leases, so cancelling or destroying an output cannot deliver an
unpaired release to its replacement target. Admission also rejects pending
dismissal sequences. Physical device identities are part of the pairing key.

Those are pure logic tests. Separately, the test-build-only
`presentation-input-probe <arm|cancel|status>` command exercises the same
dismissal guard in the compositor's actual pointer pipeline. The headless
`input/presentation_probe` fixture enables hover focus, moves between two real
Wayland clients, verifies the first complete press/release produces no client
button events, and verifies the next sequence produces exactly one press and
release. It also verifies hover suppression during the probe and refresh on
paired release and explicit cancellation. A disabled-guard negative control
requires the same complete sequence to reach the client. Physical and virtual
pointer destruction clears the corresponding swallowed buttons.

The input-only pointer fixture passes 32/32 concurrent stress instances. The
new `presentation-scene-probe` is a separate, test-build-only output owner. It
captures both desktop roles atomically after reserving output/aggregate budget,
borrows one native opening/close identity and deadline, and draws a displaced
retained source over an opaque backing. Renderer suppression is output-local;
native nodes remain enabled and retain their output membership. No new public
scene setting or source ABI is enabled.

The first `input/presentation_scene` run passes real pixel and client-delivery
assertions: the displaced corner is black, first dismissal restores the native
PNG exactly, no press/release reaches the client, hover refreshes on release,
and subsequent clicks reach the client once. Both clients' `wl_surface` output
enter/leave counts remain unchanged. The native identity and deadline remain
unchanged. `animation/presentation_overlap` initially passes close-snapshot
retention, overlap rejection through the whole burst, independent close reap,
and fresh-event admission. Expanded tests verify two surviving tiled windows'
presented boxes continue changing at the original close deadline while floating
neighbours remain stable. Real `wl_touch` delivery verifies two-contact
suppression, one subsequent activation, cancellation and device removal. All
four displaced pointer/touch/tiled/floating fixtures pass 16 concurrent copies
each (64/64). The separate third-window immediate-exit fixture also passes:
closing replaces the opening with its native close snapshot/deadline, another
opening preserves that obligation, and no queued presentation replays.

The project lead reviewed the preserved tiled, floating and touch recordings.
The bounded probe has one visible scale jump back to native, with no stale
displaced layer in subsequent frames; this is accepted as C0 probe evidence.
It is not appearance approval for authored water/portal effects or panel motion.
The restored-frame input barrier additionally waits for a successful ordinary
buffer commit. Its failed-submit fixture passes: two complete contact sequences
are swallowed while buffer commits are deliberately rejected, and one native
activation is delivered after a successful ordinary commit. Retained filtered
and unfiltered output-capture fixtures also pass while native renderer entries
are suppressed. Workspace pre-mutation cancellation passes with the same
failed-commit restoration barrier, and a hidden old opening is rejected as a
new source owner.

Touch injection uses a clearly named test IPC device registered through the
normal `Server::addTouch` and `wlr_cursor` attachment path. Its signals pass
through actual Cursor touch handlers and are observed by a real `wl_touch`
client. This proves compositor routing, not libinput or physical touch hardware.

Set `UMBRIEL_PRESENTATION_ARTIFACTS` when running the G4 fixtures to retain their
PNG/JSON steps and touch client log outside the harness's disposable directory.
Recorded handoff review remains a product gate. This narrow SDR single-output
probe does not establish workspace-pair/carousel behavior or production L0/L3.

## Runtime workspace inventory ownership

`WorkspaceInventoryHold` owns the complete native ID list and original active
ID by value. Only one hold is admitted per group; it allocates nothing while
inactive. Automatic dynamic reconciliation defers pruning and sentinel changes
until release. Explicit inventory replacement, reorder, activation or slide
invalidates the hold before mutation; callback teardown cannot re-enter pruning
or reacquire ownership during cancellation. Group destruction invalidates before
freeing any workspace, so an external handle cannot dereference the dead group.

The test-only `presentation-inventory-probe` supplies a native runtime owner for
`workspace/presentation_inventory`, covering 1/2/3/4/5/8/64 complete IDs, deferred
close-driven pruning, release, explicit reload, stable active identity after
renumbering, and output teardown. The fixture passes, with 16 concurrent copies;
workspace mutation cancellation and failed-submit restoration also pass 16
copies each (48/48). It does not activate a public carousel.

## Live workspace source preview

The internal production `WorkspaceSources` provider is exercised by the
test-only `presentation-workspace-probe open ID|select ID|cancel|status`, which
prepares a complete paired source inventory and presents one selected face.
Normal layout and configure readiness prepare never-visited hidden windows;
source capture itself preserves native visibility, geometry and output membership.
The provider reserves all retained images plus the maximum sequential capture
scratch, charges old images during atomic refresh, and tries only full, half and
quarter face resolution. It does not truncate the inventory. The native output
landing remains separately charged. Shared layer-shell/pinned strata preserve
viewport framing. This initial provider uses viewport source framing.

`workspace/presentation_sources` passes live blue-to-green client updates,
never-visited hidden geometry, unchanged active workspace and output membership,
paired-role callback deduplication, failed-output-submit callback suppression,
selection and exact native restoration. `workspace/presentation_source_inventory`
passes all 1/2/3/4/5/8/64 actual paired inventories. `effect/audio_workspace_source`
passes hidden original-owner demand, suppression of replaced native owners,
positive/zero pixels, immutable failed-submit input and captured revision,
queued fresh input after recovery, exact idle capture/commit counts and demand
restoration on release. No scene schema is enabled by these probes.

The selected-face provider shares the output audio latch and consumes it only
through final output success. Source capture never consumes it independently.
The first source fixture proved hidden callback ownership; a follow-up verifies
that output-local replacement also suppresses ordinary native callbacks and
paces selected active/pinned/shared sources after successful output submission.
The integrated callback, live source, full inventory and audio fixtures pass
together (4/4). A subsequent provider extraction run passes all five, including
client map/unmap during failed output submission.

Remaining limitations include source-view feedback histories, fit-all production
inventory framing, authored carousel composition, full output/HDR/recovery matrices and final production appearance review.
Native map/unmap and same-group migration now reconcile source owners while
retaining workspace IDs; explicit workspace inventory mutation still cancels.
The provider exposes its resource arena so final composition cannot obtain a
second independent budget. A separate native-resolution paired landing is
included in the same upfront preflight and has passed the complete inventory
fixture. The preview remains an integration experiment, not carousel acceptance.

## Native integration dependencies

- `View::handleMap` and `View::handleUnmap` in `src/view/view.cpp` must arbitrate
  before the participant set changes. `beginCloseAnimation` creates the native
  close snapshot before live animation/effect slots are cleared. Scene-source
  retention must happen there and must not change its event deadline.
- `Server::tickAnimations` in `src/server/server.cpp` ticks every native owner
  and then destroys completed `CloseSnapshot` scene trees. A renderer source
  must independently own retained visual resources before this reap; retaining
  a pointer to the snapshot or its nodes is insufficient.
- `Output::handleFrame` in `src/output/output.cpp` currently derives its render
  lock from finite native animations. Add a separate lease composition lock
  that blocks scanout/tearing while a held carousel still permits `settle`.
  This must be output-local and counted correctly by `externalRenderLocks`.
- `Cursor::processButton` in `src/input/cursor.cpp` must consume dismissal before
  configurable mouse actions, native focus, implicit grabs or Mod+drag. Its
  test probe carries physical pointer device identity through press and release;
  emulated tablet callers still need a distinct stable identity before production
  scene input is enabled.
  Existing `m_swallowedButtons` is keyed only by button and cannot replace the
  seat-owned presentation guard. The C0 probe installs the guard before this
  dispatch, without enabling it in builds lacking `UMBRIEL_TEST_IPC`.
- The test-only `Cursor::handleTouchDown/Up/Motion/Cancel` path now routes
  swallowed sequences before hit testing or `wlr_seat_touch_notify_*`. Device
  destruction clears only that device's swallowed contacts/buttons. Production
  activation still depends on a released presentation owner.
- `Cursor::processMotion` must suppress hover focus while the relevant output
  lease is active; final teardown must refresh cursor/focus against the committed
  native scene. Lock and teardown must retain pending swallowed releases until
  they arrive or their device disappears.
- `WorkspaceGroup::reconcileDynamic` in `src/workspace/workspace.cpp` must defer
  automatic empty-workspace deletion while an inventory is held. Explicit
  `reconcileInventory`, workspace destruction, output changes and ownership
  changes must cancel before identities are invalidated.

The blocking rendering dependencies are an identity desktop source (G1),
retained filtered/unfiltered visual resources, participant extraction and
admission including shadow/light companions (G2), and the output presentation
replacement path. An input-only synthetic flag can test event delivery, but
cannot prove dismissal over a displaced window or native-current overlap
handoff. Do not describe that narrower fixture as the recorded G4 experiment.

## Remaining recorded fixture

With those dependencies integrated, freeze the compositor clock, open the third
tiled window beside two moving neighbours, and record each frame through an
immediate close and another overlapping map. Verify the original close handle
and deadlines, exactly one native-current handoff, and no queued or replayed
animation. Repeat floating, with a neighbour motion longer than the trigger.

During a displaced frame, enable hover focus and send a complete pointer
press/release; assert zero client events and zero grabs, then assert exactly one
client activation from the next complete sequence against the native frame.
Repeat with multiple touch contacts and verify modal carousel accept/cancel,
lock, device removal and output teardown. Record the visible handoff for product
acceptance. Pure assertions cannot decide whether that discontinuity is
acceptable.

## Native regression found during integration

The complete suite exposed an existing fullscreen opening race in
`rule/fullscreen_opacity`. A tiled map deferred its opening until the next
workspace arrange. A fullscreen request received before that arrange changed
the presentation owner, but fullscreen participants are excluded from the tiled
admission pass. The deferred flag therefore never cleared: both the window and
its fullscreen backdrop stayed hidden even after configure and settle barriers.

The regression now queues the helper's fullscreen request in the same flush as
its map, making the failure deterministic. This version failed before the fix
with the original wallpaper pixel values, 85/119/170. `View::setFullscreen` now
resumes a deferred opening when fullscreen takes ownership. All existing pixel
assertions remain unchanged. The fixed fixture passes 32/32 concurrent stress
instances; fullscreen opening-centre and tiled opening-reflow fixtures also pass.

## Production window follow-up (2026-09-29)

The C0 limitations and insertion-point notes above describe the earlier probes.
Production `WindowPresentation` now owns the per-output lease and composes live
ordered participants with paired display/unfiltered sources, native shadows and
admitted border-light companions. It borrows the original opening/closing
clock, preserves independent neighbour motion, cancels overlap to native-current
presentation, and keeps the input restoration barrier until successful native
submission. Closing targets retain PR1's intentional no-light rule; surviving
participants still illuminate. Feedback-bearing participant effects explicitly
fall back as `feedback_effect` rather than being evaluated twice.

The Window7 checkpoint preserves configured tiled and floating overlap evidence
at `/tmp/umbriel-window-7-evidence/overlap-0` and `overlap-1`. Both fixtures pass.
The lead reviewed the `moving-neighbours`, `overlap-cancelled`, and
`original-close-deadline` PNGs and accompanying JSON: one expected handoff returns
deformed wave geometry immediately to native rectangles/current opacity, after
which native clocks continue. No stale target is visible at its original
deadline. This is acceptable image-level evidence of the bounded jump, not a
claim about physical temporal appearance. Physical session review remains open.

A final adversarial review found that the opening source override could remove
the independent old-size client-buffer crossfade. The source-only opacity now
multiplies its unfaded lifecycle-independent value by `ResizeCrossfade`'s own
factor. A real master-width action during opening also showed that ordinary
layout may reorder the same native content roots. Every frame now follows that
native order while requiring an unchanged participant-node multiset and stable
owner identities; additions, removals and replacements still cancel admission.

`animation/window_scene_resize_crossfade` changes the opening target's native
size without adding an owner. An isolated toplevel capture first verifies the
new green client buffer actually committed. Quarter/half desktop pixels then
verify the old red buffer fades independently, followed by the unchanged native
lifecycle deadline and green endpoint. Window7 passes 16/16 concurrent instances.
A factor-only negative control changes only the copied debug executable's
`ResizeCrossfade::opacityFactor` body to return `1.0f`, retaining the native-order
fix. It fails with an active scene and no fallback: both samples remain
255/0/0. The standalone patch script, exact symbol disassembly, hashes and log
are preserved in `/tmp/umbriel-window-7-negative`; no shared binary or source was
changed for this negative control.

The full-suite unready fixture also exposed a setup race in its client stimulus:
`HOLD_RESIZE=1` could withhold an initial tiled configure before the baseline
settled. The client now supports explicit `HOLD_RESIZE_CONTROL` commands, and the
fixture arms its hold only after normal startup/configuration has settled. It
waits for both the arm acknowledgement and an actual new held configure before
asserting the real-time preparation expiry with a frozen compositor clock.
Sixteen concurrent instances pass; no compositor timeout or assertion was
weakened. The existing immediate `HOLD_RESIZE` behavior remains available to
other fixtures.
