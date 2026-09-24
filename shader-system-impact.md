# Impact of the unified effect proposal

Assessment date: 2026-09-24. Source baseline: local HEAD `a258053`, plus the current
example files. This is a source review, not an implementation or a performance
measurement. The pre-existing edit to `docs/examples/config.toml` was left alone.

This is the historical assessment of the preliminary design. The completed
[specification](shader-system-proposal.md) is authoritative for current decisions;
references below to missing mappings or unspecified behavior describe the earlier
draft, not outstanding holes in the completed schema. Configuration conversion
tooling remains deferred. No implementation or performance verification has occurred.

## Decisions made after the external schema review

| Earlier finding | Completed specification |
| --- | --- |
| Animation ownership unclear | Native animation settings remain; all custom shader definitions/selections move to effects; pair/preset selection is removed. |
| Curve defaults and overrides unclear | Open/close inherit native timing unless overridden; named curve registries remain; other events follow native clocks. |
| Missing native event targets | Workspace, overview, scratchpad, backdrop, focus, and border.focus are explicitly mapped; layer open/close use layer rules. |
| Screen collapse loses a rendering stage | Separate screen and overlay leaves, explicit cursor placement, and named effect_region entries preserve composition. |
| Settings have no home | Render.effects owns subsystem policy; leaf tables own palette/time/focus/light/cursor controls. |
| Builtins, params, feedback, pass limits unspecified | Explicit builtin/file distinction, typed params, retained feedback contracts, and 1–16 passes per enabled leaf. |
| Include merger can append passes | Each effect has one defining file; cross-file redefinition is rejected without changing unrelated merge behavior. |
| Border pairing can leave old inner effect | External-only replacement definitions explicitly disable border.inner. |
| Palette publication changes authored colors | Palette opt-in is retained on every leaf. |
| Unused-first allocation omitted | Choice policies include unused_first with least-used exhaustion, round_robin, and random; lease/snapshot lifetimes are separate. |
| New reactive move/resize clock is substantial work | Version 1 follows the existing movement timeline and retains pointer wobble; speculative generic velocity/settling behavior is excluded. Identical move/resize pipelines coalesce. |
| Shader failure transaction unclear | Candidate GPU programs prepare before enabled generation commit; invalid reload keeps the working generation. Offline validation states its GPU limit. |
| Runtime controls and inspection missing | Effect action grammar, target/scope masks, cycling/default behavior, and effects inspection are specified. |

The substantive required additions remain per-subject pipeline resolution, event and
outer-border multi-pass support, typed parameter binding, layer integration, reload
transactions, runtime controls, and provenance. Retaining scope-specific GLSL hosts
and native clocks avoids a universal shader ABI rewrite or new motion simulation.

## Upstream impact, excluding the fork's added shader system

Clarification following direct comparison with the locally recorded upstream base,
`upstream/main` at `a607fdf`: the assessment below primarily measures replacing the
fork's shader system. It must not be read as requiring a broad rewrite of upstream
compositor behavior. No latest-upstream fetch was performed for this comparison.

Upstream already has custom animation shaders for nine event categories, native
animation clocks/curves, scene effect slots, close snapshots, animated shadows,
overview composition, and resize/configure synchronization. The fork adds persistent
border/content/output processing, pools, paired selections, and interactive wobble.

Excluding those additions, the expected integration changes to existing upstream
code are bounded:

- Replace existing custom-shader lookup at animation event hooks with resolved
  effect lookup, including per-surface selection where requested.
- Extend view/layer and snapshot state to carry selected pipelines through existing
  lifecycle operations, including overview copies and in-flight reloads.
- Feed position/size changes into effect event state without changing layout,
  configure/ack handling, pointer coordinates, or native geometry animation.
- Add effect references and dependencies to configuration, rules, and selective
  reload handling. Retiring existing `animation.<event>.shader` keys is the direct
  upstream configuration break if all custom shaders move to the new model.
- Extend existing scene/render integration so active effects compose correctly with
  borders, shadows, captures, scheduling, and resource cleanup.

Native animation enable flags, curves, styles, and durations need not be removed to
unify custom shader selection. Ordinary borders, shadows, blur, layout, focus,
workspaces, and input should retain their behavior when no new effect is selected.
That is a proposed implementation boundary to verify with regressions, not a claim
that any renderer edit is risk-free.

The proposal now explicitly avoids three unnecessary broad changes: making an absent
shader disable native open/close animation; replacing native movement timing with
shader settling timing; and changing include merging for unrelated configuration.
It retains native fallback animation, requires separate effect-response state, and
confines any new definition-merge rule to the effect library. The existing upstream custom hooks
for workspace/overview/scratchpad/focus now have explicit scope mappings; their native
behavior does not need a redesign. Version 1 omits the speculative response/settling
clock and follows native event timing.

## Historical assessment of the preliminary draft

This is a substantial refactor across configuration, effect selection, scene
integration, and parts of the renderer. It does not require replacing the compositor
or discarding its shader implementation.

The central idea — one named effect containing scoped pipelines — is compatible
with reusing most of the existing rendering foundations. Several additional promises
in the draft are new features or behavior changes, rather than consequences of that
idea. The largest are arbitrary pass chains for borders and animations, independently
selected per-surface transitions, continuous move/resize event state, and a shared
parameter/authoring contract.

The initial draft also omitted existing capabilities. The revised proposal marks
those mappings as incomplete rather than treating omission as removal. They still
need explicit decisions before it is a complete replacement specification.

## Existing implementation and proposed destination

| Current capability | Proposed destination | Impact |
| --- | --- | --- |
| Window postprocess presets | `content` | Reuse pass-chain renderer; replace selection and define backdrop sampling. |
| Persistent ring plus referenced inward overlay | `border.outer` plus `border.inner` | Unify definitions and ownership; preserve existing ordering. Outer multi-pass and a new overlay output contract need renderer work. |
| Global open/close pair | Per-subject `open` and `close` | Reuse lifecycle machinery; replace global selection/cache assumptions with resolved per-subject programs and settings. |
| `windows_move` shader for move, resize, maximize, and reflow | Separate `move` and `resize` | New event classification, concurrent state, and timing rules. |
| Pointer wobble | Motion input/simulation for the selected move effect | Retain the spring simulation; a shader file alone cannot replace it. |
| Workspace/overview/scratchpad/focus shader events | Not specified | Preserve or deliberately retire each hook; they are not equivalent to a window moving. |
| Global layer map/unmap shader | Layer `open`/`close`, selected through rules | Existing fades/snapshots help; per-layer pipelines and persistent content effects are new integration. |
| Region → output → global chains | One `screen` scope | Draft loses independently composable stages, regions, and cursor placement controls. |
| Border pools and window cycle lists | Whole-effect choices | Reuse allocation ideas, but assignment, cycling, disabling, and lifetime semantics change. |
| Border emission/light spill | Not specified | Preserve its separate composition and capture policy, or explicitly remove it. |
| Built-in effects, curves, master toggles | Not fully specified | Decide the relationship between native animation settings and shader effects. |
| Source watching, diagnostics, reloads | Shared effect loading/validation | Foundations exist; GPU compilation and stricter failure handling need additional work. |
| `shader:*` actions | Unified effect controls | Change action parser, runtime selections, help, and shipped bindings. |
| Resolved inspection with provenance | New inspection command | New retained source/selection trace and IPC/CLI output. |

## 1. Configuration and resolution

The current configuration has distinct types for decoration shaders, postprocess
presets, pools, and animation events/pairs. Window rules select decoration/content
settings; output rules select output processing; layer rules presently expose blur
settings rather than effect selection.

Relevant implementation: [config types](src/config/config.h),
[shader parser](src/config/shaders.cpp),
[animation and rule parsers](src/config/config.cpp), and
[rule resolution](src/config/resolve.cpp).

The new model needs one effect library, a typed pipeline for each supported scope,
selectors on each applicable context, and a resolver that distinguishes:

- No assignment, meaning inherit.
- An explicitly empty list, clearing applicable inherited scopes.
- A disabled scope, removing just that inherited pipeline.
- Ordered effect application and ordered matching-rule application.
- Runtime overrides, including how `default` restores configuration.

Output-specific defaults for window/layer effects are a new dependency. Moving a
surface between outputs or changing its output's selection must re-resolve the
relevant scopes without rerolling valid choices or restarting active transitions.
The owning output must be defined for a surface spanning two outputs.

There is an additional include-file issue: [the current merger](src/config/config_merge.cpp)
recursively merges tables and appends nonempty arrays of tables. It therefore can
append `passes` when the same definition occurs in two included files, and preserve
settings that the draft says should be replaced. String selector arrays already
replace rather than append. We must define duplicate-definition behavior separately
from applying two named effects, then either reject duplicates or implement explicit
merge rules for effect definitions. Simply adding a new parser does not enforce the
draft's replacement semantics across includes.

Keep `Section`, TOML source regions, relative-path resolution, dependency watching,
and source-content equality. They already solve useful problems. Update
[change classification](src/config/change.cpp) so effect changes reach windows,
layers, outputs, and overview without reapplying display modes or layout unnecessarily.

## 2. Inner/outer borders: much of the visual behavior already exists

[View::refreshWindowShader](src/view/view.cpp) already orders the content filter,
inward ring overlay, and external border so a window shader cannot overwrite the
inward effect. [Overview cards](src/overview/overview.cpp) implement the same pieces.
[The pool harness](tests/harness/checks/207_shader_pools.sh) explicitly checks that
inward decoration remains over independently selected window processing.

The main improvement is expressing both halves together and selecting them through
one name. The layering guarantee does not require a new rendering invention.

However, the current inward program is a postprocess pass returning the completed
content-plus-decoration image. The initial draft described a separate transparent
decoration layer alpha-blended afterward. Those are different shader contracts.
Existing inward programs cannot simply be drawn as transparent overlays unchanged.
The revised proposal allows the existing postprocess contract to remain.

The native border also has two color bands called inner and outer; **both sit outside
the client content**. The proposed `border.inner` means decoration extending inside
the content boundary. Keep those geometries distinct, and preserve native border
widths, rounded contours, fractional-scale coverage, and input/layout bounds. See
[border rendering](docs/design/border-rendering.md).

The draft's leaf-by-leaf replacement rule has a pairing consequence: applying an
outer-only effect over an effect with both halves leaves the earlier inner half.
Today selecting an external-only ring removes its previous inward overlay. Decide
whether complete border selection replaces both halves, or require an explicit
`border.inner.enabled = false` in effects intended to replace the complete border.
The same choice affects cycling and runtime toggles.

Current borders are focus-gated, have urgent/fullscreen/visibility rules, and can
emit light into a separately stacked scene layer. Speed, freeze behavior, light
spread/intensity/threshold, and focus gating need explicit homes. They are compositor
behavior, not automatically covered by putting arbitrary values in shader `params`.

## 3. Shader programs and pass chains

There are currently three shader interfaces:

| Renderer interface | Program entry point | Important distinction |
| --- | --- | --- |
| Decoration | `ring_color(vec2)` | Logical coordinates, straight RGBA; host applies the client hole and premultiplication. |
| Animation | `animation(vec2)` | Normalized sampling, premultiplied RGBA, timeline/direction/seed, optional previous result. |
| Postprocess | `postprocess(vec3)` | Current/original/previous/buffer sampling, time, pointer, output/region information. |

See the public [animation](umbrielfx/include/umbrielfx/render/animation.h),
[decoration](umbrielfx/include/umbrielfx/render/decoration.h), and
[postprocess](umbrielfx/include/umbrielfx/render/postprocess.h) APIs, plus
[shader compilation](umbrielfx/render/fx_renderer/shaders.c).

One TOML format does not force one GLSL entry point or one internal renderer path.
Retaining useful scope-specific interfaces is possible without accepting old TOML.
It avoids making shader-porting a prerequisite for the configuration redesign.

Postprocessing already supports 1–16 passes. Decorations bind one program. Animation
nodes have ten ordered event slots, each holding one program and its own parameters
and history. Slots are event composition, not an arbitrary pass chain. Snapshot
copying also contains assumptions about specific slot numbers.

Multi-pass animation therefore needs a chain within an event, shared event timing,
per-pass state/history, and updated snapshot/reference ownership. Multi-pass outer
borders need a rendered border input/intermediate path that the current direct ring
draw does not provide. Reusing postprocess resources and helpers is plausible, but
putting `passes` in TOML alone does not provide either capability.

The draft's arbitrary `params` table and shader-supported-scope declarations also
need an authoring specification: types, defaults, reserved uniforms, validation,
binding, and reload behavior. Neither public API currently accepts a generic
parameter map. Likewise, palette/time are not currently supplied identically to all
three interfaces.

Automatically publishing the palette changes existing programs that use
`umbriel_palette_count > 0` as their opt-in. They currently preserve authored colors
when it is zero. A uniform interface can retain an explicit color-source preference;
removing the existing switch must not accidentally recolor those effects.

## 4. Move/resize, lifecycle, and geometry ownership

[View::syncAnimationShaders](src/view/view.cpp) chooses one movement timeline from
position, presentation size, or workspace layout motion. Opening/closing use their
own lifecycle clocks; [the scene adapter](src/scene/animation_shader.cpp) currently
selects custom programs globally by event. Native curves include easing and springs,
whose durations can be derived from the spring.

Per-window `open` and `close` are therefore more than a rename. The view and its close
snapshot need their own resolved pipeline, duration/curve, seed, and retained program
references. A global per-event cache cannot represent different active shader choices
for different windows. Compiled code can still be shared across instances.

Separate move and resize require classifying geometry changes and defining when
each event starts, retargets, settles, and ends. A corner resize changes position
as well as size; a layout reflow can do both. Without a deliberate rule the same
operation can produce unwanted double deformation. Layer geometry changes and
output changes need the same explicit treatment.

The proposed duration-as-settling-time model is new. Existing movement shaders see
eased progress through a geometry transition, including overshoot. Giving them zero
progress throughout motion and a settling ramp afterward changes their appearance
even if their GLSL compiles unchanged. Preserve geometry clocks separately from
visual-response clocks; do not use a shader's settling time to delay configure/ack
processing or a client's final size.

Pointer wobble is already a stateful 4×4 spring simulation in
[umbrielfx/render/wobble.c](umbrielfx/render/wobble.c), fed grab location, movement,
release, and ticks by the compositor. Selecting `wobble.glsl` does not manufacture
that state. The unified system needs to retain a built-in motion driver or provide
an equivalent explicit input contract. There is no reason to discard the simulation.

Current snapshots copy active shader state/history, including interrupted opening
effects. The initial proposal's close captured the displayed result and superseded
other event stages; a literal frozen-image implementation would change history and
snapshot behavior. The revised proposal requires visual continuity and reuse of
snapshot/history machinery without mandating a new frozen-image implementation.
Pipeline extensions still need dedicated lifetime and interruption verification.

The safest boundary is to retain native positioning, easing math, configure barriers,
resize crossfades, and snapshot lifetime management, and change their effect bindings.
See [animation design](docs/design/animation-shaders.md) and
[resize crossfades](src/view/resize_crossfade.cpp).

## 5. The draft does not cover every existing event

Existing custom shader hooks include workspace transitions, overview transitions,
scratchpad show/hide and its backdrop, focus-border color changes, and unfocused
dimming. [The event regression](tests/harness/checks/181_animation_shader_events.sh)
exercises these hooks rather than merely documenting theoretical support.

Workspace and overview effects process ancestor scene trees. Scratchpad shaders can
process separate dim/blur backdrop targets. These cannot all be expressed by applying
`move` or `open` independently to each window: both their input image and event
meaning differ.

For a complete replacement, map these events and targets into the new model, or
explicitly agree to retire individual shader hooks while retaining their native
behavior. Keeping their old shader configuration indefinitely would leave two
configuration systems and undermine the stated goal.

The initial draft made absent open/close pipelines complete immediately. The revised
proposal retains native fade/popin/zoom/slide behavior without a custom shader and
keeps native animation settings. Disabling a custom pipeline restores native behavior;
existing animation enable switches still govern whether the event animates at all.

## 6. Screen, cursor, regions, and capture

[Output::applyPostprocessConfig](src/output/output.cpp) currently builds:

```text
window processing → region chains → output chain → global chain
```

The global chain runs independently on each output, but it is still an additional
chain rather than merely the default value of the output chain. Furthermore,
[scene rendering](umbrielfx/types/scene/wlr_scene.c) can run it after software cursors
when `reads_cursor` is enabled; other output effects run before cursors.

Example: an output-specific temperature filter and a global cursor trail coexist
today. In the draft, both define `screen`, so the more specific selection replaces
the other. Putting both programs in one chain can recover some combinations, but
does not automatically recover independent controls, cursor insertion, different
regions, or each chain's original/history sampling semantics.

One public screen category is still possible, but composition must preserve these
distinctions or explicitly remove them. The claim that global versus output is only
a selection distinction was too strong.

Other behavior requiring a home:

- Output-local rectangular regions and their order.
- Cursor position sampling versus sampling the cursor image itself.
- Cursor-radius cropping, damage, pointer visibility, and per-output histories.
- Capture inclusion/exclusion for output and isolated toplevel captures.
- Redraw policy, shader-only frame caps, and the disabled fast path.

The proposed `content` input also needs precision. Existing window effects process
already-composited content and backdrop; isolated window captures intentionally omit
the desktop backdrop. Redefining content as only client buffers changes translucent
and glass effects. The existing
[liquid-glass example](docs/examples/shaders/window/liquid-glass.glsl) makes this
distinction practically relevant.

Persistent content/inward effects are currently disabled on live views during their
fade, whereas the draft promises them inside the decorated transition input. Making
that consistent across native/custom transitions, live windows, overview cards, and
close snapshots is a behavior change needing pixel tests.

Light spill is another separate layer: it has its own stacking, transform suppression,
and capture exclusions. It should not be accidentally baked into every surface
transition or isolated capture merely because it belongs to the same named effect.

## 7. Choices, runtime controls, and inspection

[ShaderPoolAllocator](src/config/shader_pool.h) already implements stable leases,
round-robin, and unused-first allocation with least-used reuse when exhausted.
Hidden mapped windows retain their assignment; close/unmap releases its reservation.
Window pools, in contrast, currently govern manual cycling rather than automatic
assignment at map time.

The draft introduces random selection and automatic whole-effect assignment, defers
an allocation policy that already exists, and retains a choice through closing.
Keeping the selected visual and retaining an allocation reservation are distinct
decisions. Define both, including removal/reordering on reload, switches to a
different pool, and reassignment when a rule changes.

Requiring identical candidate scope sets also affects existing pools mixing external-
only and paired borders. They need explicit disabled inner scopes, or a revised
replacement rule. Per-scope overrides over a chosen effect are valid under the draft,
but then “choose the entire look” is a default rather than an indivisible guarantee.

[Shader actions](src/server/actions.cpp) currently have separate animation, border,
window, output, and global selection paths. A unified action needs defined target
selection, optional individual-scope control, list semantics, toggle/default behavior,
and rule/reload precedence. Shipped keybinds and the action parser/help must change
with it. The draft has not yet specified this public interface.

Inspection is worthwhile, but it needs data the current resolved structs do not
retain: assignment origins, losing definitions, selected choices, and active versus
next-event pipelines. Preserve that information during resolution instead of trying
to reconstruct it from shader filenames after the fact. This is additional IPC/CLI
work, not just printing an existing unified resolved object.

## 8. Reload and failure semantics

TOML reload is already transactional for error-level configuration failures.
However, many current shader read/validation problems produce warnings, and GPU
compilation happens in scene preparation after configuration loading. Broken custom
programs fall back to ordinary rendering or native animations; existing tests depend
on that behavior.

The draft promises stricter validation and retention of the previous configuration.
If that guarantee includes GLSL compilation, prepare the candidate GPU resources
before committing the effect generation, and propagate compiler errors back to the
reload decision. Preserve source watches on the failed candidate so fixing a shader
can recover. Also decide whether one broken unused library effect rejects the
whole reload or only a selected effect must compile.

The standalone `umbriel validate` path loads configuration without creating a GPU
renderer. It can check schema, paths, names, and metadata, but cannot promise that
the user's GPU accepts the GLSL without an additional renderer-backed validation
mode. Make that distinction visible.

Existing per-node references already help preserve in-flight programs. Extend that
ownership to pipeline chains, parameters, and selected effect versions. Renderer
recovery also explicitly recompiles all three current caches in
[Server::recreateRenderer](src/server/server_events.cpp); the new cache must retain
that recovery behavior.

## 9. Performance and renderer invariants

The configuration naming change has no necessary per-frame GPU cost. Extra passes,
new captures, histories, and longer settling do. Do not implement a unified public
model by forcing every ordinary border through a full-window offscreen texture.

Retain these existing fast paths and guarantees:

- Plain borders when no custom border program applies.
- Analytic shadows when the effect preserves shape; shader silhouettes otherwise.
- Demand-driven redraw and existing shader-only FPS limits.
- Lazy histories and resource release when effects stop.
- Shared compiled programs with separate state for each target/output/pass.
- Clean capture behavior, lock suspension, output hotplug, and renderer recovery.
- Existing direct-scanout/culling rules while sampling effects are active.
- Correct fractional scaling, output rotation, clipping, and working color format.

For scale only: one 3840×2160 RGBA8 texture is about 31.6 MiB; FP16 RGBA doubles
that. Current postprocess chains retain source/result pairs and optional buffer-pass
pairs. Multiplying full-screen passes and histories has a real memory/bandwidth cost,
even when the configuration is short. This is not a measured cost of the proposal.

The implementation remains in `umbrielfx` for visual work, with the compositor
owning policy and lifecycle. Keep wlroots-compatible struct prefixes intact; the
existing addon approach can carry new effect state without violating that ABI.
No new external dependency is intrinsically required by this design.

## 10. Assets, examples, and packaging

Inventory of files in the reviewed working tree:

| Location | GLSL files | Notes |
| --- | ---: | --- |
| `examples/shaders/barrulus` | 65 | 10 animation, 6 ring, 33 window, 4 screen, 12 cursor files; installed collection. |
| `docs/examples/shaders` | 57 | 2 animation, 12 ring, 40 window, 3 bleed files; includes additional examples and overlapping assets. |

These are file counts, not 122 distinct effects. The installed collection also has
59 TOML files. The docs example library defines 11 borders and 27 postprocess
presets, with border allocation and a 16-entry window favourites list.

All relevant TOML definitions, includes, selectors, and keybinds must be converted
as maintained project assets. No end-user conversion tool is required for this.
Retaining scoped GLSL interfaces means many programs can stay byte-for-byte intact;
a common shader entry point or different alpha/coordinate contracts would require
porting them. Either route needs visual verification, not just compilation.

Four generator scripts under `docs/examples/shaders` produce related inward overlays.
For example, [the vine generator](docs/examples/shaders/generate-vine-overlay.py)
adapts a ring program into a postprocess program and hardcodes border geometry.
A common border input contract could let inner and outer use the same source and
remove such duplication. That benefit requires shader-host work; grouping TOML
alone does not deliver it.

[Meson](meson.build) installs the collection directory, and renderer tests locate
those assets. [Home Manager](nix/home-module.nix) and [Hjem](nix/hjem-module.nix)
accept generic TOML settings, so their generation machinery need not be redesigned.
Their users' shader settings and documented examples still change. Nix packaging
and the compositor's Wayland protocols do not require a redesign for this work.

## 11. Validation required for implementation

No runtime tests were run for this assessment: there is no code change to validate.
The repository already has substantial relevant coverage to preserve and adapt.
A text inventory found shader references in 39 harness scripts; this is a review
aid, not a claim that exactly 39 tests are sufficient or all must be rewritten.

Implementation should include:

1. Parser/resolver tests for scope replacement, explicit clearing, include behavior,
   applicability, provenance, and output/rule/runtime precedence.
2. Collection compilation and pixel checks for all retained shader interfaces and
   their converted definitions.
3. Inner/outer ordering over an opaque content shader, native border geometry,
   fractional scaling, palette, light emission, and transparency.
4. Concurrent move/resize, retargeting, grab/release wobble, opening during reflow,
   close during opening/movement, configure barriers, and late client commits.
5. Snapshot, feedback, history, and resource-lifetime checks, including mid-event
   reload and renderer recovery.
6. Overview, scratchpad, workspace, and layer behavior for every retained hook.
7. Screen/region/cursor order, multiple outputs, output and isolated window captures,
   lock/unlock, and cursor-history reset.
8. Pool stability, exhaustion, scope clearing, cycling, and chosen-effect lifetime.
9. Idle/static redraw and resource release; real-GPU performance/color validation
   where headless tests cannot establish those properties.

Use the existing deterministic animation clock and pixel-probe harness. Follow
`CONTRIBUTING.md` for targeted checks, stress checks for changed harness tests,
and the eventual complete unit/renderer/harness verification. Do not replace
existing regression assertions with weaker tests that merely accept the new syntax.

## Recommended implementation boundary

Keep the unified effect library and consistent scope/selection vocabulary. Reuse
the existing renderer, geometry, simulation, and resource management wherever they
already implement the intended behavior.

Before coding, finish a coverage matrix for the omitted events, native animation
defaults, cursor/screen composition, captures, illumination, focus gating, and pool
semantics. These are specification decisions, not reasons to abandon the simpler UX.

Then implement in independently reviewable stages:

1. Define the complete effect model and resolve it in pure configuration code,
   including include-file semantics and provenance.
2. Bind persistent effects and per-subject lifecycle effects through the existing
   renderer paths, converting the project examples and controls.
3. Extend event and border paths to support the promised multi-pass/state semantics;
   preserve the existing motion simulation and geometry clocks.
4. Complete screen/cursor/capture integration, reload transactions, inspection, and
   the regression/performance checks.

Stages are an implementation sequence, not a recommendation to ship an incomplete
schema or retain compatibility aliases. A wholesale common GLSL interface can be
deferred without preventing the unified TOML model; it should not be mistaken for
a prerequisite. Configuration migration tooling remains outside this work unless
subsequently requested.
