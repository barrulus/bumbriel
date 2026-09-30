# Renderer acceptance audit

This records evidence and remaining obligations, rather than treating primitive
GPU tests as proof of complete compositor behavior. The complete UmbrielFX suite
passes 63/63 on each of Intel and NVIDIA after the shared integer-precision fix
(`/tmp/umbriel-native-all-gpu-fixed.log`). The integrated native-session tree also
passes 129/129 Meson tests and all 292 headless checks forced onto Intel
(`/tmp/umbriel-effects-native/headless-intel-final.log`).

## Proven renderer primitives

- Source identity: independent source lists, visibility, optimized/live blur and
  scratch; native damage, history, enabled flags and sample callbacks remain
  unchanged. Rectangular outputs cover all eight transforms and fractional scale.
- Sources: native-role frozen feedback histories, committed per-role raw border
  emission retention, cold nonfeedback emission, hidden workspace overrides,
  nested clips, mixed viewport/content framing in one target, and exact framing
  metadata. Live hidden occurrences own separate display/unfiltered histories,
  start from the native current-input bootstrap, snapshot stage parameters and
  promote only on successful presentation. Failed frames, interleaved native
  rendering and another face cannot advance those histories. Session matching
  checks program inventory, output/renderer, framing and reserved dimensions.
  Paired capture can alias only after a conservative equivalence proof.
- Mixed framing light: each source light retains its owner's physical framing
  through the native light stratum. The all-eight-transform source oracle now
  includes a content-framed owner drawn through a viewport-framed light layer.
- Output locality: the owner tag covers the complete dedicated presentation
  subtree. Coincident/mirrored outputs show owner-only pixels and callbacks;
  buffer changes, movement and teardown do not damage the peer. Native visibility
  and source captures remain independent of the presentation picture.
- Working values: managed linear FP16 retains values above one and encodes once;
  unmanaged ten-bit keeps encoded FP16 separately from linear metadata. An
  independent 10-bit ramp checks source/composition/native landing on the GPU's
  supported native XBGR2101010 format. XRGB2101010 allocation is unavailable on
  the tested NVIDIA allocator, not silently converted to eight bits.
- Generic geometry: bounded grids, homogeneous positions, perspective-correct UV,
  depth, 1/2/3/4/5/8/64 selectable faces and endpoint oracles. Public wipe, melt,
  iris, carousel, water and portal files are the GPU tests' canonical sources.
- Companions: opaque native-corner geometry independent of client alpha; authored
  grid shadow mask followed by native convolution; explicit convergence to the
  analytic native endpoint; raw authored emission followed by native threshold,
  pyramid and screen blend. RGBA8/FP16 and all transforms have pixel oracles.
- Atomic resource preflight, role/version outputs, retained failed-submit inputs,
  renderer invalidation and immutable compiled bundles have renderer/unit evidence.

## Integrated production evidence and remaining limits

Harness names below refer to `tests/harness/checks/<name>.sh`. GPU tests are
Meson test names. The integrated 292-test run includes these runtime fixtures;
the primitive experiments are supporting evidence rather than substitutes.

| Gate | Current evidence | Remaining limit |
| --- | --- | --- |
| G1: source/capture roles | `effect/scene_pair_capture` starts capture after freezing and destroying the outgoing client; `effect/scene_inputs` verifies isolated toplevel capture through configured carousel; `effect/scene_inputs_screen` checks one screen pass; `animation/window_scene_capture` checks production window role separation, isolated capture and one screen pass. `workspace/presentation_pair_fullscreen` preserves fullscreen endpoints. | Physical 125% scale opening, melt and carousel rendering was accepted, with no fallback and zero retained presentation bytes after landing. Physical HDR remains unverified. |
| G1: feedback/light | `effect/scene_feedback` checks fresh virtual histories, native-history isolation, failed-submit promotion and frozen native replay after client destruction. `effect/scene_feedback_light` compares cold virtual feedback border/halo pixels against literal references. | Missing committed native feedback/emission provenance remains an explicit readiness failure. Never-focused hidden windows retain the native no-border selection gate. |
| G2: window composition | `animation/window_scene`, `window_scene_floating`, `window_scene_lit`, `window_scene_portal`, `window_scene_transform` and `window_scene_capture` exercise the production window owner, native companions and capture roles. `animation/window_scene_resize_crossfade` preserves independent resize opacity and ordinary reflow. `animation/window_scene_feedback_fallback` verifies the unsupported feedback path. | Backdrop-dependent/in-place shaders, blur participants and feedback participants retain documented window-profile fallback. The admitted subset does not promise arbitrary GLSL factorisation. |
| G3: live inventory/framing | `workspace/presentation_carousel_inventory` admits and commits all 1/2/3/4/5/8/64 identities in both framing modes with native-resolution landing. `workspace/presentation_carousel_framing` covers 1/4/2/0-window populations and wide scrolling content. `workspace/presentation_sources`, `presentation_source_callbacks` and `presentation_source_reconcile` cover live refresh, callbacks, retry and topology reconciliation. `workspace/presentation_carousel_budget` verifies complete 4K RGBA8 rejection; GPU `scene-resources` covers the stricter 4K FP16 bound. | The available display runs at 1080p; physical HDR presentation has not been validated. Resource arithmetic and headless rejection do not establish HDR presentation. |
| G4: lifecycle/input | `animation/window_scene_overlap`, `window_scene_overlap_floating` and `window_scene_immediate_close` preserve native handoff/deadlines; `input/presentation_scene`, `presentation_touch` and `presentation_commit_barrier` check guarded dismissal and successful-restore input ownership. `workspace/pair_gesture` and `presentation_navigation` cover gesture decisions. The user accepted physical tiled/floating handoffs, melt/carousel panel motion in both framing modes, and actual touchpad reversal/full swipes. | Physical touch-device input is not inferred from touchpad or injected-touch evidence. |
| Recovery | `workspace/presentation_recovery` checks frozen configured carousel through compile failure, binding removal, renderer loss, transform change, overview and lock. `animation/window_scene_recovery` checks renderer replacement during a configured window scene. `effect/scene_inputs` and `effect/scene_feedback` retain rendered candidates/input across failed output submission; `input/presentation_commit_barrier` preserves the restoration barrier. | This describes the named scenarios, not every failure crossed with every owner. Physical output removal/reconnection and scanout restoration remain hardware evidence. |
| Locality | GPU `scene-source`/`scene-view` cover coincident-coordinate ownership. `workspace/presentation_source_locality` preserves peer pixels, exact frame counts and independent touch delivery; it passes 32 stress copies, and deliberately damaging the peer after selection fails its oracle. | Two-physical-output scanout isolation is unverified because only one display is available. |
| Cost | `animation/window_scene_1080p` admits full-resolution lit composition. Resource, composition and complete-inventory fixtures enforce atomic ceilings, release and native fallback. Physical four-face carousel in either framing mode reserved 99,537,088 bytes and released all presentation memory after acceptance. The native matrix covers 120 selected samples: 119 valid originals plus a matched supplemental baseline video sample, with invalid evidence retained. | CPU/helper/RSS and GPU query measurements are recorded in [the native cost review](effects-native-cost-review.md). Query elapsed time includes synchronization and is not hardware utilization; requested deadlines remain unmeasured. Physical two-output cost is unavailable. |

## Resource findings

The original source view estimate always charged twelve full-face scratch images,
even for a plain desktop, and retained two role images even when acquisition
proved equality. This rejected ordinary 4K sources and some 1080p FP16 transactions
before composition. The refinement charges no GPU scratch for shader/blur-free
views, two blur ping-pong images plus two optimized images when applicable, and
derives shader scratch from selected features: two depth-indexed output/group
pools, an in-place copy per used pool, blur ping-pong and backdrop per used pool,
the optimized pair, and two extra depth levels when an animated native shadow
may capture its owner. A border-only stage requires two scratch images rather
than fourteen. Serial capture roles reuse this storage. The positive/negative
scratch matrix exercises nested stages, in-place input, live blur and optimized
backdrop, including rejection when the reservation is one byte short.
`capture_bytes` includes CPU/list/import allowances; `scratch_bytes` reports the
transient GPU portion separately. Alias proof charges one retained image. `history_bytes` is a separate persistent
reservation, including session metadata and four local FP16 stage images (two
ping-pong images per role). Frozen native ranges instead reserve copied native
histories and committed emission, without unused writable histories; distant
foreign-output effects do not impose readiness on the local range. Cold emission
reports its retained raw roles separately from its one reused capture target and
transient scratch.

Exact source-only preflight bytes for an opaque rectangle, including the current
conservative CPU allowance (these are reservation figures, not RSS measurements):

| Canvas | Values/storage | Plain | Optimized blur |
| --- | --- | ---: | ---: |
| 1920×1080 | encoded RGBA8 | 10,600,832 | 43,778,576 |
| 1920×1080 | linear FP16 | 18,895,232 | 85,250,576 |
| 3840×2160 | encoded RGBA8 | 35,484,032 | 168,194,576 |
| 3840×2160 | linear FP16 | 68,661,632 | exceeds 256 MiB |

Final composition separately reserves four full-size role/version images, or
five with a final composite (one intermediate is reused serially). At 1080p
these image-only amounts are 31.64/39.55 MiB for RGBA8 and 63.28/79.10 MiB
for FP16; at 4K they are four times larger.
Retained old/new sources, depth and companions are additional. A composition can
therefore decline despite an individually admitted source.

Companion scratch can be shared across serial role/version draws while retaining
independent completed outputs. The C API retains that scratch by reference,
validates renderer/format/size/value-space compatibility and survives destruction
of the original target. The composition owner reserves and prepares it once.
Light preflight already includes the common three padded images and must not
charge those again as shadow storage. This removes fourfold scratch duplication;
it does not justify aliasing final output roles or changing their input policy.

## Cost and physical acceptance

Native Intel validation exposed a GLES linking defect that the default NVIDIA
fixture did not reject: the shared scene wrapper left integer precision at the
different vertex/fragment defaults. The wrapper now explicitly selects high
precision integers in both stages. Before the fix, `scene-program`, `scene-pair`,
`scene-carousel` and `scene-windows` all failed on Intel with the same uniform
precision error as the native session. After the fix, all 63 UmbrielFX cases pass
on both Intel `/dev/dri/renderD128` and NVIDIA `/dev/dri/renderD129`, along with
the renderer ownership check. `just gpu-test` now runs the complete UmbrielFX
suite on every exposed render node so driver coverage does not depend on the
fixture's default device. These are GPU correctness results, not a native visual
or performance signoff.

The [native cost review](effects-native-cost-review.md) records three valid
repetitions of the available same-baseline disabled/declared-unused/active/
returned-off comparisons, including advancing and frozen video. Compositor and
helper CPU, sampled RSS, CPU render duration, named GPU query elapsed durations
and physical presentation cadence are reported separately. Pure GPU utilization,
isolated authored-mesh execution and requested-deadline misses are not inferred.

The user separately accepted physical tiled/floating overlap
handoffs, melt/carousel panel motion in both framing modes, and actual libinput
swipe reversal/full swipes; verdicts and recording paths are preserved in
`/tmp/umbriel-effects-native/physical-review.json`. Physical 125% scale was also
accepted and 100% restored. Playback routing, software stimulus-to-feature
latency and silent helper CPU measurements are recorded in
[the hardware validation record](effects-hardware-validation.md). Physical HDR
and two-output scanout remain unverified; a microphone
and second physical display are unavailable. Do not turn automated endpoint or
fallback assertions into claims about those unverified hardware limits.
