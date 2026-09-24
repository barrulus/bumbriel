# Effects and the Barrulus collection

Named effects combine shader pipelines and pointer-drag physics. Define them in
`[effects.NAME]`, then select them with `effects = ["NAME"]`. Including a library
registers its definitions without selecting anything. Native animations, window
geometry, input, borders, shadows and blur remain configured independently.

## Selecting effects

```toml
[include]
files = ["shaders/barrulus/collection.toml", "shaders/barrulus/choices.toml"]

[appearance]
effects = ["lightning", "whirlpool", "cursor.comet-glow"]
border_width = 6
outer_border_width = 0

[render.effects]
enabled = true
in_capture = false
reads_cursor = false
redraw = "auto"
fps = 30

[[window_rule]]
match.app_id = "^foot$"
effects = ["window.parchment-dark", "terminals"]

[output.DP-1]
effects = ["screen.warmtint"]
```

The collection includes window content, screen, cursor and lifecycle definitions.
`choices.toml` adds border definitions and named lists. Individual files under
`window/`, `screen/`, `cursor/`, `rings/` and `animations/` can also be included.
Names have no scope semantics. The lightning lifecycle definition is named
`lightning-melt`; the persistent border is `lightning`.

Paths resolve relative to the file defining each pass. An effect name belongs to
one file: duplicate ownership across includes is an error. A file may define
several leaves under one name.

Resolution applies appearance, runtime global defaults, output defaults, runtime
output defaults, matching rules in order, then runtime subject settings. Each
selected definition replaces its complete contribution to each leaf. Omitted
leaves inherit; an empty selector clears the applicable inherited leaves.

```toml
[effects.no-content.content]
enabled = false

[[window_rule]]
match.app_id = "^steam_app_.*$"
effects = ["no-content"]
```

## Scope and pipeline settings

| Leaves | Target and interface |
| --- | --- |
| `content` | Window or layer content; `postprocess` |
| `border.inner` | Rounded client box above content; `postprocess` |
| `border.outer` | Existing native double-band border; first `ring_color`, then `postprocess` |
| `border.focus` | Native border focus transition; `animation` |
| `open`, `close` | Window or layer lifecycle; `animation` |
| `move`, `resize` | Native window geometry clocks; `animation` |
| `focus`, `scratchpad` | Window focus dim and scratchpad transition; `animation` |
| `backdrop`, `workspace`, `overview` | Output-owned native transitions; `animation` |
| `screen` | Per-output scene filter; `postprocess` |
| `overlay` | Per-output final custom stage, including cursor effects; `postprocess` |
| `drag` | Window pointer simulation; CPU parameters, no GLSL passes |

Shader pipelines contain 1–16 ordered passes. Each pass uses exactly one `shader`
path or explicit `builtin`. Builtins are `invert`, `grayscale`, `saturation`
(default amount 1.5), and `temperature` (default kelvin 4000; neutral at 6500).
The collection's `screen.grayscale` and `screen.warmtint` retain their authored
formulas independently of these builtins.

```toml
[effects.reading.content]
passes = [
  { builtin = "temperature", params = {kelvin = 6500} },
  { builtin = "saturation", params = {amount = 0.7} },
  { shader = "reading.glsl", params = {strength = 0.4, tint = [1.0, 0.9, 0.8]} },
]
```

`params` is a flat table of linked GLSL uniforms: boolean, signed 32-bit integer,
float, or numeric vectors of length 2–4. Types must match; unknown, optimized-out,
array, sampler, reserved host names and nonfinite values are errors. Integer
vectors require integer components; floating uniforms and vectors accept numeric values.
Omitted user uniforms retain their GLSL defaults independently of other effect instances.
`buffer = true` is available only on file-based postprocess passes that define
`postprocess_buffer`.

Persistent leaves support `animated` and `speed` (0–10). False animation or zero
speed freezes time at zero while client damage still updates the input. `palette`
is opt-in on every shader leaf. Border leaves support `focused_only` (default
true). `border.outer` additionally supports `padding` (0–1024 logical pixels) and
`light`. `overlay.cursor_radius` bounds a cursor-local effect; zero covers the output.

```toml
[effects.neon.border.outer]
passes = [{shader = "shaders/barrulus/rings/neon-bleed.glsl"}]
padding = 14
light = {enabled = true, spread = 80, intensity = 1.0, threshold = 0.5}

[effects.neon.border.inner]
passes = [{shader = "shaders/barrulus/window/neon-bleed-overlay.glsl"}]
```

Inner decoration renders above content, survives opaque content effects and is
captured inside lifecycle animations. A border choice's external-only candidates
must explicitly disable `border.inner` to clear the previous candidate's inner
contribution. Padding and light spread do not change client geometry or input.
The host preserves the rounded client hole after the entire outer chain; light
uses the final processed ring, with the native threshold and stacking behavior.

## Native timelines and drag physics

Native event enables remain gates. Only `open` and `close` may override
`duration_ms` (1–10000) and `curve`; each omitted setting inherits the corresponding
native event. Curves can name the native Bézier or spring registry. An explicit
duration with a resolved spring curve is an error because springs settle on their
own clock. Move and resize retain the compositor's existing geometry timelines;
identical pipelines run once when both channels change together.

Events retain their source, parameters, palette, timing and feedback through
ordinary reloads. New events use the new generation. Explicit runtime off, the
master gate, or disabling the native event cancels active custom effects.

Drag physics is independent of these timelines. `animation.windows_move.drag_physics = true` opts into native Jelly. Selecting `[effects.NAME.drag]` enables a named
simulation without that switch. See [drag physics](animation.md#drag-physics) and
[the shipped Jelly/Taffy definitions](../../examples/effects/drag.toml) for all
reloadable coefficients. The native animation and movement enables remain gates.

## Choices and runtime controls

```toml
[effects.reading-favourites]
choose = ["window.parchment-dark", "window.adaptive-text-v4"]
selection = "round_robin"

[keybinds]
"Mod+S" = "effect:window cycle reading-favourites --scope content"
"Mod+Shift+S" = "effect:window toggle --scope content"
"Mod+Alt+S" = "effect:window cycle terminals --scope border.inner,border.outer"
```

Choices contain unique concrete names defining the same leaf set and belonging
to one owner family. They cannot contain other choices. `unused_first` assigns
unused, then least-used members; `round_robin` follows list order; `random` samples
uniformly. Cycling excludes the current member when possible. Unselected choices
provide manual favourites without automatically styling new windows.

Assignments survive focus, temporary off, output moves and ordinary reloads by
candidate name. Removing a candidate reallocates affected owners. Unmap releases
its reservation while a closing snapshot retains its visuals until completion.

```sh
umbriel msg 'effect:window set neon'
umbriel msg 'effect:window toggle --scope content'
umbriel msg 'effect:output set screen.warmtint --target DP-1 --scope screen'
umbriel msg 'effect:global set whirlpool --scope open,close'
umbriel msg 'effect:system off'
umbriel msg 'effect:system default'
umbriel effects --json
umbriel effects --window WINDOW_ID --json
```

Kinds are `global`, `output`, `window`, `layer`, `region` and `system`. Operations
are `set`, `cycle`, `off`, `on`, `toggle` and `default`. Layer and region require
`--target`; windows default to the focused window and outputs to the preferred
output. `--scope` accepts exact comma-separated leaf names. Region actions affect
all output instances of the named region. System accepts only gate operations.
`default` clears runtime overrides; off retains choices so on does not reroll.
Runtime selections last for the session. Deleted runtime references restore
configuration with a diagnostic. There is no implicit alphabetical cycle.

Inspection reports the library, gates, source locations, overridden assignments,
leases, suppression reasons and retained active events. It never allocates choices.
Normal windows, outputs and layers inspection provides target identifiers.

## Regions, capture and scheduling

```toml
[effects.reading-screen.screen]
passes = [{builtin = "saturation", params = {amount = 0.7}}]

[[effect_region]]
name = "reading-area"
output = "DP-1"
effects = ["reading-screen"]
x = 20
y = 40
width = 800
height = 600
```

Regions use output-local logical coordinates and run in declaration order before
screen and overlay. Omit `output` to instantiate a region on each output.
Histories are separate for every instance and pass. Capture draws do not advance
display feedback; transforms, working format changes and locking reset history.

`render.effects.redraw` accepts `auto`, `on_damage` and `continuous`. Auto uses
linked uniforms and feedback capabilities. The FPS cap applies only to shader
idle frames; input, client damage and native animation retain their cadence.
Static effects do not keep repainting. Disabling custom effects releases their
histories and restores the native rendering path.

`in_capture = false` excludes persistent postprocessing from protocol captures;
true includes it. Isolated window captures use that window's surfaces without
desktop backdrop or light spill. Lifecycle shaders and native decoration retain
their existing capture policy. `reads_cursor = true` places a software cursor in
the overlay input; otherwise the cursor is composed afterwards.

## Palette, reload and validation

`palette = true` publishes accent_primary, accent_secondary, warning and error
from `[colors]`, in that order. Other colours are excluded. The shared
`umbriel_palette_count` and `umbriel_palette_at(t)` helpers work in all contracts.
The disabled fallback is the native ring base for decorations and opaque white
for postprocessing and animation. Ordinary palette edits update persistent
instances; active events retain their starting palette.

Source edits reload automatically. Runtime preparation compiles every enabled
concrete pipeline, including currently unselected effects, and validates uniform
bindings before committing any configuration. Failure retains the entire previous
working configuration and keeps watching failed source paths for corrections.
The disabled master gate defers GPU preparation but still validates the schema
and source files. Startup errors use native/default recovery.

`umbriel validate -c config.toml` checks schema and source files without a GPU and
explicitly reports that compilation and uniform binding were not checked. Renderer
recovery rebuilds programs and resets invalid GPU histories. Shader values use
sRGB-compatible contracts with conversions around linear/HDR composition.

## Authoring a ring

Provide `vec4 ring_color(vec2 coords)` returning **straight RGBA**. The host
applies the configured base alpha, punched client hole and premultiplication.

- `coords`: logical pixels from client top-left, X right and Y down.
- `ring_size`, `ring_radius`: client dimensions and clockwise TL/TR/BR/BL radii.
- `ring_width`: sum of the native border widths; `ring_padding`: raster allowance.
- `ring_distance(coords)`: rounded-client signed distance, positive outside.
- `ring_base_color(coords)`: configured straight base colour.
- `umbriel_time`: monotonic elapsed seconds multiplied by configured speed.
- `umbriel_scale`: physical pixels per logical pixel for antialiasing.
- `umbriel_palette_count`: published colours, zero unless `palette = true`.
- `umbriel_palette_at(t)`: the ramp at `t`, wrapping so `t` and `t + 1` agree.

A shader chooses whether to follow the palette by testing the count, which keeps
it correct under either setting and needs no second source file:

```glsl
vec3 tint = umbriel_palette_count > 0
    ? umbriel_palette_at(umbriel_time * 0.08).rgb
    : vec3(0.15, 0.8, 1.0);
```

Pass a hue-wheel shader the same position it already gives the wheel and the
ramp cycles where the spectrum did. `umbriel_palette_at` returns
`ring_base_color(coords)` when the count is zero, so an unguarded call degrades
to the configured border colour rather than to black.

These shader examples are authored by Barrulus.

## Authoring postprocess shaders

Define `vec4 postprocess(vec3 coords)` returning premultiplied RGBA. `coords.xy`
is normalized across the window for window scope, or across the output for output,
region and cursor scope. Coordinate Z is 1. Texture helpers use that same domain;
sampling beyond the captured rectangle returns transparent pixels.

- `umbriel_size`: physical size of this captured rectangle, in its logical orientation.
- `umbriel_output_size`: physical output size, or window size in window scope.
- `umbriel_region`: normalized output rectangle `(x, y, width, height)`; window scope uses `(0, 0, 1, 1)`.
- `umbriel_cursor`: physical output-local pointer coordinates; window scope receives zero.
- `umbriel_scale`: physical pixels per logical pixel.
- `umbriel_time`: monotonic seconds; shared compositor clock, multiplied by the leaf speed.
- `tex2D_screen(uv)`: current pass input.
- `tex2D_source(uv)`: original composition before this chain, unchanged between passes.
- `tex2D_prev(uv)`: this pass's previous successful result, initially transparent.
- `tex2D_screen_prev(uv)`: previous original source, initially the current source.
- `tex2D_buffer(uv)`: dedicated accumulator, or previous result when none is declared.

A pass with `buffer = true` also defines `vec4 postprocess_buffer(vec3 coords)`.
It runs first, and its newly rendered accumulator is visible to that pass's colour
function. Comet and comet-glow instead use two ordinary passes: accumulator then
composition over the original source. History is separate for every pass, window
and output, and advances only after successful render-pass submission. Geometry,
transform, colour mode, chain changes and session locking reset it.

The shader files expose editable constants. Descending
`smoothstep` edges are expressed through a defined helper, removing source GLSL
undefined behaviour while retaining the intended reversed falloff.
