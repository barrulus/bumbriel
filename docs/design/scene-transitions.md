# Scene transitions (proposal)

The canonical shareable document is the
[design specification and implementation plan](effects-implementation-plan.md).
This supporting requirements note is aligned with its initial release scope;
future capability examples below are explicitly labelled as deferred.

This is an initial design map, not an implemented interface or a committed
configuration schema. The deliverable is reusable transition machinery with a
public GLSL authoring contract, complemented by shared [audio
inputs](audio-inputs.md) for audio-responsive effects. Users should be able to
create full-scene workspace transitions and scene-wide window animations
(`window_scene`) by supplying shader presets. A `window_scene` animation is
triggered by a window opening or closing and can affect the entire scene on that
output, including neighbouring windows and the backdrop. Existing behavior
remains documented in [Effects](effects.md).

The primary acceptance examples are an outgoing workspace scene melting away
to reveal the next workspace, a full-output water effect for window opening and
closing, and a 3D workspace carousel displaying a variable number of workspaces.
These are capability examples and acceptance cases, not
built-in styles that the compositor must recognize by name. Their appearance,
motion paths, and visual phases belong in user-authored GLSL. The compositor
provides sources, geometry, clocks, composition, and lifetime management.

## What exists and what is missing

Umbriel already captures groups of scene nodes for animation shaders. Workspace
effects bind to `Output::viewRoot()` in `WorkspaceGroup::tickAnimations`, after
workspace presentation applies the native slide. This can distort the captured
window group, but does not expose outgoing and incoming workspaces as separately
addressable scenes. Fullscreen windows have a separate root; wallpaper, shell
layers, pinned windows, and other output content are not all under `viewRoot`.

Persistent screen effects can shade the composed output, but have no transition
transaction supplying a destination scene. `umbriel_sample_previous` is the
previous result of this effect instance, not an independent outgoing scene or
the destination. Neither adding feedback nor drawing a larger window effect
provides that missing contract.

The required extension is an owner that retains the source presentation,
prepares the destination, and renders a transition between them. Some effects
also need individually addressable windows and content behind them. A flattened
image cannot reveal pixels hidden behind a window when that window moves away.

Relevant implementation starting points:

- [Workspace switching](../../src/workspace/workspace.cpp): activation, gesture
  progress, slide settling, and workspace lifetime.
- [Output](../../src/output/output.cpp): separate roots and output effects.
- [Effect renderer](../../umbrielfx/types/scene/wlr_scene.c) and
  [shader interface](../../umbrielfx/render/fx_renderer/effect_shader.c): capture,
  composition, input sampling, and feedback.
- [Overview rendering](overview-rendering.md): live surface mirrors and
  wallpaper composition, including frame callbacks and buffer lifetime.
- [Gesture contracts](touchpad-gestures.md): direct manipulation, reversal,
  release velocity, and settling.

## Review against effects PR1 and PR2

Reviewed against PR1 ([#321](https://github.com/noctalia-dev/umbriel/pull/321),
merged as `512e2fb3`) and PR2
([#336](https://github.com/noctalia-dev/umbriel/pull/336), reviewed at
`b1e33849`, still open on 2026-09-28). The checkout includes both implementations.
The contracts in [Effects](effects.md) remain the baseline; this proposal
extends their rendering inputs and presentation scope.

### Reuse the effects system

| Existing mechanism | Use in scene transitions | Required extension |
| --- | --- | --- |
| `EffectPreset`, config registry, source watcher | Same preset namespace, include provenance, source-content equality, diagnostics, and event selectors | Versioned stage sources, capability/resource declarations, typed parameters and schema validation |
| `EffectRegistry::prepare` | One compilation owner, reference-driven preparation, cached failures, renderer recovery | A retained program bundle instead of one fragment program per new-interface preset |
| `AnimatedValue`, transition ID/seed, `Animatable` | Existing clock, progress, cancellation, output render lock, `settle`, and frozen-clock tests | Explicit output presentation transaction and interactive-mode activity |
| Scene capture, working formats, sampling transforms | Pixel/color conventions, imports, clipping and capture-role separation | Independent complete scenes, participant/backdrop inputs and generic geometry/pass execution in UmbrielFX |
| Close snapshots and retained effect requirements | Buffer/program retention after unmap and capture policy after config roots disappear | Transition source handles and program/resource bundle retention beyond a view's live selection |
| PR2 owner selections and inspection | Consume cached persistent selections without choosing again; inspection remains read-only | Describe new program-bundle readiness and active transition state without mutating it |

Keep the public `[effects.preset.<name>]` namespace and
`[animation.<event>] effect = "<name>"` binding model. The preferred direction
is a versioned interface within `kind = "animation"`, with explicit compatible
events and scope, rather than another parallel preset registry or shader-path
binding. Exact new keys remain provisional. Existing `shader` files and the
`animation(vec2 uv)` ABI keep their current meaning. New-interface presets must
not flow through the legacy single-program lookup or scene-slot binder.

Extend `EffectRegistry` with typed access to a compiled transition bundle;
UmbrielFX owns its stage programs, textures, meshes and passes. The compositor
owns events, source membership and lifetime. Preserve the existing 13-slot
composition contract for ordinary effects. A whole-output transaction is not
an extra persistent screen slot, nor an arbitrary pass graph squeezed into an
existing node slot. This is still TOML plus GLSL, not a native plugin loader or
another scripting runtime; rendering belongs in UmbrielFX as required by
[SCOPE.md](../../SCOPE.md).

### Preserve PR2's selection boundary

PR2 pools, holdings, history, suppression, and set/cycle/toggle/reset actions
cover persistent border/window/screen/cursor selections. Animation bindings are
explicitly preset-only. Keep that boundary for this work: none of these examples
requires animation pools or sixteen more runtime actions. Transition-preset
randomisation would need its own per-event lifetime contract later.

A carousel face, participant mirror, retained scene, or closing copy is not
another selection owner. It consumes the original owner's resolved assignment;
it neither reserves another pool member nor changes selection because it becomes
visible. Rendering and inspection must not call selection/RNG, enumerate owners
for holdings, perform source I/O, or compile programs. Live persistent runtime
changes affect live inputs through their original owners; retained snapshots
keep their copied program versions. Copies must not extend a mapped selection's
lifetime after unmap, although their rendering requirements remain retained.

### Program preparation, reload, and failure

Extend the existing reference walk from an enabled event to every stage/resource
dependency of its selected preset. Unreferenced declarations remain inert.
Keep configured persistent-action roots, pool expansion, overlays, and runtime
overrides exactly as PR2 defines them. Every source file participates in
include-relative resolution, watching (including missing files), validation,
and config equality. Bound both individual sources and total bundle size.

The compiled cache identity must include interface version, all stage contents,
and compile-affecting declarations. A bundle becomes usable atomically only
when its required programs/capabilities are ready. A failed bundle stays selected
and reports failure while rendering the ordinary event fallback; it must not
mutate a selection or partially execute a graph. Preparation happens before
rendering, with failures cached until relevant source/capability changes.

An in-flight transition retains one coherent bundle across source reloads;
never combine a new vertex stage with an old fragment stage. Retain its resource
layout and typed parameter schema/values with that bundle. Existing animation
programs retain code but can receive refreshed time/palette uniforms on reload;
do not claim they already snapshot all configuration. Define live shared palette
updates separately from the new bundle's immutable parameter schema. Disabling
or removing the binding cancels its presentation safely. Renderer replacement
invalidates GPU resources, cancels the new presentation, and prepares current
roots without changing PR2 assignments, including while time is frozen.

### Scheduling and window lifetime

Use the existing animation clock and `Animatable` scheduling for finite scene
transitions; `EffectLedger` remains the scheduler for persistent effects that
read time. A progress-only shader still requires transition frames. Conversely,
an idle carousel with live client content need not spin a timer unless navigation,
settling, or an authored time-dependent pass needs it. Track its active input
mode separately from finite animation activity: an idle held carousel permits
`settle` after ordinary barriers while still disabling scanout/tearing. Persistent effects
retain their existing `max_fps` behavior and do not become finite animations.

There must be one owner for each clock and presented transform. Ordinary
`windows_in`, `windows_out`, and `windows_move` already have independent
lifetimes: tiled opening starts alongside reflow; closing copies retain their
own fixed canvas and clock. Do not globally merge these clocks to implement
`window_scene`. An explicitly selected output-scoped lifecycle transaction
exposes lifecycle/motion progress separately and applies residual motion around
current native presented boxes. Capture bypasses the target lifecycle
presentation it replaces to avoid applying fade/deformation twice. The
`window_scene` animation ends at the triggering lifecycle deadline; longer
reflow continues normally. Overlapping lifecycle events cancel the
`window_scene` transaction and run the burst natively at existing
progress/deadlines. The existing configure barrier and `MonotonicEasing`
continue to govern tiled geometry even if GLSL bends or displaces its
presentation.

Ordinary position/size changes and reflow from the triggering event are expected
inputs, not cancellation triggers. Participant migration means a change of
workspace/output ownership; topology mutation changes the admitted participant
set or stacking contract. Keep those distinct from native layout motion.

A single output coordinator cannot silently cancel an earlier close when a
second window opens. It must preserve retained closing content until its visual
obligation ends or apply a documented cancellation fallback. Reuse snapshot
retention rules, but do not rely on a raw `CloseSnapshot` pointer after the
server's post-tick reap. Gesture reversal preserves transition identity and seed;
an actual retarget renews them according to the existing animation contract.

### Composition gaps requiring explicit work

PR1's in-place window/overlay shaders sample the current render target. Inside
an isolated participant capture they see that capture, not the desktop behind
the window. Therefore moving already-shaded window images cannot promise exact
visual continuity for every existing backdrop-sensitive shader. The initial
participant path explicitly falls back for legacy in-place effects and backdrop
blur. A bounded replay experiment may establish a precisely scoped exception;
it cannot claim general shader equivalence. Existing shader semantics must not
silently change globally.

G2 must publish accepted/rejected cases for representative upstream presets,
including border-only themes, in-place effects and blur. A fork's configuration
is not evidence of the upstream rejection rate. Prominently document fallback
for common themed configurations and report the reason in inspection.

Border light is a separately stacked proxy and is suppressed under transient
ancestors; copied snapshots disable its emission. Shadows also sit outside the
content tree and may derive their silhouette from shader output. Ordinary
shadows and lit borders are required `window_scene` support: preserve
owner-linked ordered items, pooled-shadow placement and the separate
screen-blended light stratum. Avoid duplicate emission and regenerate
transformed silhouettes as needed; closing copies keep PR1's no-light rule. A
complete scene capture needs to reproduce the relevant separate scene roots, not
just copy `viewRoot`. Exact endpoints must be demonstrated with these features
enabled, not inferred from a plain opaque-window example.

Screen and cursor effects belong after transition composition under their
existing gates. Do not bake them into every face and apply them again to the
finished output. For `in_capture = false`, build unfiltered source inputs and
feedback separately from display inputs; a display screenshot cannot serve both.
Preserve the isolated toplevel-capture scene: sharing one window must not expose
the `window_scene` backdrop or other participants.

Keep the renderer's history promotion on successful submission, per output,
renderer, transition, program and composition role. Extend identity with pass
and participant where necessary. Shadow/source re-renders read history without
advancing it; a failed pass must not publish half a new multipass state. The
current single-result feedback helper is useful infrastructure, not an existing
general simulation scheduler.

PR1's no-effect cost contract and PR2's unused-pool contract remain acceptance
gates. No active new transition means no new captures, meshes, resource walks,
timers or scene nodes. Compiling a referenced preset is distinct from allocating
its execution resources. Output-local damage/culling/scanout isolation requires
new work: existing transient slots still impose scene-wide conservative costs.

## Effect map

These examples map visual requirements to reusable capabilities. Adding another
effect within these capabilities should require a preset and GLSL files, with
no new compositor event, native style enum, or effect-specific C++ code.

| Family | Workspace change | Window use | Required inputs beyond today's single animation target |
| --- | --- | --- | --- |
| Crossfade / wipe | Whole outgoing desktop becomes incoming desktop | Replace a selected window or reveal an opener | Separate source and destination, shared coordinates |
| Scene melt | Outgoing scene melts away, exposing the intact destination underneath | Not required for this acceptance case | Separate complete scenes, source displacement and coverage |
| Water (`window_scene` example) | Not required for this acceptance case | Full-output wave moves neighbours, forms an opening window or dissolves a closing one, then drains | Backdrop, separate window participants, destination layout, shared water field, retained closing content |
| 3D workspace carousel | Display and rotate among N workspace faces, including fewer or more than four | Not required for this acceptance case | Multiple live scene inputs, count/order metadata, authored perspective geometry, input ownership |
| Page / fold | Desktop folds away to expose the next | Window folds open or closed | Two inputs and projected geometry or inverse projection |
| Portal / iris | Destination appears through an expanding opening | New window emerges through a local opening | Two inputs and a mask; separate participants if neighbours react |
| Shatter / particles | Desktop fragments reveal the next | Window assembles or breaks apart | Source sampling, deterministic fragment state; separate geometry for depth |

A full-output effect describes its visual extent. A fullscreen application is
one possible participant. Entering fullscreen, switching workspaces, opening a
window, and changing focused window remain distinct triggers.

## Shared transition model

Proposed conceptual inputs; the authoring contract below sketches their GLSL
exposure, with exact identifiers still provisional:

- **Source and destination:** independently renderable presentations in a
  common output-local coordinate system. A frozen source plus live destination
  is sufficient for the first proof; the workspace carousel needs multiple live
  scenes, with stable identities and explicit count/order metadata.
- **Participants:** stable identities, source and destination boxes, stacking,
  content including subsurfaces and decorations, shadow ownership, and lifecycle
  status. Window-aware effects request these instead of only flattened scenes.
- **Backdrop:** independently renderable content underneath participants, so
  moving a window does not leave a hole or a baked-in duplicate.
- **Timeline:** existing animation clock, stable transition ID and seed,
  progress, direction, and gesture velocity. A reversible effect evaluates from
  progress; it cannot depend exclusively on irreversible feedback history.
- **Policy:** included layers, live versus retained sources, capture role,
  input ownership, interruption behavior, and resource limits.

One transition controller per affected output should coordinate presentation,
with the lifecycle arbitration described above.
Workspace and view owners retain authority over logical membership, focus,
configure requests, and object lifetime. A shader must not create or rearrange
real windows. Layout computes the destination once; presentation animates toward
it without issuing a configure for each ripple or face rotation.

Provide two rendering paths behind that ownership: a two-scene composite and a
participant/face composition. Presets declare which capabilities they require.
A two-scene effect can use inverse projection in a fragment shader or textured
quads. The workspace carousel additionally requires a collection of scene faces,
camera/navigation state, and visibility handling. Two endpoint samplers alone
do not satisfy that acceptance case.

Existing window effects retain their selection and slot ordering; their capture
sampling needs the explicit compatibility work described above. A scene
transition must
replace the native workspace slide presentation when selected, otherwise rotation
or melt would operate on an already sliding scene. Participating window motion
must likewise have one presentation owner rather than adding scene-animation displacement
to independently evolving native motion without a defined composition rule.

## GLSL authoring contract

Keep today's `animation(vec2 uv)` interface for existing presets. Introduce a
versioned transition interface alongside it, since two independent scenes and
participant geometry have materially different requirements. The following
names illustrate the proposed contract; none are available configuration or
shader symbols yet.

### Scene composition

A fragment entry point receives output-local normalized coordinates and can
sample source and destination independently:

```glsl
// Proposed interface: example crossfade authored entirely in GLSL.
vec4 transition(vec2 uv) {
    float p = umbriel_clamped_progress;
    return mix(umbriel_sample_from(uv), umbriel_sample_to(uv), p);
}
```

The same two samplers support authored warps, masks, two-face projections,
and melting. The compositor supplies undistorted inputs; GLSL chooses
where to sample them and how to combine them. Window opening/closing uses the
separate `window_scene` participant profile, which can also cover the output.

Specify the contract for UV orientation, output-local logical pixels, physical
resolution, fractional scale, output transforms, transparent out-of-bounds
sampling, premultiplied alpha, and the renderer's working color space. Sampling
helpers hide texture placement and output rotation. Endpoint images must match
ordinary rendering in the same composition role; backdrop-sensitive existing
effects require the compatibility work identified in the PR review. Shared
uniforms include eased and linear progress, elapsed time, delta time, direction/axis, output size,
stable random seed, and the triggering window's source/destination boxes when
there is one. Publish gesture position separately from clamped progress so a
preset can deliberately handle overshoot and reversal.

Scene metadata uses a separate typed descriptor owned by an active transaction,
not extra entries in the legacy eight-uniform table or larger per-node slot
parameters. C1 freezes its layout, packing and stage-specific budgets alongside
the entry points. Palette, audio, user parameters and samplers all count against
the queried GPU limits. Reuse binding helpers without requiring uniform-buffer
objects or scene allocations for ordinary effects.

Audio is another optional input, shared with persistent effects through the
[audio-input contract](audio-inputs.md). RMS, peak, envelope and frequency bands
may modulate shading or presentation, but must not choose pool
members, restart progress, change logical window geometry, or prevent completion.
All passes of one composition use the same immutable audio snapshot. Modulation
that changes geometry or coverage must vanish at the exact transition endpoints.

### Participant geometry and shading

Expose generic draws for scene faces and window participants. Bind one
participant's content and metadata per draw; the baseline must not require an
unbounded sampler array. Provide source/destination rectangles, presence at each
endpoint, lifecycle role (entering, leaving, surviving), stable per-transition
index/seed, and stacking information. Do not encode a process-wide 64-bit ID in
a floating-point uniform. Scene-face draws additionally expose total workspace
count, stable ordering for the current interaction, face index, active/selected
indices, and continuous navigation position. Bind each face's scene texture for
its draw rather than restricting the collection to source and destination.

The geometry profiles' GLSL vertex hook transforms a compositor-supplied quad or bounded
tessellated grid into homogeneous clip coordinates, retaining perspective-correct
texture interpolation. The fragment hook samples the current item; a final
output composite samples the completed composition. Participant shaders can
change their coverage within their profile. Window bounds
do not limit the final draw: a participant can move across its output and a
separate full-output pass can draw the surrounding effect. Output containment
and resource bounds remain compositor responsibilities.

This gives authors control of the carousel's face transforms and `window_scene`
participant displacement. Shader motion changes presentation only. The
compositor does not read back vertex positions to update layout or send client
configures. The compositor owns the bounded geometry and fixed profile draw
state. Participant passes preserve painter order and premultiplied blending.
Scene-set faces are opaque depth-tested draws with blending disabled and output
alpha forced to one; transparent/discarded face composition is outside that
profile.

Shaders share analytic functions and uniforms across stages to coordinate
scene shading and window motion. Intermediate field textures and
vertex texture sampling are deferred; there is no fragment-to-vertex dataflow
promise in the initial interface.

### Backdrop and pass composition

Participant mode draws static backdrop/stacking bands without baked-in
participant images, separate participant draws, and a composed result for a final pass. A preset
can therefore uncover the desktop as a window moves, refract the composition,
and draw beyond every original window rectangle.

The architectural review selects fixed scene-pair and ordered-geometry-plus-
composite pipelines for the first version. Static bands fill exposed regions;
the final shader can refract the completed composition. Shared analytic GLSL
functions coordinate participant geometry and scene shading without a simulation field or
user-defined pass graph. No pass samples the attachment it is writing.

Generic pass graphs, field textures and new transition feedback are deferred.
They would need explicit dependency, format, budget, initialization, time-step,
reset and reversal contracts. Existing effect feedback retains PR1 semantics;
previous output is not a substitute for an independently retained source scene.
Repeated source/shadow draws must not advance legacy feedback per replay.

### Presets, events, and compatibility

A transition preset declares its interface version, GLSL stage files, required
scope, bounded mesh resources, and typed user parameters. Reuse the
existing preset loading, source watching, diagnostics, and program retention
where possible. Unsupported requirements or compile/allocation failure choose
a defined ordinary-animation fallback with a useful diagnostic.

`interface` selects the shader ABI version; `scope` selects an execution contract
within it. Initially, omission retains the legacy ABI and the only explicit
interface is `scene-v1`. Reject unknown versions and invalid combinations.
Validate keys locally and resolve cross-preset/event references after all
presets are loaded, using the existing deferred validation pass.

Events select compatible presets. The `window_scene` scope lets a window event
request an animation with scene-wide shading and neighbouring participants;
scope cannot be inferred solely from the trigger. A workspace event supplies
workspace scenes/faces to the same machinery. Timing and input state belong to
the event owner, while GLSL determines visual choreography throughout that time.
Configuration must define how a scene preset takes presentation ownership from
the event's native animation and any participating reflow, avoiding double
transforms.

Initial scene presets evaluate from progress/seed and shared inputs without new
transition history. Stateful simulation and its reversal contract are deferred.
The compositor owns commit, cancellation, lock, focus,
and cleanup even when a shader cannot reach its intended visual endpoint.

The workspace carousel requires a generic navigation/input mode supplying face
order and continuous gesture/navigation state. GLSL maps that state to camera
and face transforms; the compositor owns the selected workspace, commit, and
cancel. A visual shader alone cannot define new input bindings or choose the
active workspace. This input contract is part of carousel acceptance, alongside
the rendering hooks.

## Acceptance: `window_scene` on window open and close

The trigger is a window opening or closing; the visual stage is the whole
affected output, including the existing windows. The opening/closing window
need not itself be fullscreen. Both directions are required.

Water is one concrete acceptance preset for `window_scene`; its waves and visual
phases are authored in GLSL. The scope also supports other appearances, such as
portal emergence. The water example for opening a tiled window could implement:

| Phase | Appearance | Presentation responsibility |
| --- | --- | --- |
| Arrival | A wave enters from the chosen edge or origin and refracts the desktop | Draw water across the output, with backdrop sampling |
| Displacement | Existing windows travel with the wave and make room | Move separate window presentations toward their layout destinations; optionally bend their edges |
| Formation | Water gathers around the opener and resolves into its content | Reveal the participant using the shared analytic wave and its lifecycle progress |
| Drain | Water recedes and residual ripples fade | Reach native-current presentation at the lifecycle deadline, release the transaction resources, and let any longer native reflow finish |

For closing, the wave enters across the desktop, picks up the closing window
and dissolves it into water, moves surviving windows toward the layout without
it, then drains. The client may already have unmapped or exited: retained visual
content must remain available until this presentation finishes. The preset can
author a distinct close sequence; it need not play the opening frames backward.

The final positions are the normal layout result. Floating lifecycle events need
not permanently rearrange neighbours: they can move aside temporarily and
return. Workspace switching uses a separate scope and is not part of this
`window_scene` acceptance case.

An initial example can use an analytic travelling wave, displacement field,
refraction, and formation/dissolution masks driven by progress and seed. This
gives controllable, reversible choreography without requiring a fluid solver.
A later simulation
could add velocity, pressure, and obstacles, but would need explicit budgets,
time stepping, and a policy for reversal or restarting. Existing color feedback
alone is not a complete fluid simulation contract.

Acceptance requires:

- Open a third window while two existing windows move independently with the
  wave. Show water beyond all three window rectangles and correct backdrop in
  the regions they uncover.
- Close that window, including a client that exits immediately, while the
  retained closing image dissolves and surviving windows move independently.
- Exercise tiled and floating windows, close during formation, and a second
  lifecycle request during drainage. The initial overlap policy cancels the
  `window_scene` transaction to native current-progress presentation and runs
  the overlapping burst natively, preserving all original close/reflow
  deadlines. The `window_scene` animation ends at its triggering lifecycle
  deadline; longer native reflow may continue afterwards.
- Include ordinary shadows and lit borders. Backdrop blur and legacy in-place
  window/overlay effects initially cause explicit compatibility fallback; do not
  remove those effects silently to admit a `window_scene` animation. Revalidate
  on runtime changes.
- Edit wave shape, phase timing, refraction, formation/dissolution masks, and
  window paths through GLSL and preset parameters without rebuilding Umbriel.

Record the tiled/floating overlap fixture and obtain explicit visual acceptance
of its native-current handoff. One documented jump is allowed; stale content,
restarted deadlines, queued animations and recurring handoff flicker fail. Also
record G4's dismissal fixture: the first complete click/touch sequence cancels
without reaching a client, and the next activates the visible native target
exactly once. Hover focus and cursor/grab state must recover after cancellation.
The existing drag-grid inverse relies on enforced no-folding limits and does
not provide inverse hit testing for arbitrary scene shaders.

## Acceptance: 3D workspace carousel

The supplied [video reference](https://www.youtube.com/watch?v=Ban7wspkrNQ)
could not be played during this initial mapping; exact choreography still needs
visual confirmation. As a technical reference,
[Wayfire's cube implementation](https://github.com/WayfireWM/wayfire/blob/master/plugins/cube/cube.cpp)
renders workspace streams into separate framebuffers and projects those faces.
Its implementation supports the architectural distinction between retaining
scene inputs and merely distorting one composited output.

The acceptance target is a rotating 3D display of workspaces with a variable
number of faces. Call it a workspace carousel; a cube describes only one possible
appearance for one workspace count. Individual windows on faces are outside
this acceptance case.

Accepted framing policy: equal-sized, output-aspect face canvases regardless of
the number or extent of windows. Default viewport framing shows the normal
workspace view. Optional fit-all framing uniformly fits the workspace viewport
and its window bounds, including off-viewport scrolling content, into that same
canvas. Preserve layout, aspect ratios and occlusion; fill spare space with
background. Empty workspaces remain selectable background faces. Do not build an
irregular polygon to accommodate different content widths. The
[implementation plan](effects-implementation-plan.md#accepted-face-sizing-and-framing)
defines bounded rendering and the animated return to the normal viewport.

Enter from the active workspace, pull back to expose the spatial arrangement,
rotate through the workspace collection, then land on the selected workspace or
cancel to the source. Each face contains a complete workspace scene, including
wallpaper and its windows. Content remains live while displayed. The shader
controls face placement, rotation, camera distance, projection, and shading;
the compositor provides stable scene identities and navigation state.

The example preset can arrange N faces around a ring or prism for N >= 3,
provide a nondegenerate two-face arrangement for N = 2, and a single scene for
N = 1. Those geometry choices live in GLSL. One workspace must not create fake
selectable workspaces, and two must not rely on a singular prism formula.

Acceptance requires:

- Run with 1, 2, 3, 4, 5, and 8 workspaces, including empty workspaces and live
  animated content. Verify every workspace is reachable and correctly mapped
  to its face; a two-texture turn is insufficient evidence.
- Run both framing modes with four workspaces containing 1, 4, 2 and 0 windows.
  Face dimensions stay equal. Fit-all includes off-viewport window bounds with
  preserved layout, and commit/cancel restores the normal viewport without
  resizing clients. A long scrolling layout must not allocate an unbounded
  content-width texture; shared panels retain their face-relative placement.
- Hold and reverse rotation, select a nonadjacent face, commit, and cancel.
  Ordinary input reaches the selected workspace only according to the mode's
  input contract, and the final face matches the resting output exactly.
- Exercise both workspace axes, configured cyclic/noncyclic navigation, and
  changing workspace membership during the mode. Freeze ordered stable workspace
  IDs, defer automatic pruning until exit, and cancel before explicit inventory
  mutation rather than silently rebinding a texture to another workspace.
- Make all displayed faces available within declared resource limits. Bound
  resident textures and capture resolution, with a documented degradation or
  fallback when the budget is exceeded, rather than imposing four faces.
- Exercise all 64 faces at a resolution/format whose complete reservation fits.
  Separately, 64 quarter-dimension 4K RGBA16F faces plus one native landing image
  require 316.406 MiB before scratch/depth/extra roles, exceeding 256 MiB/output.
  That case must decline atomically with `resource_budget`, preserve ordinary
  navigation and release provisional resources. Do not omit faces, defer the
  landing reservation or assume an RGBA8 subtotal proves the full budget fits.
- Change the spatial arrangement and rotation style through GLSL alone. Generic
  face draws, navigation, and resource handling must contain no preset-specific
  rotation logic.

## Acceptance: workspace scene melt revealing the next workspace

The trigger is a workspace switch. The outgoing scene melts and drains away as
one complete image, including its wallpaper and windows, revealing the intact
destination behind it. The destination is independently rendered at its resting
coordinates; it does not have to melt or move with the outgoing image.

An example shader displaces the source downward with uneven drips and reduces
its coverage as it flows away. Wherever source coverage disappears, the next
workspace is visible. Authors choose the direction, melt profile, edge shading,
and timing. This requires independent scene sampling and source alpha/coverage,
not individual window lifecycle triggers or a crossfade of both scenes.

Acceptance requires:

- Begin with exactly the source scene and end with exactly the destination.
  At an intermediate frame, show displaced source pixels and a recognisable,
  undistorted part of the destination through the exposed region.
- Use different wallpaper and window content in the two inputs so a test can
  distinguish a whole-scene melt from windows melting over a stationary shared
  wallpaper. Test fixtures may supply distinct backgrounds even when ordinary
  desktop configuration uses the same wallpaper on both workspaces.
- Exercise empty source/destination workspaces, fullscreen application content,
  translucent windows, and direct nonadjacent switches. No native slide runs
  beneath the effect and no vacated source region becomes an unintended hole.
- Reverse a gesture-driven analytic melt and retarget during a timed melt
  according to the declared interruption policy. Reach a clean endpoint and
  release retained scenes.
- Alter the melt entirely through GLSL. The engine supplies no hard-coded drip,
  gravity, or dissolve choreography.

## Additional authoring acceptance examples

These smaller cases exercise capabilities that the three primary examples may
not isolate. Initial examples use the fixed interfaces; deferred rows are future
acceptance targets and do not imply that their mechanisms ship in version one.

| Example | Trigger and expected visual | Distinct evidence |
| --- | --- | --- |
| Directional wipe / iris | Workspace switch reveals the destination through a moving edge or opening | Two-scene sampling, exact coverage/endpoints, arbitrary progress evaluation and reversal without feedback |
| Hinged page peel | Outgoing workspace bends away, showing its shaded back and the next scene beneath | Tessellated vertex deformation, perspective interpolation, front/back shading, and explicit depth/alpha policy |
| Window fragments (deferred) | A closing window separates into seeded pieces that travel beyond its original bounds | Future repeated-geometry capability, retained closing content and source subregion sampling |
| Reflow wake (deferred) | Moving/resizing windows leave short fading or refractive trails which disappear as the event settles | Future transition-history capability, with reset and cleanup at the event deadline |
| Portal emergence | A window opens through an aperture while a halo/refraction extends onto the surrounding desktop | Trigger-local metadata combined with output-wide passes, backdrop sampling, and independent window coverage |

Window fragments require a bounded instance/repeated-draw capability in the
geometry contract; a fixed quad or grid alone does not promise arbitrary piece
creation. Reflow wake must fit its decay inside the event owner's timeline;
PR1 explicitly gives shaders no ability to extend a timeline. A separately
configured finite tail would be a future animation-contract change, not an
implicit feature of reading feedback. These requirements should be
explicit capabilities rather than hidden special cases for the example names.

Audio variants add independent coverage: water amplitude responds to bass while
open/close deadlines remain fixed, carousel lighting responds to frequency bands
without selecting a workspace, and melt edge detail responds to level while
source coverage still reaches zero. Inject synthetic signals so these checks
prove input binding independently of real audio devices.

## Composition and lifetime decisions

Proposed starting policies, to be validated with the first prototype:

- Workspace faces include wallpaper/background, bottom-layer content, normal
  windows, fullscreen windows, their decorations, shadows, and closing copies.
  Their composition must reproduce the resting output's applicable stacking.
  Shared wallpaper may be sampled by both faces without creating real copies
  of the wallpaper client.
- Scene-pair/set inputs capture the desktop through pinned windows, including
  top panels, preserving fullscreen-over-panel stacking. These shared desktop
  elements move with each face; overlays/compositor UI and cursor stay outside.
  `window_scene` preserves static bands between its moving participants. More
  general stationary-versus-moving layer policies are deferred.
  Per-namespace participation via `[[layer_rule]]` would need additional bands
  and admission tests; the initial ABI does not promise arbitrary exclusions.
  Include actual panel motion in manual melt/carousel review.
- Preserve relative stacking across captured and stationary content. A single
  flat scene texture cannot interleave a stationary panel between two moving
  layers; such a policy requires multiple composition bands or a restriction on
  which layers may stay stationary.
- Suppress duplicate ordinary drawing while a participant is presented by the
  transition. Live sources still receive correct buffer release, presentation
  feedback, and frame callbacks; hidden destination rendering must not make
  that destination input-active.
- Keep semantic focus changes tied to existing actions. Timed presentations
  suppress hover targeting; a new activation cancels and consumes its complete
  input sequence. Active grabs prevent admission. The workspace carousel owns
  selection input until commit/cancel. General inverse hit transforms are deferred.
- Workspace identities are assigned at construction. Window migration changes
  face content; workspace destruction/replacement or output removal cancels
  before frozen identities become invalid. Any future workspace-transfer path
  must cancel before changing ownership or identity.
- Reversing a gesture retains its source/destination pair and seed. A new timed
  workspace request ends presentation at the authoritative destination and begins
  a fresh pair/identity. Overlapping window events use the native fallback above.
  These cancellation paths permit a documented visual discontinuity.
- Unmapped participants retain buffers only as visual snapshots. Output removal,
  session lock, renderer loss, and allocation failure must cancel safely, drop
  retained resources, and render the authoritative destination or lock scene.
  An unavailable destination buffer must not stall the compositor indefinitely.
- Preserve the existing distinction between transient animation and persistent
  effects in captures. Display and unfiltered capture inputs/history must remain
  separate where `in_capture` excludes a persistent effect; do not reuse a baked
  display snapshot that reintroduces it. Keep the documented export-dmabuf
  behavior. Capture inclusion for the new transient mode needs explicit tests.
- Allocate only while active and bound faces, participants, resolution, and
  history. Two 3840×2160 RGBA8 textures alone cost about 63 MiB (about 127 MiB
  in RGBA16F), before output targets, scratch buffers, feedback, or window textures.
  Begin with full damage on affected outputs and measure; the current transient
  slot policy damages all outputs, so confinement needs renderer work.

## Delivery order and evidence

1. **Versioned scene interface and ownership.** Expose source/destination
   samplers and transition uniforms through loadable GLSL presets. Bind window
   and workspace triggers, with explicit presentation scope. Prove whole-scene
   wipe and the scene melt revealing an intact destination, including wallpaper
   and a fullscreen window, with no native slide underneath.
2. **Participant and face interface.** Add separate backdrop/content capture,
   metadata, vertex transforms, bounded meshes, and fragment hooks. Prove a
   GLSL workspace carousel with multiple live faces and its generic navigation
   contract, plus independent motion of multiple existing windows. Exercise
   perspective, transparency policy, and output-wide drawing bounds.
3. **Window scene animations (`window_scene`).** Preserve static bands,
   shadows/light and lifecycle handoff; prove scene-wide opening/closing with
   editable water and portal examples using the fixed pipeline. No generic graph
   or fluid solver is a prerequisite.
4. **Authoring and compatibility.** Document stage interfaces, sampling/color
   contracts, parameters, capability declarations, and diagnostics. Ship small
   editable example presets and check hot reload, retained in-flight programs,
   invalid shader fallback, and compatibility with existing animation presets.
5. **Deferred extensions.** Repeated geometry, new transition history and
   simulation need separate capability proposals and acceptance evidence. They
   do not gate the initial delivery; multi-workspace navigation is already
   required by carousel acceptance.

Use the frozen animation clock and headless pixel checks for deterministic
endpoints and intermediate phases. Exercise rapid repeated requests, close
during transition, empty workspaces, lock, output removal, renderer recovery,
capture policy, and multi-output isolation. Check that all resources and frame
requests disappear at rest. Hardware review is needed for smoothness and GPU
cost; deterministic pixels alone cannot establish either. Acceptance requires
changing each example's visual behavior by editing GLSL alone, without adding
effect-specific native code.

Use the implementation plan's named performance workloads: idle/silent input,
small repeated damage under a window/overlay effect, video with border lighting,
and transitions returning to rest with another output unaffected. Measure before
adding optimizations. Shadow shape preservation and local sampling are different
contracts; frozen time alone does not validate an emission cache. Preserve legacy
time semantics, including at long uptimes, rather than introducing an implicit
clock wrap.

Carry forward the PR1/PR2 regression gates, with targeted new assertions:

- `effect/selection`, `effect/registry`, and `effect/reload`: mirrors never add
  holdings or consume RNG, inspection stays pure, stages prepare before use,
  and reload/recovery retain selections. Mutate one stage to verify atomic
  bundle failure and repair; unused presets/pools allocate no runtime resources.
- `effect/window_capture`, `effect/capture_feedback`, and `effect/screen_cursor`:
  display/unfiltered sources remain separate, screen effects apply once, and
  isolated window capture contains no neighbour or output backdrop.
- `effect/animation_lifetime`, `effect/animation_feedback`,
  `effect/animation_seed`, and the animation shadow checks: retain exact copied
  programs/requirements, promote history only after submission, and distinguish
  gesture reversal from retargeting without duplicated light or shadows.
- Tiled lifecycle/reflow, close-snapshot workspace, and renderer-recovery checks:
  preserve independent clocks and configure barriers outside opted-in
  transactions, handle repeated lifecycle requests, and recover while frozen.
  Use targeted negative controls for new pixel/state assertions, following PR2's
  validation approach. Documentation review alone does not establish these gates.

Audio input delivery can proceed independently of scene capture: prove it first
with existing persistent presets, then reuse the same interface in transitions.
It requires an explicit extension to PR1's time-only persistent frame eligibility;
the [audio proposal](audio-inputs.md) records that change and its cost gates.

The [implementation plan](effects-implementation-plan.md) resolves the initial
layer, interruption, opaque-face, shader/profile and resource directions and
defines bounded evidence gates before ABI freeze. Water appearance and carousel
choreography remain author choices. Deferred examples do not imply support for
particle instances, generic graphs or transition simulation in the initial ABI.
