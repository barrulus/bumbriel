# One effect, named once

Status: authoritative implementation specification, including the pointer-drag
extension and subsequent naming decisions. Implemented on `feat/shader-system`,
with automated regression and GPU verification completed on 25 September 2026;
the working-tree changes await review.
This document supersedes the earlier drafts. It incorporates the external
`effects-schema-review.md` and the [impact assessment](shader-system-impact.md).
Source comparisons use fork `a258053` and its recorded upstream base `a607fdf`.

## The user model

> Define an effect, configure its shader or drag-physics scopes, and select it by name.

There are two everyday operations:

- Define an effect under `[effects.NAME.SCOPE]`.
- Apply it with `effects = ["NAME"]` in appearance settings or a rule.

“Shader” means a program. “Effect” means the named configuration you select. A
border, its inward decoration, and its opening/closing shaders can all belong to
one effect. There are no separate preset, ring, overlay-preset, or animation-pair
libraries. Additional scopes and policy settings below are reference material;
they are not prerequisites for configuring a border.

```toml
[appearance]
effects = ["neon"]

[effects.neon.border.outer]
padding = 24
passes = [{ shader = "shaders/neon-border.glsl" }]

[effects.neon.border.inner]
passes = [{ shader = "shaders/neon-inner.glsl" }]

[effects.neon.open]
duration_ms = 180
curve = "easeout"
passes = [{ shader = "shaders/neon-open.glsl" }]

[effects.neon.close]
duration_ms = 140
curve = "easeout"
passes = [{ shader = "shaders/neon-close.glsl" }]
```

Define only the scopes you need. A border-only effect needs just its border table.
Paths in examples assume the referenced shader files are installed or copied beside
that configuration; paths are not built-in effect names.

## 1. Ownership and the upstream boundary

`[animation]` survives. It owns native animation behavior, enable switches, default
curves and durations, native styles/scales, and the named Bézier/spring registries.
The native pointer simulation opt-in is named `windows_move.drag_physics`, replacing
the old `wobble` spelling without an alias. It enables native Jelly independently of
custom movement shaders. Selecting a named `drag` preset enables that simulation
without this opt-in; section 14 defines its parameters and gates.

`[effects]` becomes the sole owner of custom shader and drag-preset definitions. Appearance, output,
window, layer, and region settings select effects. There are no shader selections
left under `[animation]`.

| Existing animation configuration | Decision |
| --- | --- |
| `[animation] enabled`, `duration_ms`, `curve` | Retain as native behavior/defaults. |
| Native event `enabled`, `duration_ms`, `curve`, `style`, `scale` | Retain, including their existing defaults. |
| `[animation.beziers]`, `[animation.springs]` | Retain; effect lifecycle curves can reference these names. |
| `windows_move.wobble`, temporary `wobble_style` | Replace with native `drag_physics` opt-in and named `drag` presets; no aliases. |
| Overview `workspace_curve`; scratchpad dim/blur/scale/maximize/fullscreen; unfocused dim | Retain native settings and behavior. |
| `animation.<event>.shader` | Remove; map every event to an effect scope as specified below. |
| `[animation.pair.NAME]` and `[animation] preset` | Remove; named effects hold open/close pipelines. |
| `shader:animation` runtime selection | Replace with the uniform effect action. |

Ordinary borders, shadows, blur, layout, focus, workspaces, input, native animation
math, and configure/ack synchronization retain their behavior. No selected custom
effect means native presentation. Disabling a custom pipeline restores native
behavior; disabling the native event prevents its custom event pipeline too.

The integration work in existing upstream code is effect lookup at event hooks,
resolved state on surfaces and snapshots, rules/reload dependencies, and rendering
connections. The schema does not require a universal GLSL interface or a rewrite of
working rendering paths. Visual work remains in `umbrielfx`.

No compatibility aliases or legacy config reader are introduced. Project examples
and documentation change with the implementation. End-user conversion tooling is
out of this release's scope unless requested separately.

## 2. Complete scope inventory

A shader scope defines its input image and rendering/event location, using the
same ordered `passes` format. The window-only `drag` scope instead configures the
existing CPU simulation; it accepts coefficients, not shader passes or a timeline.

| Scope | Owner and input | Native event gate/timing, when applicable |
| --- | --- | --- |
| `content` | Window or layer content, before shader decoration | Persistent |
| `border.inner` | Processed window content; inward decoration clipped to its rounded content box | Persistent |
| `border.outer` | Native external window border and its drawing allowance | Persistent |
| `border.focus` | External border subtree during its focus-color transition | `animation.border` |
| `open` | Decorated window or layer appearing | Window: `windows_in`; layer: `layers` |
| `close` | Retained decorated window or layer disappearing | Window: `windows_out`; layer: `layers` |
| `move` | Decorated window during native position animation | `windows_move` |
| `resize` | Decorated window during native size animation | `windows_move` |
| `drag` | Window deformation driven by pointer movement and the existing spring simulation | Native animation master and `windows_move.enabled`; no timeline duration |
| `focus` | Decorated window during unfocused-opacity transition | `dim_unfocused` |
| `scratchpad` | Decorated window during scratchpad show/hide, instead of its ordinary open/close | `scratchpad` |
| `backdrop` | Each existing scratchpad dim/blur backdrop target on an output | `scratchpad` |
| `workspace` | Output workspace-view root during workspace switching | `workspaces` |
| `overview` | Output overview tree during its existing transitions and settling | `overview` |
| `screen` | Composed output image, or a declared output-local region | Persistent |
| `overlay` | Final output processing stage; includes cursor effects | Persistent |

`border` is a grouping table containing only `inner`, `outer`, and `focus`.
It accepts no settings. For example, `effects.neon.border.padding` is an error:
“border is a group; put padding under border.outer”.

Window scopes are content, all border leaves, open, close, move, resize, drag, focus, and
scratchpad. Layer scopes are content, open, and close. Output-owned scopes are
backdrop, workspace, overview, screen, and overlay. A region accepts screen only.
An output assignment can also provide defaults for its windows and layers.
Border scopes on an undecorated/fullscreen window are dormant, not an applicability
error. Layers do not gain native borders or move/resize animation from this change.

`overlay` replaces the old global *rendering stage*, not its selection syntax.
Selecting it globally or on one output still uses the same effect definition.
It remains separate from `screen` so a screen filter and a cursor trail coexist.
There is no cross-monitor image sampling.

## 3. Definition grammar and settings

`[effects.NAME]` is exactly one of:

1. A concrete effect containing one or more of the scope tables above.
2. A choice containing `choose` and optional `selection`, described in section 9.

The presence of `choose` discriminates the second form. Mixing forms is an error;
no `type` field or second library is needed. Names are case-sensitive, nonempty
strings without whitespace or commas. Quoted TOML keys can contain dots. Names
have no reserved `off` or builtin meaning.

An enabled shader leaf has 1–16 passes. An enabled `drag` leaf uses section 14's
coefficients or explicit `enabled = true`. A disabled leaf is exactly `enabled = false`;
other settings or passes alongside it are errors. Omitted `enabled` means true.
An empty leaf is invalid. Each supplied pass must be valid: no partial chains.

| Leaf setting | Applies to | Type/default |
| --- | --- | --- |
| `enabled` | Every leaf | Boolean, true |
| `passes` | Every enabled shader leaf | Array of 1–16 pass tables |
| `palette` | Every enabled shader leaf | Boolean, false |
| `animated` | Persistent leaves | Boolean, true |
| `speed` | Persistent leaves | Finite number 0–10, 1.0 |
| `padding` | `border.outer` | Integer logical pixels 0–1024, 0 |
| `focused_only` | `border.inner`, `border.outer` | Boolean, true |
| `light` | `border.outer` | Table described below; disabled by default |
| `duration_ms` | `open`, `close` | Integer 1–10000; inherit native event when omitted |
| `curve` | `open`, `close` | Existing curve syntax/name; inherit native event when omitted |
| `cursor_radius` | `overlay` | Integer logical pixels 0–4096; 0 means full output |

Persistent leaves are content, border.inner, border.outer, screen, and overlay.
Settings on unsupported scopes are errors, rather than ignored values. Non-lifecycle
event timing belongs to its native `[animation.<event>]` settings, not to an effect
leaf. In particular, version 1 does not accept move/resize settling durations.

`palette = true` publishes the existing four configured chromatic colors through
the palette helpers; false publishes a count of zero. Keep the existing fallback
colors of each shader interface. This retains authored-color opt-out and existing
palette-aware shader behavior. A shared helper does not mean mandatory recoloring.

`animated = false` or `speed = 0` fixes that pipeline's shader time at zero and
prevents it from requesting idle frames. Otherwise time advances at `speed` times
elapsed seconds. Client/native damage still evaluates the pipeline; feedback, if
used, advances on successfully submitted evaluations even when time is frozen.
Freeze is a time/scheduling control, not a cached screenshot of changing content.
A single scope instance supplies the same time to all its passes. Persistent scopes
read the existing compositor shader clock, so paired halves with equal speed stay
aligned; focus/visibility changes do not restart their time.

`focused_only = true` preserves current active-border gating for both halves,
including urgent/hidden/undecorated suppression. False permits the custom shader
on unfocused or urgent decorated windows; native fullscreen/decoration visibility
still wins. Selection and allocation persist while a border is dormant.

`border.outer.light` accepts `enabled` (false), `spread` (1–256 logical pixels,
default 80), `intensity` (0–4, default 1), and `threshold` (0–1, default 0.5).
These are compositor illumination controls, not GLSL parameters. Emission derives
from the final outer-border pipeline result. Preserve existing diffusion, separate
stacking below panels/overlays, transform suppression, and capture exclusions.

### Pass tables and shader interfaces

A pass contains exactly one of `shader` or `builtin`, optional `params`, and optional
`buffer = false` where supported.

```toml
[effects.reading.screen]
passes = [
  { builtin = "temperature", params = { kelvin = 4000 } },
  { builtin = "saturation", params = { amount = 0.7 } },
  { shader = "shaders/grain.glsl", params = { strength = 0.04 } },
]
```

`shader` is always a file path, including a path with no extension. `builtin` is
always one of grayscale, invert, saturation, or temperature. Grayscale/invert take
no parameters; saturation takes `amount` 0–10, default 1.5; temperature takes integer
`kelvin` 1000–40000, default 4000. Preserve their current color formulas, including
neutral temperature at 6500 K. Builtins work in postprocess scopes and in outer-border
passes after the first; event passes use animation shaders.

Retain these source contracts in version 1:

- Content, inner border, screen, overlay, and region screen passes use
  `postprocess(vec3)`, returning the complete premultiplied processed image.
- Every event pass uses `animation(vec2)`, returning premultiplied RGBA.
- The first outer-border pass uses `ring_color(vec2)` with the existing logical
  geometry and straight-RGBA contract. The host applies coverage/premultiplication.
  Subsequent outer-border passes use postprocess on that border image. Clip final
  output to the external region and padding; filtering must not fill the client hole.

Passes use previous-pass output as their current input. Retain postprocess original
source and previous-frame helpers; original source means the input to that *scope
instance*, not the original desktop before other stages. Animation previous-result
history is separate for each pass, target, and output. Border postprocess coordinates
are local to its padded raster with consistent scale and region metadata.

`buffer = true` requests the existing additional `postprocess_buffer` program and
feedback buffers. It is accepted only on file-backed postprocess passes, including
later outer-border passes. It is an error on builtins, animation passes, or the first
outer-border pass. Animation feedback continues to use `umbriel_sample_previous`.

`params` is a flat map to user-declared GLSL uniforms. It accepts booleans, signed
32-bit integers, finite float-representable numbers, and numeric vectors of length
2–4. Bind against the linked uniform's type: floats accept numeric values, integer
uniforms require integers, and vector dimensions/types must match. No strings,
textures, nested tables, or arbitrary arrays are supported. Host uniforms and names
beginning `umbriel_`, `ring_`, or `effect_` are reserved. Unknown/inactive uniforms and
type mismatches are errors; shaders without supplied params need no changes.
Builtin params are validated against their declared keys instead.

Sources retain the current regular-file, nonblank, NUL-free, 256 KiB limit and GLSL
ES 1.00 host contract. Relative paths resolve from the file declaring that pass.
Source files remain watched, including paths missing in a failed reload. A source
must compile against its scope's contract; no new sidecar manifest or automatic
conversion between shader interfaces is introduced.

## 4. Selection, precedence, and disabling

```toml
[appearance]
effects = ["neon", "film", "comet"]

[output."DP-1"]
effects = ["warm-screen"]

[[window_rule]]
match.app_id = "^terminal$"
effects = ["quiet"]

[[layer_rule]]
match.namespace = "^notifications$"
effects = ["gentle"]
```

Every selector is a list of effect names. Resolve references after all files load,
so forward references are valid. Apply names left to right. A later definition
replaces an earlier definition of the same leaf scope in full; omitted scopes leave
previous choices alone. Pass lists and settings do not merge between effects.

Resolve each owner's scopes in this order:

1. Appearance defaults, then runtime global-default overrides.
2. Its output's defaults, then runtime output overrides.
3. Matching window/layer rules in merged declaration order, then that subject's
   runtime overrides. Output-owned scopes stop at step 2.

The surface's compositor-assigned output supplies defaults, including when it spans
monitors. Moving outputs re-resolves defaults without changing a still-valid chosen
variant or restarting active event clocks. A selected definition containing scopes
for multiple owner types contributes only applicable scopes. Each selected name must
have at least one applicable leaf for its assignment context; otherwise report an
error. A window rule cannot alter the output's screen processing or scratchpad backdrop.

Omitting `effects` inherits. `effects = []` clears all applicable inherited custom
scopes at that level; later, more specific assignments can add effects again. It
does not disable native animations, blur, ordinary borders, or shadows.

To remove one inherited scope, select a concrete effect containing that disabled leaf:

```toml
[effects.no-content.content]
enabled = false

[[window_rule]]
match.app_id = "^game$"
effects = ["no-content"]
```

Border leaves replace independently. To replace a complete paired border with an
external-only border, the new effect must explicitly contain
`border.inner.enabled = false`. Shipped external-only border choices must do this.
This prevents a previous inward half surviving unintentionally while keeping the
same replacement rule for every leaf.

### Includes

Use the existing `[include] files = [...]` mechanism for effect files. There is no
separate `include.effects` key. Includes, merging, and path resolution are application
behavior defined by this specification and the existing config loader, not features
provided by TOML itself.

For example, `config.toml` can load an effect definition and select it:

```toml
[include]
files = ["effects/neon.toml"]

[appearance]
effects = ["neon"]
```

The included `effects/neon.toml` defines the complete named effect:

```toml
[effects.neon.border.outer]
padding = 24
passes = [{ shader = "../shaders/neon-border.glsl" }]

[effects.neon.open]
duration_ms = 180
curve = "easeout"
passes = [{ shader = "../shaders/neon-open.glsl" }]
```

Including a definition makes it available; an `effects = [...]` selector applies it.
Reusable definition files should therefore omit selectors such as
`[appearance] effects = [...]`, which would also participate in normal config merging.
A file may define one or several effects. A collection file may itself include
individual effect files using the same mechanism.

Relative include paths resolve from the including file. Relative shader paths resolve
from the file declaring the pass, so both shader paths above refer to the `shaders/`
directory beside `config.toml`. `files = []` adds no includes from that file; effects
defined in the main config remain available.

An effect name has exactly one defining source file. Different headers for its leaves
within that file are normal TOML; any declaration of the same effect name in another
included file is an error with both locations. Definitions never acquire extra passes
or partial settings through cross-file deep merging. This check happens before the
generic merge loses declaration ownership. Users select another named effect to
customize an inherited scope, or edit the owning definition.

Selector string arrays retain existing replacement behavior across includes; rule
arrays retain their existing ordered accumulation. No unrelated include/merge rule
changes. Native animation defaults and curve registries use existing merge behavior.

## 5. Animation timing and motion

The native master switch and corresponding event switch gate every custom event
pipeline. A disabled pipeline or absent selection uses native behavior. A selected
custom open/close pipeline replaces the native lifecycle visual rather than adding
native popin/slide/scale a second time; native style/scale remain the fallback.
One-sided custom lifecycle effects are valid: an omitted close inherits an earlier
custom close or uses native close. A pair is not mandatory.

For selected open/close pipelines, resolve `curve` and `duration_ms` independently
from the leaf, falling back to that target's native event settings. Curve names use
`animation.beziers`/`animation.springs`. Spring curves derive their own duration as
they do today. An explicitly supplied effect duration with a resolved spring curve
is an error explaining that the duration is unused; choose a duration-based curve
or omit the duration. There are no hidden 200/400/500 ms effect defaults.

All passes in an event share its linear/eased progress, direction, transition ID,
and stable random seed. Linear progress runs 0–1; eased progress retains overshoot.
Open and close are separate definitions and are not automatically reversed. Explicit
lifecycle timing overrides affect that lifecycle only, not surviving windows' layout
motion or client configuration delivery.

Other event pipelines observe their existing native event clocks, curves, enablement,
and retargeting. They cannot change native duration/curve from inside an effect.
Scratchpad window/backdrop scopes share the corresponding existing native fade;
backdrop selection is output-owned, never inferred from an arbitrary member window.
Layer open/close use the native layer timeline and preserve partial-alpha behavior.

### Move and resize in version 1

Both scopes use the existing native geometry animation, classified from its start
and target presentation boxes: changed origin activates move, changed dimensions
activate resize, and both may run together. They observe the same movement timeline
the existing view would select for its `windows_move` shader, including layout-owned
motion. Order is resize, then move. When both resolve to identical pass sources,
params, and settings, evaluate that pipeline only once with one history set. This
preserves the old combined movement shader when its source is assigned to both
leaves; different pipelines still compose. Events end with their native timeline;
there is no added settling phase.
A retarget uses existing native transition identity/retarget behavior rather than
creating another animation clock. A fresh tiled opener is not a move from `(0, 0)`.

Immediate geometry changes without a native animation do not manufacture a timed
shader transition. Interactive drag physics continues through the existing simulation,
including its grab constraint and settling after release. Its built-in presentation
composes with custom movement/lifecycle processing as it does today. Interactive
resize remains native; a resize shader runs when a native size animation exists.

This is an explicit boundary: arbitrary velocity-driven shaders during direct drag
or resize, and a new shader-controlled settling duration, are not version-1 features.
They may extend this schema later without replacing native geometry clocks. The
previous draft's mandatory `active`/velocity inputs and duration-as-settling model
are withdrawn, not silently promised by the move/resize scope names.

### Lifetimes and interruption

Resolve an event's pipeline and timing at its start. Retain program references,
params, and shader state until it completes; ordinary reloads or selection changes
affect subsequent events. Persistent pipelines update on successful reload. Palette
colors update persistent instances live; event instances and close snapshots retain
their starting/captured values.

Close snapshots retain the decorated image, selected close pipeline, and any copied
in-flight shader state required by existing snapshot composition. Freeze copied
opening/motion parameters; do not restart those events. Run close around that
retained presentation. Preserve per-pass histories, stable seeds, clipping, and
reference lifetimes without mandating a new flattened screenshot implementation.
Overview copies use the source window's resolved selections and event state, not a
fresh choice or replayed open. Native geometry/configure barriers remain authoritative.

Explicit runtime off, a disabled native event, or the subsystem master switch may
cancel an active custom pipeline immediately and clear its history. Its existing
lifecycle owner must still finish safely; no orphaned close snapshots or delayed
client configures are permitted. A deliberate off may visibly change presentation;
it is distinct from the retain-until-completion rule for ordinary edits.

## 6. Rendering and border contracts

The surface pipeline is:

```text
content processing → inward decoration → external border (including border.focus)
    → focus processing → resize → move → lifecycle or scratchpad processing
    → existing interactive drag physics presentation
    → existing ancestor workspace/overview processing
```

This is logical composition order, not a requirement to allocate a framebuffer for
every box. Border focus processes the external border subtree, not the application's
content. The existing ordering of independently animated ancestor trees is retained.

An opaque content shader cannot erase inward decoration because that decoration runs
after it. Inward passes retain their complete-image postprocess contract; outer
passes retain the external client-hole mask. Shader border “inner” means inside the
content boundary, whereas the native double-band border's two colors remain outside
that boundary. Native border geometry and fractional-scale coverage do not change.

Content processing retains the current composed-content/backdrop input. Isolated
window capture uses its existing isolated source, without other applications behind
it. Do not redefine content as only raw client buffers and accidentally change glass
and translucent effects. Persistent content/inward pipelines must be represented
inside custom lifecycle captures; their appearance must not disappear merely because
a custom lifecycle event started. This is required shader composition work, not a
change to native geometry or unshaded animation.

Reuse analytic shadows for shape-preserving effects and existing silhouette shadows
otherwise. Preserve scene stacking, rounded clipping, output transforms, working
color formats, and transparent padding. Light spill remains a separately stacked
output-visible layer, suppressed during the existing transform/fade cases, excluded
from close snapshots and isolated window captures, and included in output captures.

## 7. Subsystem controls, screens, cursors, and regions

Subsystem policy has a separate, optional table; `[effects]` contains only definitions:

```toml
[render.effects]
enabled = true
redraw = "auto"
in_capture = false
reads_cursor = false
fps = 0
```

| Setting | Meaning |
| --- | --- |
| `enabled` | Master gate for custom shader pipelines and named drag presets. Native effects and the native Jelly fallback retain their native gates. |
| `redraw` | `auto`, `on_damage`, or `continuous`; controls persistent output/region/overlay idle redraw as described below. |
| `in_capture` | Include persistent content, inward, region, screen, and overlay processing in protocol captures; default false. |
| `reads_cursor` | Run the final overlay after the software cursor so it can sample the cursor image; default false. |
| `fps` | Shader-only idle redraw cap, integer 0–240; 0 follows output refresh. Native animations/client damage retain their cadence. |

`auto` schedules output/region/overlay redraw when visible compiled programs use
time, pointer input as it changes, or feedback. `on_damage` suppresses their idle
redraw; client/native/pointer damage can still render them. `continuous` schedules
visible screen/overlay processing even if static. Pipeline `animated = false` or
zero speed prevents that pipeline from being an idle-frame reason under any policy.
Visible content/inner/outer pipelines retain their own activity detection; `fps`
caps all shader-only idle requests. Event timelines are governed by native animation
scheduling. Dormant effects and hidden outputs do not create idle work.

Default output order is:

```text
scene → declared region chains → screen → overlay → cursor
```

With `reads_cursor = true`:

```text
scene → declared region chains → screen → software cursor → overlay
```

Reading pointer position does not itself require reading the cursor image.
`reads_cursor` controls placement of overlay only, and forces a software cursor only
while an eligible overlay is active. Screen filters remain before the cursor. Each
output has independent histories. `cursor_radius > 0` crops overlay processing to
the pointer's footprint; zero uses the full output. Preserve damage of old/new
footprints and history reset when the pointer hides, leaves the output, or the
session locks. Display transforms, working format, or program changes reset history.

A screen filter and overlay selected by different names do not replace each other:

```toml
[appearance]
effects = ["warm-screen", "comet"]

[effects.warm-screen.screen]
passes = [{ builtin = "temperature", params = { kelvin = 4000 } }]

[effects.comet.overlay]
passes = [
  { shader = "shaders/barrulus/cursor/comet-0.glsl" },
  { shader = "shaders/barrulus/cursor/comet-1.glsl" },
]
```

Use `[[effect_region]]` for output-local rectangles. Each entry requires a unique
`name`, positive `width`/`height` (1–100000), and `effects`. `x` and `y` default to 0
(range -100000–100000); `output` is an optional existing output identity selector.
Omitting output creates a separate region instance on every output. Regions clip
to output bounds, do not affect input, and execute in merged declaration order.

```toml
[[effect_region]]
name = "reading-area"
output = "DP-1"
x = 20
y = 40
width = 800
height = 600
effects = ["reading"]
```

Regions do not inherit appearance/output screen assignments: those already run later
on the entire output. Resolve their explicit names from an empty screen scope;
`effects = []` disables that region. Other scopes in a selected concrete effect are
ignored as for other contexts. Separate overlapping regions remain separate chains;
each sees the previously composited result. Choice definitions for regions must
contain screen scopes only.

Capture policy preserves the existing distinction: external borders/native visuals
remain scene decorations in output capture; isolated window capture excludes external
decorations, shadows, and light spill. `in_capture` gates the persistent sampling
pipelines named above, not native lifecycle rendering. Capture and shadow-only
rendering do not advance an effect's history a second time. Session locking suspends
custom processing and clears cursor histories; the lock surface is not shaded with
private desktop content.

## 8. Reload, validation, and shader failures

Validate every definition, choice reference, scope, setting, builtin parameter,
selector, and region before applying a configuration. Unknown keys, removed shader
keys, empty/mixed definitions, unsupported settings, and missing shader sources are
errors with source locations. Errors do not silently select another program.

Prepare all enabled concrete pipelines before committing an enabled effect library,
including currently unselected definitions, so a runtime selection cannot discover
an unchecked shader. Cache identical programs and their failures per renderer/source/
contract; do not compile in rendering callbacks. Shader uniform/type validation and
GPU compilation failures reject that candidate generation. The previous configuration
and effect generation remain active on failed reload; watches track the failed
candidate so fixing a file recovers. This deliberately replaces the fork's current
“broken program drops to ordinary rendering” reload behavior.

When `render.effects.enabled = false`, still validate schema, references, parameter
shapes, and source-file readability, but defer GPU preparation. Enabling later must
prepare and validate the complete library first; failure leaves custom effects off.
At first startup there is no previous generation: an invalid effect configuration
reports diagnostics and uses the existing native/default configuration recovery path.
Renderer loss or allocation failure likewise preserves ordinary rendering rather
than blocking the desktop; these runtime failures are not renamed-key fallbacks.

`umbriel validate` performs configuration/source checks without a GPU and explicitly
reports that GLSL compilation/uniform binding were not checked. Runtime load performs
those checks with the actual renderer. Warnings retain their existing CLI behavior;
no claim of GPU validation is made by a plain offline success.

Renderer recovery rebuilds programs for the new renderer, rebinds resolved selections,
and resets invalid GPU histories. Effect changes must trigger only dependent runtime
refreshes; identical reloads are inert and shader edits do not reapply output modes.

## 9. Choices and allocation

A choice is another named definition in the same library:

```toml
[effects.terminals]
choose = ["neon", "ember", "ice"]
selection = "unused_first"
```

`choose` is a nonempty list of unique concrete effect names; nesting choices, cycles,
and mixing `choose` with scope tables are errors. Candidates must define the same
set of leaves, counting explicit disabled leaves. This ensures changing candidates
clears the previous variant's complete contribution. External-only candidates in a
paired-border choice therefore include a disabled inner leaf.

Candidates belong to one lifetime family: window scopes, layer-compatible scopes,
or output-owned scopes. A surface/output mixture is invalid. A layer choice must
contain only layer-compatible scopes. Applicability is validated at selection time.
Concrete non-choice effects may freely bundle scopes across families.

`selection` is one of:

| Value | Assignment behavior |
| --- | --- |
| `unused_first` (default) | Choose an unused candidate; if all are used, choose a least-used candidate. Tie order starts after the previous allocation. |
| `round_robin` | Assign successive candidates in list order, wrapping and allowing duplicates. |
| `random` | Uniformly select a candidate; duplicates across subjects are allowed. |

Allocation is per choice name and owner family, not per focus change. A mapped
subject receives a selection when that choice first contributes to its resolution.
Hidden or temporarily disabled subjects retain it. Definition reorder and source/
parameter edits retain a still-valid candidate by name. Removing a candidate
reallocates affected owners; references to a removed choice are configuration errors.
When a choice no longer contributes, release its lease. Retain a dormant lease for
an explicit off/toggle so on does not reroll it.

Unmap releases the allocation reservation, while a close snapshot retains its selected
concrete effect and program references through completion. Visual lifetime and pool
reservation are intentionally separate. Output/region instances release on removal.
Normal movement between outputs keeps a still-selected choice/candidate; changing to
a different choice selects from that choice. Allocate deterministically in existing
subject order when a reload introduces a choice to multiple subjects.

Explicit cycling excludes the current candidate when there is more than one.
`unused_first` keeps its least-used policy; `round_robin` takes the next listed
candidate; `random` chooses uniformly among the remaining candidates. A one-member
choice is unchanged. Start from the owner's currently effective candidate when it
belongs to that choice, including one selected directly or by a rule. Otherwise use
the policy's initial assignment behavior. Neither assignment nor cycling restarts
an in-flight event.

Window favourites do not have to auto-assign effects: define a choice without placing
it in a config selector, then use it only from a cycle binding. This preserves the
current manual window-cycle use case without a separate `window_pool` setting.
Use `selection = "round_robin"` to preserve its listed-order cycling; unused-first
is an intentional different choice when avoiding duplicates is desired.

## 10. Runtime actions and inspection

Use one action, `effect`, with target kinds `global`, `output`, `window`, `layer`,
`region`, or `system`. The existing action form permits `effect:window ...` in a
keybind or `umbriel msg`. Grammar:

```text
effect:<kind> set <name> [<name> ...] [--target <id>] [--scope <leaf>[,<leaf>...]]
effect:<kind> cycle <choice> [--target <id>] [--scope <leaf>[,<leaf>...]]
effect:<kind> off|on|toggle|default [--target <id>] [--scope <leaf>[,<leaf>...]]
```

Window defaults to the focused window; output defaults to the preferred output.
Explicit identifiers use the existing window/output identities. Layer and region
require `--target` (layer inspection ID or region name). Global accepts no target.
A region action applies to all output instances of that named region. Scope filters
use exact leaf names; omission means all scopes applicable to the target kind.
Unknown/duplicate flags, names, targets, and inapplicable filters are errors.
Every set/cycle reference must contribute at least one leaf within the requested mask.

Global updates default selections at the appearance level; it does not override
more-specific rules. Output affects that output's scopes and surface defaults.
Window/layer actions affect only that subject. System accepts only off/on/toggle/
default with no target or scope, and controls the master custom-effect gate.

`set` replaces the saved runtime selector for the addressed scope mask. Re-resolve
from config plus other runtime masks; never merge against the previous rendered
result. Filter selected definitions to that mask. Omitted leaves inherit configured
values; explicitly disabled leaves clear them. `cycle` selects the next concrete
candidate from the named choice and replaces its contribution within that mask.
Overlapping runtime assignments use the most recent assignment for each leaf.

`off` adds a disabled override for addressed scopes while retaining their saved
runtime selections and choices. `on` removes that disabled override; `toggle` turns
the whole addressed mask off if any leaf in it is effective, otherwise on. It is
not individual inversion of each leaf. `default` clears runtime selections and
disabled overrides for the mask and restores configuration; keep a still-valid
configured choice assignment. Set/cycle lift off masks for the addressed scopes.
Runtime selections last for the session and re-resolve on reload; deleted runtime
references are cleared with a diagnostic and restore configured selection.

```sh
umbriel msg 'effect:window set neon'
umbriel msg 'effect:window cycle terminals --scope border.inner,border.outer'
umbriel msg 'effect:window toggle --scope content'
umbriel msg 'effect:global set lightning-melt --scope open,close'
umbriel msg 'effect:output set warm-screen --target DP-1 --scope screen'
umbriel msg 'effect:system off'
```

Remove the old `shader:*` action; do not retain aliases. Independent cursor/screen
bindings become explicit choices restricted to overlay/screen. Animation pair
bindings become choices or set operations on open/close. There is no implicit
alphabetical library cycle or name-prefix classification.

`umbriel effects [--json]` reports the library, policy gates, and resolved state for
outputs, windows, layers, and regions. Filters `--window ID`, `--output ID`,
`--layer ID`, or `--region NAME` may select one subject kind. Report each leaf's
source effect, pass list/settings, assignment and definition file/line, overridden
sources, chosen variant/lease, and any suppression reason. For event scopes, show
both configured next-event selection and an active retained generation when different.
Persist provenance during resolution. Normal subject inspection supplies IDs;
inspection does not mutate selections or trigger allocations.

## 11. The review's configuration, expressed completely

This translates the behaviors shown in the external review, using relative paths
for readability. Native settings elsewhere in the user's configuration stay intact.
The original absolute Nix store paths can be used unchanged in each `shader` value.

```toml
[animation]
enabled = true

[appearance]
effects = ["lightning", "lightning-melt", "workspace-reveal", "comet"]

[render.effects]
enabled = true
redraw = "auto"
in_capture = false
reads_cursor = false
fps = 0

[effects.lightning.border.outer]
padding = 30
animated = true
speed = 0.35
palette = true
passes = [{ shader = "shaders/barrulus/rings/lightning.glsl" }]
light = { enabled = true, spread = 80, intensity = 1.6, threshold = 0.5 }

[effects.lightning.border.inner]
enabled = false

[effects.lightning-melt.open]
duration_ms = 400
curve = "linear"
passes = [{ shader = "shaders/barrulus/animations/lightning-open.glsl" }]

[effects.lightning-melt.close]
duration_ms = 500
curve = "linear"
passes = [{ shader = "shaders/barrulus/animations/melt-close.glsl" }]

[effects.workspace-reveal.workspace]
passes = [{ shader = "shaders/barrulus/animations/reveal.glsl" }]

[effects.comet.overlay]
passes = [
  { shader = "shaders/barrulus/cursor/comet-0.glsl" },
  { shader = "shaders/barrulus/cursor/comet-1.glsl" },
]

[effects.sentient.content]
palette = true
passes = [{ shader = "shaders/sentient-circuit-v2.glsl" }]

[effects.smoke.content]
palette = true
passes = [{ shader = "shaders/barrulus/window/rainbow-smoke.glsl" }]

[[window_rule]]
match.title = "^ghostty$"
default_workspace = 2
effects = ["sentient"]

[[window_rule]]
match.title = "^rmpc$"
default_workspace = 5
effects = ["smoke"]

[[window_rule]]
match.app_id = "^steam_app_.*$"
effects = ["no-content"]

[effects.no-content.content]
enabled = false
```

The review supplies excerpts, not the entire live files. This covers every behavior
in those excerpts; the game opt-out additionally demonstrates the earlier proposal's
scope-specific off case. It is not a claim to have inspected or converted the user's
live configuration.

## 12. Complete capability mapping

This is a design/release checklist, not a compatibility reader or a claim that all
changes are mechanically interchangeable.

| Existing setting or capability | New home / explicit decision |
| --- | --- |
| `shaders.preset.NAME`, scope window | `effects.NAME.content` |
| Scope border postprocess preset / border `overlay` reference | `effects.NAME.border.inner`; put paired halves under one name |
| `appearance.border_shader`, `shaders.border.NAME` | `effects.NAME.border.outer` plus appearance selection |
| `window_rule.shader`, `window_rule.border_shader` | `window_rule.effects`; definitions contain the applicable leaves |
| `shaders.window` | Appearance-selected content effect |
| `shaders.output`, `output.NAME.shader` | Appearance/output-selected screen effect |
| `shaders.global`, including cursor/screen-prefixed presets | Appearance-selected overlay effect; names have no scope semantics |
| `shaders.region` | Named `effect_region` with a screen-effect selector |
| `animation.windows_in.shader` | Open scope; window timing defaults remain in animation.windows_in |
| `animation.windows_out.shader` | Close scope; window timing defaults remain in animation.windows_out |
| `animation.windows_move.shader` | Move and resize leaves using the same source, where both old behaviors are wanted |
| `animation.workspaces.shader` | Workspace scope |
| `animation.overview.shader` | Overview scope |
| `animation.scratchpad.shader` | Scratchpad and backdrop leaves, preserving both targets explicitly |
| `animation.border.shader` | Border.focus scope |
| `animation.dim_unfocused.shader` | Focus scope |
| `animation.layers.shader` | Open and close leaves applied through layer rules; native layer timing remains |
| `animation.pair.NAME.open/close`, animation preset selector | Effect open/close leaves and effects selector; explicit 400/500 ms where preserving those old pair defaults |
| Native curve/style/scale/enable settings and curve registries | Retained in animation; lifecycle effects can override curve/duration only |
| `windows_move.wobble`, temporary `wobble_style` | Native `windows_move.drag_physics` fallback and named `drag` presets; no generic shader settling contract |
| Border `animated`, `speed`, `palette`, `padding` | Same keys on the appropriate border leaf |
| Border `light.*` | Border.outer.light; compositor controls |
| Preset `palette` | Palette on the new leaf; remains opt-in |
| Builtin pass `preset`, `amount`, `kelvin` | Explicit builtin plus params |
| Pass `buffer` and 1–16 limit | Retained with the interface restrictions in section 3 |
| Preset `cursor_radius` | Overlay cursor_radius |
| `shaders.redraw`, `in_capture`, `reads_cursor` | Render.effects policy; on-damage spelling becomes on_damage |
| `appearance.shader_fps` | Render.effects.fps |
| `shaders.enabled` | Render.effects.enabled; deliberately expanded to all configured custom pipelines |
| Border pool unused-first / round-robin | Choice selection unused_first / round_robin, with lease/exhaustion rules |
| `shaders.window_pool` | Round_robin choice used by explicit cycle binding; no auto-application unless selected |
| `shader:*` actions / prefix-based cycles | Effect action with explicit target/scope and named choices |
| Missing/invalid shader drops to native on reload | Candidate effect generation rejected; last working configuration retained |
| Relative source paths and watches | Retained at pass definition origin |
| Existing native behavior with no custom shaders | Retained |

No existing shader event, builtin, region, buffer capability, cursor stage, light
control, or allocation policy is intentionally dropped. Native layer shader selection
now has its own rules: a globally selected open shader supplies defaults to both
windows and layers, subject to their native enable gates. To preserve different old
window/layer sources, add a matching layer rule selecting the layer-specific effect.
Assigning the old combined movement pipeline to both move/resize uses the single-
evaluation rule in section 5 when both channels change.

## 13. Required implementation and acceptance

Version 1 includes the complete schema above, all mapped existing capabilities,
per-surface selection, multi-pass event/outer-border pipelines, params, runtime
controls, and inspection. Multi-pass renderer work is real work: event slots are
not pass slots, and outer-border postprocessing needs appropriate intermediates.
Use the existing shader hosts, geometry owners, simulation, cache/refcount patterns,
and capture machinery. No universal shader ABI rewrite or new motion simulation is
required. The previous speculative velocity/settling extension is explicitly excluded.

Implement in reviewable stages, without releasing competing old/new selectors:
configuration/resolution and provenance; renderer pipeline support; surface/event/
output binding and runtime actions; converted assets and inspection; regression and
performance verification. Keep unrelated include behavior and native presentation
stable. No extra global rendering stage beyond the specified screen/overlay model
is implied by the implementation sequence.

Acceptance requires:

- The section 11 example resolves every reference and renders all shown scopes with
  real installed shader paths; no feature in the review is left without a mapping.
- Every native event shader maps to its correct target and native clock, including
  workspace/overview ancestry and scratchpad backdrops.
- Inner decoration survives an opaque content effect; external-only choices clear
  the paired inner half; focus/urgent/fullscreen behavior and native border geometry
  remain correct at fractional scale and rotation.
- Effects compose through simultaneous move/resize, opening during reflow, close
  interruption, overview copies, and delayed resize commits without changing layout,
  client input, or configure delivery. Drag physics retains its existing behavior.
- Screen, overlay, cursor, overlapping regions, capture inclusion, isolated capture,
  illumination stacking, lock/unlock, and multiple-output histories are verified.
- Includes reject duplicate effect ownership; resolution checks inheritance, empty
  selectors, disabled leaves, applicability, rules, output moves, and runtime masks.
- Choices retain assignments across focus/reload, reuse least-used entries on
  exhaustion, cycle correctly, and release reservations separately from close visuals.
- Invalid shader generations leave the active configuration unchanged; ordinary
  reloads preserve active event references; explicit off releases state safely.
- Shader interfaces, params, builtins, feedback, all converted collection definitions,
  renderer recovery, and per-pass resource ownership have appropriate tests.
- Native unshaded behavior, analytic-shadow/direct-scanout fast paths, static idle
  behavior, shader-only FPS caps, and memory release are preserved and measured as
  appropriate. GPU compilation and performance claims require actual renderer tests.

This finishes the configuration and observable behavior specification. Internal C/C++
class layout, buffer pooling strategy, and code organization remain implementation
choices governed by these contracts and the project's contribution rules.

## 14. Pointer-drag deformation extension

Added by the implementation request: `drag` is a window-only simulation scope.
It is distinct from native geometry timelines and from `move`/`resize` shaders.
`[effects.NAME.drag]` uses the same effect names, selectors, precedence, choices,
runtime masks, provenance, and disabled-leaf rules. Its enabled form accepts CPU
simulation coefficients instead of `passes`, `params`, palette, or timeline settings.
An enabled drag scope requires a parameter or explicit `enabled = true`.

Selecting an enabled drag preset activates pointer deformation without requiring
`animation.windows_move.drag_physics = true`. The native animation master and
`windows_move.enabled` remain gates. `render.effects.enabled` gates custom drag
presets; when no custom drag preset applies, `drag_physics = true` retains native jelly.
The temporary `animation.windows_move.wobble_style` key is removed without an alias.

The parameters are `stiffness` (1–1000, default 36), `coupling` (0–500, 100),
`damping` (0.5–60, 6.5), `pointer_response` (0–10, 2),
`stiffness_gradient` (−0.9–1, 0), `lag_gradient` (−0.9–4, 0),
`downward_pull` (0–50, 0), `motion_gain` (0–32, 8), and `decay` (0.1–30, 2.8).
All must be finite. Gradients multiply the corresponding coefficient by
`1 + gradient * normalized_vertical_position`. Motion gain loads a bounded
0–1 reservoir from pointer distance normalized by window dimensions; decay drains
it exponentially per second. Downward pull scales vertical acceleration by window
height, reservoir, distance below the grab point, and the existing belly profile.

The shipped jelly and Taffy definitions are data. Taffy uses coupling 48, damping
4.8, stiffness gradient −0.55, lag gradient 0.9, downward pull 18, and the remaining
jelly defaults. Reloads update the coefficients of active simulations without
resetting their grab, displacement, velocity, reservoir, or transition identity.

Keep the fixed 240 Hz integration, grab pinning, bounded displacement/velocity,
no-folding constraint, inverse sampling, expanded rendering bounds, and complete
settling, including resting while still held. This does not add shader-controlled
settling or a generic velocity-driven GLSL interface.

The native Jelly opt-in is named `animation.windows_move.drag_physics`. This replaces
the original requirement to preserve the `wobble` spelling; the old key is rejected,
with no compatibility alias. Shared simulation and rendering machinery uses
drag-physics/deformation terminology.
