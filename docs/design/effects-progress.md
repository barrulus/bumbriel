# Programmable effects implementation evidence

This records implementation evidence against
[the delivery plan](effects-implementation-plan.md). It is not a release sign-off.
The working baseline is `b1e33849`; pre-existing design files remain intact.

## Current acceptance status

The later standard-config showcase review added projected carousel window
selection. Picking executes the retained authored vertex/fragment stages with
depth, resolves capture-time native window identities, and focuses only after
successful native restoration and the complete selecting input sequence.
Ambiguous source sampling and final composites remain keyboard/swipe selectable.
A real touch-release focus bug found by concurrent stress was fixed. Five
pixel-located regression variants (viewport, fit-all, deleted target, touch and
last-submitted-frame retention) pass **40/40 stress instances**; the earlier
binary fails these checks. The integrated follow-up passes **298/298 headless
checks**, **129/129 Meson tests**, **63/63 GPU tests on each of Intel and NVIDIA**,
changed-file lint, **23/23 workflow tests** and **31/31 benchmark/report tests**.
Logs are `/tmp/umbriel-carousel-full-{headless,unit}-final.log`,
`/tmp/umbriel-carousel-full-gpu.log`, `/tmp/umbriel-carousel-final-lint.log` and
`/tmp/umbriel-carousel-click-3/stress.log`. Composition tests now explicitly
wrap the picking APIs and cover committed/candidate texture and input ownership.
The optimized installed launcher binary is SHA256
`ea74352448ff1e52afef46032a9ff39aa00fb05a5b65fd0a21ca989a76ca8e11`;
both the standard and copied showcase configurations validate. Native click
review awaits restarting into that binary. The user confirmed the revised
Spotify spectrum border responds; its live validation is recorded in
[hardware validation](effects-hardware-validation.md).

The integrated implementation passes all required automated suites. The second
complete run under `/tmp/umbriel-effects-round2` passed 129/129 Meson tests, full
lint, both DRM renderer ownership checks, 23/23 workflow tests and, after fixing
an unready-source fixture's setup, 292/292 headless checks. Configured resize /
ordinary reflow passed 16/16 stress runs; held callback and seed regressions
passed 32/32 independently. These checks include the corrected independent
resize opacity and held carousel seed behavior.

The disposable native session then exposed an Intel GLES linker defect: shared
integer uniforms inherited different vertex/fragment precision. Both shader
stages now explicitly use high precision integers. Before the fix, all four
production scene bundle tests failed on Intel with the native error; afterward,
the complete UmbrielFX suite passes **63/63 on Intel and 63/63 on NVIDIA**, and
renderer ownership passes on both. `just gpu-test` now runs the entire suite on
every exposed GPU, closing the previous device-coverage gap. Evidence is in
`/tmp/umbriel-native-intel-before.log` and
`/tmp/umbriel-native-all-gpu-fixed.log`.

Verification of the corrected native-session tree passes **129/129 Meson tests**
and **292/292 headless checks forced onto Intel**. The first Intel headless run
identified a locality fixture counting a queued setup frame as source-triggered
damage. A read-only trace proved that frame committed before source selection.
The fixture now requires bounded pre-selection quiescence and observes several
subsequent frame periods while preserving the exact unchanged-neighbour count.
It passes **32/32 Intel stress runs**; deliberately updating the neighbour's
buffer after selection still fails. The complete rerun is recorded in
`/tmp/umbriel-effects-native/headless-intel-final.log`; the locality trace,
stress and negative control are under `/tmp/umbriel-locality-{trace,after,negative}.log`.

The user explicitly accepted both physical tiled and floating overlap handoffs
in the disposable session, then accepted workspace melt panel motion and
carousel in both `viewport` and `fit_all` framing. The four-workspace fixture
contains 1/4/2/0 windows and a wide scrolling layout. Every face remained active
without fallback; each framing mode reserved 99,537,088 bytes and released all
presentation memory at the accepted landing. User verdicts are preserved in
`/tmp/umbriel-effects-native/physical-review.json`, with workspace captures under
`/tmp/umbriel-effects-session/workspace-review-1790714747`. A fixture cleanup
command attempted cancellation after acceptance had already completed; the
fixture was corrected, clients exited and the original configuration restored.
This was a cleanup error, not a compositor failure. The user also explicitly
accepted physical touchpad swipe/reversal and restored pointer/keyboard input.
The read-only `/tmp/umbriel-effects-native/physical-gesture.jsonl` recording
includes interactive progress advancing from 0.17 to 1.0 and reversing to 0.0;
no input was injected for this check. The user additionally accepted physical
125% scale opening, melt and carousel rendering with native-quality landing.
Both opening samples and the held carousel had no fallback; presentation memory
returned to zero at landing. The original 100% scale was restored. Evidence is
in `/tmp/umbriel-effects-native/scaled-review`.

Physical playback acquisition passes fixed/default selection and 200/200
stimulus edges for each selector. Software rise p95 is 63.91/53.92 ms and fall
p95 is 95.73/96.31 ms, measured from producer PCM write to feature receipt;
these are not acoustic or visible latency measurements. Three 30-second silent
runs consume 2.79–2.97% of one core; unavailable-source retries consume
0.140–0.158%. Four silent virtual sources consume 11.14–11.57% total and about
40 MiB summed sampled RSS maxima. Default following, cross-class default
isolation and partial-link loss/recovery pass. Fixed serial/name lifecycle,
stereo/mono replacement and unsupported-source rejection also have evidence.
Exact defaults were restored and all fixture nodes/helpers removed. The user
stopped audible tests; the interrupted steady-tone CPU case is invalid and
audible tests will not restart without a new instruction. Virtual routing does
not establish physical hotplug or hardware rate/format negotiation. See
[the hardware evidence record](effects-hardware-validation.md).

The first native cost attempt was invalidated: generated `cost-window` and
`cost-overlay` window presets contained unsupported `animated` keys, producing
configuration banners. The earlier 16-cell rehearsal checked rendering/report
integrity but missed semantic config validation; it does not certify valid
benchmark configurations. The original native attempt and its `INVALID.json`
are preserved under `/tmp/umbriel-cost-native-final`; the strengthened report
correctly accepts zero cells from that attempt.

The generator is corrected and the runner now validates every configuration
with its exact measured binary before starting a compositor. Reports require
clean validation evidence and matching configuration hashes. All 31 benchmark
regression tests pass, as do 40 advancing and 8 frozen-video exact-binary config
checks and all four live phases on both binaries. Evidence is under
`/tmp/umbriel-config-preflight-fixed-final`,
`/tmp/umbriel-config-preflight-fixed-frozen` and
`/tmp/umbriel-config-live-smoke`. The corrected silent runner
`/tmp/umbriel-effects-session/run-native-acceptance` uses fresh
`/tmp/umbriel-cost-native-corrected` and `...-corrected-frozen` directories.
It requires the user's free active TTY throughout the approximately 75-minute
run; Super+Shift+Escape aborts. Both matrices have now run: 96 advancing and
24 frozen-video cells. Independent review and strengthened output-boundary
validation accept 119 original samples. The baseline run-1 declared-unused video
sample lost its active seat and remains excluded. The targeted native recheck
passes 8/8; its matching baseline declared-unused sample supplies the missing
third repetition, covering all 120 planned single-display cells. Exact artifact
hashes, video configurations and shader files match. The seven other recheck
samples remain confirmation rather than changing repetition weights. Original
matrix files retain their 95/96 and 24/24 validity; explicit combined provenance
is in `/tmp/umbriel-effects-session/native-cost-review.json`. All
candidate cells are valid, all 24 idle samples contain no render work, and all
45 candidate non-active cells have zero observed helpers and scene reservations.
Active scene compositor CPU has a median 29.94% of one core, versus 16.97% for
corresponding native actions; these are different visual workloads. GPU query
union includes submit waits and cannot establish hardware utilization. Detailed
measurements and limitations are in the hardware evidence record above.
The [native cost review](effects-native-cost-review.md) contains all 20
workload/phase comparisons and the explicit supplemental sample provenance.

The user confirmed that no microphone or second physical display is available.
Real microphone and physical two-output acceptance remain unverified; synthetic
audio and headless multi-output tests do not replace them. The missing baseline
repetition is now resolved. GPU execution/deadline measurement limits remain
explicit; steady-tone CPU was cancelled and acoustic/visible latency was not
measured. These additional measurement limits do not create a new requirement
for physical device hotplug beyond the plan's tested session-manager behavior.
No release sign-off or
completion of the canonical acceptance criteria is claimed.

## Work ownership

The integration lead owns configuration, build integration and acceptance.
Rendering owns UmbrielFX source capture, meshes, resource admission and GPU
fixtures. Lifecycle owns lease arbitration, input fixtures and native lifecycle
handoffs. Audio owns numeric analysis, protocol, helper supervision, shared
demand and frame bindings. Each stream's evidence is reviewed by another stream.

## Contract experiments

- `scene-effects`: atomic include-relative stage loading with repair watches,
  required/forbidden stage combinations, scope/trigger compatibility, aggregate
  source cap, bounded parameters and per-stage uniform budget arithmetic. These
  now feed the integrated `scene-v1` parser; public ABI sign-off remains pending
  completion of the runtime acceptance matrix.
- The registry now owns an internal scene-program cache with atomic stage and
  parameter versions, cached failures, pure lookups and retained immutable
  bundles. Its ownership/reload tests and focused lint pass.
- `scene-resources`: complete face inventory accounting, paired capture roles,
  scratch/depth/retained storage, native landing allocation and atomic output/
  aggregate reservations. Includes complete 64-face success within budget and
  4K FP16 rejection, bounded grid meshes and equal-aspect fit-all extents.
- `scene-projection`: GPU depth, forced opaque face alpha, perspective-correct
  item UV and output coordinates, face counts 1/2/3/4/5/8/64, and subsequent
  ordinary drawing. The depth-disabled control must fail the occlusion assertion.
  This tests a renderer experiment, not a live workspace carousel.
- The authored `scene-program` backend passes an isolated GPU run covering
  pair inputs, all face counts, perspective UVs, depth, grids, eight non-square
  output transforms, final composition, all 32 parameters with palette/audio,
  failed-stage rejection and subsequent ordinary drawing. Its Meson integration
  run passes with scene-source, scene-participant, scene-resources and
  scene-projection; live source and lifecycle gates still apply.
- `scene-pair` passes editable internal wipe/melt GLSL with distinct patterned
  sources, exact audio-independent endpoints, both axes/directions, intact
  revealed destination pixels, displaced outgoing melt pixels, analytic
  reversal, intermediate audio response and a source-alias negative control.
  These shaders are test fixtures, not installed workspace transitions.
- `scene-windows` passes editable internal water/portal stages, with current
  native boxes independent of longer neighbour motion. Both opening and closing
  endpoints match the corresponding ordinary translucent scene at zero/full
  audio; intermediate output-wide shading extends beyond all windows. Held
  inputs retrace progress, audio changes the intermediate result, and wrong
  target-token/quad-only controls detect missing target formation/grid motion.
  These are authored-stage contracts, not participant extraction or lifecycle
  sign-off; shadow/light silhouettes and live source readiness remain separate.
- `scene-carousel` passes editable vertex/fragment stages for all faces at
  N=1/2/3/4/5/8/64, including a finite two-face arrangement. Every selected face
  fills the native canvas exactly at landing at either audio extreme. Fractional
  navigation shows neighbouring faces, retraces under reversal and responds to
  audio while held. This is shader evidence; live workspace provider, input and
  framing acceptance remain separate.
- `scene-presentation`: stable identities, retained visual handles, complete
  sources, independent lifecycle deadlines, overlap arbitration, modal phases,
  reversal/retarget and pointer/touch sequence pairing. The model does not yet
  replace native scene presentation.
- `audio-protocol`, `audio-profile`, `audio-state`, `audio-transport` and
  `audio-supervisor`: little-endian encoding independent of struct layout,
  deterministic DFT golden vectors, anti-phase channel power, quantization,
  bounded overflow, freshness/order checks, immutable frame latch, retries,
  seqpacket backpressure, shared demand and pidfd-supervised child shutdown.
  Numeric reference generation is in `tests/audio/golden.py`.
- `effects-audio-inputs`: existing time/palette inputs plus two packed audio
  entries fit the unchanged eight-entry legacy table and thirteen slots. GPU
  pixels verify all sixteen bands, interpolation/clamping, no-time response and
  a held-zero negative control.
- `scene-source`: identity composition across all eight output transforms and
  scales 1 and 1.25, with regular blur, shadows, border lighting and paired
  filtered/unfiltered images. The 54-test UmbrielFX suite passed after the
  regular-blur fix. Optimized-blur identity now passes too, with initialized
  native cache readiness and independent capture scratch. This is not complete
  G1 sign-off: working-space/HDR and active source history ownership remain open.
- New focused source tests pass FP16 linear/extended values with one final
  encoding pass, paired deep-copied histories, interleaved native history
  promotion, and rollback after failed final submission. Frozen acquisition
  replays the completed feedback result; FP16 history preserves values 2.0 and
  4.0. Native light-cache isolation, format/alias decisions and retained capture
  role routing after source destruction pass. Feedback-bearing border emission
  remains explicitly unsupported pending independent source-history evidence.
- `scene-view` now passes hidden-owner visibility and translation without native
  node mutation, off-viewport enumeration with nominated clip bypass, preserved
  content clips, cold border emission, and unchanged native damage/callbacks.
  An owned paired-image helper reserves atomically and preserves capture roles.
  Managed FP16 replacement passes actual native output composition with one
  final encoding pass after source destruction. Mixed framing now preserves
  shared patterned backgrounds and pinned panels in face coordinates while
  fitting workspace content, including blur across the band boundary. The
  descriptor exposes the exact capture framing transform (stage budgets 41/50).
  Resource admission sums all retained images and one sequential capture peak;
  64 reduced faces fit the test budget without pretending all scratch images
  must coexist. Live compositor framing/landing remains a separate gate.
- `light-cache` passes exact RGBA8 and FP16 emission/pyramid cloning, including
  values above one, independent native mutation/destruction, exact reservation,
  ordinary rendering afterwards and output/renderer invalidation. Only a
  matching actual output buffer commit makes a candidate emission ready;
  mismatched or abandoned submissions do not. Native display versus unfiltered
  emission ownership and missing-role bootstrap still need integration proof.
- `scene-participant` passes rigid motion of two translucent windows with
  separately ordered analytic shadows and lit-border halos. Expanded capture
  bounds preserve off-viewport halo pixels which later move into view. This
  does not certify deformed grid silhouettes or native lifecycle integration.
- `effect/participant_admission` checks selected upstream presets with a real
  translucent client and shadows enabled. Plain and bundled `pulse` border
  presentations are accepted; bundled `scanlines`, inversion and a border
  overlay are rejected as `in_place_effect`; blur is `backdrop_blur`. Returning
  to `pulse` revalidates successfully. Repeated inspection leaves selection
  state unchanged. Focused lint and 16 concurrent matrix runs pass. This matrix
  establishes the internal compatibility policy, not live window-scene endpoint
  or grid-deformation acceptance.
- Audio configuration load/schema tests cover all legacy preset kinds,
  include-relative external executables, literal and empty argv entries,
  explicit typed source selection, invalid declarations and reference repair.
  The optional helper has an isolated absent-server protocol fixture; real
  acquisition remains gated on the integrated cadence tests.
- `effect/audio_inputs` passes actual pixels for screen/window/border/cursor
  shaders without time references, shared demand, explicit frozen input, a
  held-zero negative control, unchanged silence and last-consumer shutdown.
  `effect/audio_cadence` passes the 8 fps cap, frozen capture snapshots while
  provider sequence numbers advance, resume and second-output idleness.
  Six focused audio unit binaries pass, including graceful helper retirement on
  reload and independent output latches with failed-submit retries. Actual
  capture-only and straddling-output harness acceptance remain open A2 work.

## Integration failures caught

The full harness exposed a native fullscreen bug: a client requesting fullscreen
before its deferred tiled opening was admitted could remain invisible forever.
The ownership handoff in `View::setFullscreen` now resumes that deferred opening.
The existing fullscreen-opacity fixture requests fullscreen in the map flush,
reproducing the original failure deterministically without relaxing pixel checks.
It passed 32 concurrent stress copies after the fix.

Audio review found that relative watchdog timers rearmed on every packet could
postpone READY and stale-input deadlines indefinitely. Absolute deadline
scheduling and heartbeat-only/pre-READY-chatter helper fixtures address this.
Pending frame latches have an explicit cancellation path for lifecycle teardown.

The next full headless checkpoint exposed a synchronization race in the existing
`rule/is_alone` fixture: the unmaximized configure can precede the deferred
layout-size configure. The test now waits for the normal settle barrier before
comparing widths. Its original geometry/state assertions pass 32 concurrent
stress copies after that correction.

Scene-source identity testing exposed a native blur culling problem: pixels
hidden by an opaque panel could still be needed by a blur kernel. Render-local
sampling coverage now retains those dependencies. The renderer stream is
checking its allocation and traversal cost as well as optimized-blur parity.
The next GPU run exposed an uninitialized optimized-blur reference cache and
participant/light composition mismatches. Shadow alpha required separate blend
factors on transparent targets; clipped halos required expanded capture bounds.
Focused GPU reruns pass with the original pixel tolerance. Source history,
working format and native lifecycle evidence still block public scene interfaces.

## Integrated checkpoint

The third complete unit/renderer checkpoint passes **124/124**. Its frozen
compositor/client build initially passed **249/251** headless checks. The two
failures were the inventory fixture's attempt to close a hidden window through a
current-workspace action, and a lit audio border assertion comparing
screen-blended output to the unlit color. Correcting the stimulus and comparing
lighting to identical literal-input shaders produces **251/251** in the complete
rerun. The stronger lit straddling fixture passes 16 stress copies; inventory,
workspace cancellation and restoration-commit barrier pass 16 each (48/48).
Full lint found two style issues; both corrections pass focused lint. Renderer
ownership/recovery passes on both `/dev/dri/renderD128` and `renderD129`, and
packaging workflows pass 23/23. Subsequent editable carousel and strengthened
window-stage fixtures pass 2/2. These remain integration checkpoints, not release
sign-off.

The complete unit/renderer run with the optional helper enabled passed 117 of
119 tests. Its two failures were the participant issue above and an assertion
accidentally duplicated into the wrong config fixture. Both fixes are present;
the participant focused rerun passes, as do config-load, scene-effects and the
packed audio GPU test in the focused authoring rerun.
Four full-lint style findings were corrected and also need their next rerun.

A saved compositor/client build passed all 237 headless checks, followed by
16 concurrent copies each of audio inputs, audio cadence and session environment
(48/48). The latter covers shared process-supervision extraction. The optional
PipeWire absent-server and private-server reconnect tests passed in Meson.
All 23 packaging workflow tests passed. These are checkpoint results, not
verification of the next audio/capture and displaced-scene changes in progress.

The second frozen-build full run passed 240/243 checks. Two new audio fixture
failures were traced to a pending setup reload and initial placement on the
wrong output; their corrected focused reruns passed. A later coherent build
exposed cross-output damage during audio uniform rebinding: the straddling
fixture observed 85 commits in roughly 1.4 seconds against a limit of 15.
A composition-local uniform setter now passes that fixture, including zero
healthy-output commits during asymmetric failed submissions. The third full-run failure was the
`is_alone` synchronization issue above. This checkpoint does not include the
new restore-commit input barrier, actual failed-submit audio probe or the latest
source-history/working-format implementation.

The next GPU checkpoint initially passed 7/9. Its two failures exposed a
participant fixture lacking a native border-emission reference and a rectangular
headless fixture setting a mode before enabling its output. With those corrected,
the focused source/participant/program, four FP16 and capture-feedback checks
pass 8/8 with the original pixel tolerance. The separate editable pair test
also passed the earlier checkpoint.

The test-only `presentation-input-probe` verifies real client pointer delivery:
the first sequence is swallowed, the next arrives exactly once, and hover focus
is restored after dismissal. A disabled-guard control verifies that the assertion
depends on the guard. It passed 32 concurrent stress copies. The newer
`input/presentation_scene` fixture also passes with real displaced pixels,
recorded dismissal stages, unchanged client output membership and native clock
identity, and exactly paired pointer delivery. Touch now exercises real client
`wl_touch` delivery, multiple contacts, cancellation and device removal.
`animation/presentation_overlap` and its floating counterpart pass retained
close identity/deadline and burst fallback assertions with two neighbours.
These four displaced fixtures passed 16 stress copies each (64/64), before the
new ordinary-commit restoration barrier. The lead reviewed preserved tiled,
floating and touch PNGs under `/tmp/umbriel-effects-g4-evidence`: the one-time
jump is acceptable for the bounded probe; final authored effects still need
visual review. See [lifecycle evidence](effects-lifecycle-c0.md).
The latest frozen build passes all eight pointer/touch/overlap/immediate-close
and capture-role fixtures, including suppression until an ordinary restoration
buffer successfully commits. Workspace pre-mutation cancellation is the next
focused validation. The subsequent workspace mutation and inventory tests pass
48 stress instances as recorded above. A new `workspace/presentation_sources`
probe passes never-visited hidden layout preparation, live blue-to-green client
updates, failed-submit callback suppression and exactly one successful callback,
empty/source selection, native restoration and zero retained resources. This is
a selected-face provider probe; projected carousel and full source pacing still
require integration.

## Outstanding release gates

The next integration slice passes frozen role-light pixel comparisons over two
feedback frames, including the alias and split-role cases. Deformed shadow and
light companions pass independent geometry-mask pixel oracles; native shadow
convergence reaches exact native pixels. Fixed scene-light storage now handles
RGBA8-to-FP16 sampling without GLES copy-format errors and rejects unadmitted
recipe changes without allocating during a draw. These are renderer checks,
not evidence that the window lifecycle runtime is finished.

The reusable workspace source owner passes the five single-output source,
inventory, callback, audio, and live map/unmap/migration fixtures. Hidden-source
audio passed 16 stress instances. Scene descriptor audio and replacement of all
four demanded sources at the cap pass focused unit tests. The new composition
owner passes three ownership/arbitration tests: paired role publication, exact
failed-submit retry, and atomic allocation/budget failure. Public config routing
and workspace presentation orchestration are being integrated; the last full
suite checkpoint above predates these changes.

G1 requires exact desktop source composition, including blur, shadows/light,
output transforms and filtered/unfiltered histories without disturbing native
state. G2 requires movable participants with those companion strata, explicit
legacy-preset admission and native-current endpoint evidence. G3 additionally
requires live workspace sources, both framing modes, native-quality landing and
deduplicated surface pacing. G4's bounded displaced-scene/touch, recorded overlap
and restoration-commit fixtures pass; workspace/output mutation and production
lease integration remain. G5 requires the integrated output/capture cadence matrix.

Public immutable scene bundles, workspace melt/carousel, window-scene open/close,
their installed presets and scene/audio cross-feature tests are not complete.
Real playback and microphone routing, removal/reconnection and acoustic latency
need a disposable session; the user has offered to enter one when a test build
is ready. Physical gesture, two-output scanout, scaled/HDR and performance reports
remain separate from headless/GPU test results. No gate is satisfied by replacing
its required fixture with an easier one.

### Configured scene runtime integration (2026-09-29)

The configured workspace-set path now owns a complete native inventory,
role-paired live sources, authored geometry, full-resolution landing, output-local
input dismissal and atomic authoritative selection. The `carousel-2` snapshot
passed the basic enter/select/hold/cancel/accept test and the 1/2/3/4/5/8/64
inventory matrix in both framing modes. Later fixtures on `scene-inputs` passed
actual 1/4/2/0 populations, long scrolling content, and the bounded unacknowledged
client preparation fallback. These tests use production bindings/actions, not
only the earlier source probes.

Production scene audio and reflected TIME tests passed on
`/tmp/umbriel-scene-inputs/umbriel`: exact synthetic levels, no-TIME audio damage,
held idle, failed-submit latch preservation, 8 fps TIME cadence, frozen zero-wake
behavior, resume and lease release. The hidden-source TIME stress passed 16/16.

The first configured workspace-pair snapshot proved exact starting pixels,
frozen outgoing pixels despite later client updates, a live destination, authored
intermediate displacement and native destination landing. Its retarget case
exposed an input restoration barrier race; the source now waits for a successful
native restoration commit before acquiring the replacement lease. That fix still
requires validation in the next integrated snapshot. The separate production
capture test passed on `pair-2`: capture begins after outgoing client destruction,
and both retained display and unfiltered roles remain correct when policy changes.

The renderer now distinguishes FP16 storage from linear values. An independent
unmanaged XBGR2101010 ramp preserves sixteen adjacent ten-bit values through
source acquisition, encoded FP16 composition and native landing. The native
allocator does not support XRGB2101010 on this test device; the fixture exercises
supported native ten-bit formats and requires at least one. Managed-linear tests
continue to pass. Focused source/view/program/blend: 4/4.

Public editable melt/wipe/iris/carousel/water/portal bundles are installed from
`examples/effects/scene`; GPU example tests now consume those canonical files
and pass 3/3. Window-scene production integration and gesture routing are still
being integrated. No fresh complete suite or physical acceptance has been
claimed for these newer changes. Earlier complete-suite results remain a prior
checkpoint, not release evidence for the current tree.

The subsequent `window-1` integrated snapshot passed both-axis pair/retarget and
post-freeze capture tests (3/3), plus carousel wheel/finger navigation, reversible
pair gesture pixels/identity, queued release, device loss and existing input /
overview regressions (4/4). The first complete headless run passed 270/271; the
existing `effect/border_frames` test exposed frozen-clock native TIME damage that
is being corrected without changing its pixel oracle.

The complete Meson suite passed 127/129. Its failures identified unsorted new
action names and missing rows in the action reference; both source/docs fixes
are implemented, with verification pending the coherent renderer scratch batch.
The packaging workflow suite passed 23/23. The independent complete UmbrielFX
suite passed 63/63 before that newer memory-accounting batch.

`window-2` passed configured water opening/closing in tiled and floating layouts,
ordinary lit pulse borders, and portal (4/4). Review images are retained under
`/tmp/umbriel-window-evidence-2`; the project lead inspected lit opening/closing
and portal intermediate frames. Native geometry descriptors and separate
capture canvases now come from native motion owners. Configured carousel recovery
passed binding removal, failed shader edits with retained bundle, renderer loss,
transform changes, overview interruption and session lock in one fixture.

Resource acceptance remains open: the initial source planner charged twelve
full-size scratch images even for plain scenes, and composition duplicated
sequential companion scratch across role/version targets. Those conservative
allocations reject important higher-resolution scenarios. Feature-sensitive
source accounting and shared preflighted companion scratch are being implemented
and must pass ownership, pixel and admission checks before release. No budget
increase or unmeasured performance claim is being used to close this gate.

The action ordering/documentation corrections now pass both targeted tests.
Native TIME fixes pass the unchanged border-frame oracle and a two-output
failed-submit fixture: a held output catches up to the same frozen instant while
the healthy output remains idle. Mirrored presentation isolation and teardown,
shared companion scratch, and cross-framing border light pass focused source /
view / shadow GPU checks across all eight transforms.

`window-4` passes six composition tests, including separate retained final role
pairs with one shared serial composite intermediate. Its new configured
`effect/scene_feedback` test proves separate live face histories, hidden-owner
initialization, successful-submit-only advancement across a rejected frame,
native-history isolation after cancellation, and exact native feedback replay
for a frozen melt after the original client is destroyed. It passes 16/16 stress
runs; disabling the scene binding makes the negative control fail. The feedback
fixture plus both-axis melt and post-freeze capture regressions pass 4/4.

Optimized PR2 baseline builds now exist with LTO-aware archiving, both ordinary
release and a matching real Tracy client. The candidate production build with
test IPC disabled and the optional audio helper enabled also passes. This is
build evidence only: no physical performance/latency or manual visual acceptance
has yet been claimed. Final full-suite verification remains pending the ongoing
1080p resource and recovery fixture checks and final renderer changes.

The first complete verification of that integration passed formatting, full lint,
129/129 Meson tests, both DRM renderer-device checks, and 23/23 workflow tests.
Headless results were 289/290, including 1080p lit window presentation, renderer
recovery, cold virtual feedback border emission, isolated toplevel capture and
single screen-effect application. Logs are in
`/tmp/umbriel-effects-final-verification`; this remains a checkpoint, not release
sign-off. The separate feedback/capture/fullscreen/budget stress matrix passed
64/64, and the configured 4K/64-face resource rejection preserved all native
workspace identities and released provisional resources.

The one headless failure exposed the native hidden-workspace background timer
acknowledging callbacks independently of an active presentation's failed output
submission. It now skips presentation source owners; the strengthened fixture
crosses two real background timer intervals while submission is held and fails
the old build. Review also found lifecycle-bypass source opacity dropping an
independent resize crossfade, and workspace-set exit retargets changing the
documented held shader seed. Both fixes are implemented; their new regressions
and subsequent complete verification are pending. Negative seed pixels already
fail the old build on exit retarget.

The optional Tracy path required completed timestamp-query calibration on the
NVIDIA device: its integer timestamp getter returned no value without reporting
an error. The same profiling-only correction is applied to baseline/candidate.
A calibrated smoke trace has a coherent approximately four-second CPU/GPU
timeline, actual output commit/presentation messages and bounded exports. This
establishes the measurement pipeline, not native performance acceptance.
