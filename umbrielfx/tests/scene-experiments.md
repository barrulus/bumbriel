# Internal scene experiments

These are C0 experiments for `docs/design/effects-implementation-plan.md`, not a
released scene API. They do not satisfy all G1–G3 acceptance criteria or enable
scene presets in user configuration.

The tests use the real UmbrielFX GLES context and a headless Wayland output.
Lack of a usable DRM renderer returns Meson's skip status 77; a successful test
is not software-rendering evidence. The initial measured context was NVIDIA
GeForce RTX 5060 Laptop GPU, with texture limit 32768, 1024 vertex uniform
vectors, 1024 fragment uniform vectors, and 31 varying vectors.

## Source capture

`scene-source` compares an ordinary composition with an independently captured
inclusive root-sibling range. Its desktop fixture contains transparency, a
clipped real scene buffer, analytic shadow, border light, regular and optimized
blur, a top panel, fullscreen-like opaque content, and a pinned window. An opaque
excluded overlay obscures the native source while it is captured. Both display
and persistent-effect-excluded roles are tested under all eight output
transforms and scales 1 and 1.25, with a nonzero output origin. Captures do not
repeat the buffer's output-sample callback, change native visibility or pending
damage, or mutate the optimized-blur dirty flag. Retained buffers remain
readable after their source scene is destroyed.

Source captures use independent render lists, coverage regions and renderer
scratch targets. Optimized blur is rebuilt in the source-local scratch set. A
clean optimized node lacking its native cache is explicitly rejected: native
rendering currently uses live-blur fallback in that state, which cannot be
silently replaced with an optimized source. Direct stateless captures suppress native history promotion. The bounded
source-session experiment snapshots both native role histories by deep copy at
acquisition, accounts for those copies and future writable history images, and
owns their subsequent promotion. A repeated capture in one source frame reuses
its retained target. Source promotions are staged until the caller confirms a
successful final submission. The GPU fixture interleaves native promotions
between source roles, checks role independence, rejects an insufficient
reservation, retries a failed final submission without advancing history, and
invalidates the source on node destruction. Frozen sources replay the completed role-specific feedback result directly,
without evaluating another feedback step. Separate fixtures check a retained
FP16 value of 2.0 and independently advancing source history of 4.0, and confirm
source emission does not mutate the native border-light cache.

A second identity matrix uses non-square 32×16 output targets under all eight
transforms and scales 1 and 1.25. The paired capture helper selects FP16 storage
for a working-space output. Native paired capture aliases role zero only when
native capture itself aliases the display (no excluded in-place or output stage).
Border-only feedback therefore does not demand a missing or stale clean-role
history. Split native capture retains two independent histories and emission
caches. Frozen feedback-light fixtures advance two native frames, reject
uncommitted emissions, deep-copy committed role caches, and compare all source
pixels against their corresponding native roles. A native-scene replacement fixture tags retained buffers EXT_LINEAR with sRGB
primaries and verifies both display and unfiltered landing encode exactly once,
even after the source client is destroyed.

The FP16 fixture verifies sRGB node decoding into linear working values, retains
an authored channel value of 2.0, and encodes only at the final landing. Four
existing FP16 blending/blur/save-restore regressions and native capture feedback
also pass. This is a working-space primitive proof, not a complete HDR desktop
capture/landing matrix.

The compositor's proposed contiguous desktop range is `Server::m_backdrop`
through `pinnedTree`, inclusive. Overview and drag ownership must be excluded at
admission. Overlays, compositor UI, lock surfaces, and cursors are outside this
range. `scene-view` now verifies source-only visibility/translation overrides for an
inactive workspace, nominated viewport clip bypass, off-viewport content,
preserved nested content clipping and cold border emission without a native
proxy/cache. Mixed framing draws native strata into one target/pass: shared
patterned background and pinned panel remain in face coordinates, while the
workspace uses a fit transform. An ordinary native composition is the pixel
oracle, including blur across the shared-background/workspace boundary,
in-place inversion and both roles. Sources leave native visibility, positions,
callbacks and pending output damage unchanged. The complete mixed-framing
matrix passes all eight output transforms. A separate 40×30 matrix verifies
all transforms, scales 1 and 1.25, viewport and half-resolution fit framing,
nested content clips, hidden translations and owned role-buffer dimensions.
A conservative visual-bounds query includes selected transient buffers, native
shadows, declared effect expansion and cold light halos. Cross-framing light
owners and evaluated feedback-bearing virtual-view stages still reject.

Owned view pairs publish atomically. Their plan separates retained role images
from a transient capture peak reused sequentially across roles/faces. The GPU
fixture checks that 64 retained reduced faces plus one peak fit 256 MiB while
summing 64 redundant capture peaks would fail; this is resource accounting,
not a live 64-workspace presentation. Native-resolution landing and old images
held during atomic refresh must be reserved by the inventory owner separately.
The exact framing helper supplies the shader item transform without a second
rounding calculation. Live workspace occurrence/frame/audio pacing still needs
compositor integration evidence. Full HDR desktop capture,
failure injection and native-quality presentation landing still require evidence.

## Participant capture

`scene-participant` treats two translucent content nodes and their analytic
shadows as independent ordered sources. Native translated node positions are
the pixel oracle. A retained content buffer survives immediate source node
destruction. A selected legacy in-place effect and backdrop blur are rejected
with distinct reasons; a node becomes admissible after its in-place selection
is removed. This is a narrow rigid-translation experiment, not an upstream
preset admission matrix or a proof for arbitrary grid deformation.

The experiment exposed an analytic-shadow alpha bug: source-alpha blending
also squared alpha on transparent targets. Using a separate alpha blend
equation preserves the native shadow when its transparent image is replayed.
The lit-border test also exposed loss of off-viewport halo pixels when an
output-sized retained source is translated back into view. Expanded companion
capture extents fixed the error without increasing pixel tolerance. The fixture
now compares both the plain and lit-border compositions successfully, using a
32×32 capture extent around its 16×16 output. The emission-only companion
fixture first presents its native owner to seed the read-only emission cache.
A missing cache rejects capture; this does not establish cold opening or hidden
owner lighting support.

## Projection and resource bounds

`scene-projection` currently exercises a raw GL experimental program rather
than a production scene-program bundle. It draws 1, 2, 3, 4, 5, 8 and 64 distinct
face textures, forces opaque output alpha, checks depth independently of draw
order, and includes a disabled-depth negative control. Varying vertex W changes
perspective-correct item UV without changing raster-derived output UV. An
ordinary UmbrielFX pass after teardown checks for leaked state. It does not yet
prove live workspace capture, face identity through selection, fit-all source
enumeration, or exact landing.

`scene-program` uses the new internal renderer compiler/draw backend rather
than direct test-only GL. It validates immutable pair/set/window bundles, bounded
quad/grid draws, optional final composition, both source samplers, typed
parameters/palette/audio inputs, and reflection. Its analytic varying-W pixel
oracle checks perspective-correct UV; all eight transforms use a non-square
128×64 target. A deformed 8×8 mesh differs from a quad negative control; independent
window motion and trigger clocks are tested. Set drawing uses depth and opaque
alpha, while window drawing preserves premultiplied order and screen-blended
emission. Queried limits are checked conservatively with 43 vertex and 54
fragment vectors reserved before user parameters; no public ABI is frozen.

`scene-resources` checks complete-inventory planning, overflow, native landing
storage, per-output and aggregate atomic reservations, full/half/quarter source
resolution selection, regular mesh limits, and equal-aspect framing. The
64-face 4K FP16 image lower bound alone exceeds the 256 MiB output budget and
is rejected without truncating the inventory.

## Ordinary-path cost and correctness

The regular-blur identity fixture exposed ordinary render-list culling that
discarded lower pixels still needed by a blur kernel. The correction propagates
bounded sampling footprints in the existing top-down render-list walk. It does
not add a second source walk. Coverage overrides are created only where needed;
clearing a list with no overrides is constant-time. The render-list entry grows
from 24 to 48 bytes on 64-bit builds, and blur scenes incur region operations and
may retain additional lower entries. These are structural costs, not measured
GPU/frame-time claims. Idle/off must not be described as having zero additional
storage cost merely because no scene transaction is active.

The private G4 replacement probe adds one nullable output pointer and a
constant-time null check per ordinary render-list candidate. Active replacement
walks each candidate's ancestry and the bounded desktop root range. It allocates
its state only on explicit probe acquisition and does not disable native nodes.

The paired-source convenience helper is test-only. It rejects effect-group
extents larger than the output and derives a conservative scratch bound from
the enabled scene's effect depth. Managed and ten-bit output acquisitions select
FP16 storage. Managed working-space sources are tested; unmanaged ten-bit
working-space selection still needs a dedicated pixel oracle. Native-role alias
follows native capture policy; virtual views conservatively alias only when no
selected stages are installed.
Copied native feedback histories are included before allocation. Complete
expanded-group/light allocation accounting and real allocation-failure tests
remain prerequisites for production admission. Paired working-format, alias,
frozen role-replacement and committed feedback-light cases now pass the focused
GPU checkpoints.

## Derived companions and native landing

`scene-shadow` uses the internal scene-program backend with three preallocated
padded scratch images shared across participants. Authored grid and fragment
formation masks consume opaque owner geometry, independently of client alpha;
the native bounded shadow kernel consumes the result. An independent CPU
rasterizer supplies a full-pixel oracle for the bent/formed geometry, including
off-output casters. All eight output transforms and RGBA8/FP16 targets pass.
Unprepared storage, excess halo and changed light recipe/scale reject before
drawing. The companion input semantics remain private.

The mask kernel is not identical to the ordinary analytic shadow: the measured
64×48 endpoint L1 difference is 1590 channel units. A retained native analytic
companion and explicit generic `native_mix` weight address this difference.
Weighted premultiplied addition happens in isolation at the shadow stratum,
before optional authored final composition. The SDR convergence fixture's
errors decrease 8209, 2925, 323, 44, 0 as the geometry residual vanishes and
native weight advances 0, .5, .9, .99, 1. The final frame matches exactly.
Runtime transaction policy must still supply and test these weights.

The same fixture deforms retained raw border-emission input before applying
preallocated native threshold, blur pyramid and screen blending. An independent
CPU ring raster matches both threshold extremes and progress 0, .5 and 1 in
RGBA8 and FP16. Fixed recipe pyramids are shared sequentially and conservatively
reserved as FP16; no shader compilation or GPU storage allocation occurs in
these companion draws. Native feedback-bearing raw-emission acquisition and
cold participant admission are separate gates.

`scene-blend` exercises native-resolution landing using existing renderer
programs: bilinear resampling followed by a weighted additive premultiplied sum.
Independent CPU sampling checks all eight consistent source orientations,
weights 0/.25/.5/.99/1, alpha, RGBA8 and FP16 values above 1. Nonidentity sampling
matrices and working-format mismatches are explicitly rejected.

The target-only source override fixture captures a nested participant on a
transparent canvas while its native content, border and shadow have zero fade.
Absolute unfaded values preserve unrelated opacity factors; only the declared
lifecycle stage and its animation clip are bypassed. An unrelated movement
shader still runs. Full pixels match the native unfaded oracle and native
properties/damage remain unchanged. An evaluated feedback lifecycle still
rejects without virtual history ownership. View/Decoration must provide the
unfaded values directly rather than dividing by a possibly-zero native fade.

### Storage precision versus color encoding

Unmanaged ten-bit output retains gamma-valued FP16 source and composition
storage. The `floating_point` metadata controls allocation/budget; the separate
`working_space` flag controls linear-value transfer tagging. Explicit scene
creation preserves this distinction. `scene-source` checks sixteen adjacent
10-bit ramp steps against an independent analytic oracle through native output,
owned source roles, scene blend and landing. XBGR2101010 passes on NVIDIA;
XRGB2101010 allocation is unavailable on that device. The fixture requires at
least one working native ten-bit format and reports unsupported allocator formats.
Managed linear/extended-value, source-view, program and blend checks also pass.

Cold nonfeedback border emission now exports raw paired source-owned values
before threshold and deformation without creating native proxy/cache state.
Committed native role-specific feedback emission can be retained independently.
Cold feedback without a completed role history remains an explicit admission gap.
