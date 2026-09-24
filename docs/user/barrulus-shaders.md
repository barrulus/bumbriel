# Barrulus shader collection

The collection by Barrulus provides animation pairs, persistent borders with
optional illumination, window effects, screen effects and cursor effects.

## Development environment

The repository already supplies `flake.nix` and `nix/devshell.nix`, pinned to
wlroots 0.20.2. Use `nix develop` and then the normal `just` commands. The local
`.envrc` contains `use flake`; enable it with `direnv allow` after reviewing it.
This follows Quixote's project-pin workflow and needs no changes to Quixote's
shared Rust or Qt development shells. Hardware renderer tests need access to
`/dev/dri/renderD*`; isolated compositor checks do not replace your running session.

## Lifecycle presets

Include one of `whirlpool.toml`, `melt.toml`, `ripple.toml`, or `lightning.toml`
from the installed `share/umbriel/shaders/barrulus/animations` directory. Each preset selects
both directions, with a linear curve and 400/500 ms durations
(lightning uses 400/400 ms). See the [asset README](../../examples/shaders/barrulus/README.md)
for installation and editing.

For Nix, derive paths from your selected package:

```nix
{ config, ... }: {
  programs.umbriel.settings.include.files = [
    "${config.programs.umbriel.package}/share/umbriel/shaders/barrulus/animations/whirlpool.toml"
  ];
}
```

## Persistent active border

```toml
[appearance]
border_width = 6
outer_border_width = 0
shader_fps = 30 # shader-only idle redraw cap; 0 follows native refresh

[appearance.border_shader]
enabled = true
shader = "shaders/barrulus/rings/lightning.glsl"
animated = true
speed = 1.0
padding = 24
palette = false
```

Paths resolve relative to the TOML file containing `shader`, including included
files. Shader edits are watched automatically. Invalid or missing shaders use
ordinary configured border colours with their original widths; correcting or
creating the file recovers on reload. Program compilation happens on config
application, and failed source is cached until it changes.

This interface selects the **complete existing double-band border**. It does
not create another focus-ring geometry. Unfocused, urgent, fullscreen and hidden
decorations retain their ordinary styling/visibility. `padding` reserves drawing
space for irregular edges and sparks; it changes neither the layout gap nor
client size or input regions. The client hole remains transparent.

The supplied `rings/*.toml` presets configure width 6 and:

| Shader | Padding | Controls inside GLSL |
| --- | --- | --- |
| `pulse.glsl` | 0 | Cyan pulse |
| `rainbow-ripple.glsl` | 14 | Flowing pastel wax |
| `lightning.glsl` | 24 | `LIGHTNING_COUNT` 1–4; `SPEED=1` gives a four-second lap |
| `fuse.glsl` | 48 | `EMBER_COUNT` 1–4; `FUSE_SECONDS=10`, `FUSE_WANDER=1`, `FUSE_BRIGHTNESS=1` |

A per-window block replaces the inherited block, so disabling a selected
application restores its normal border:

```toml
[[window_rule]]
match.app_id = "^foot$"
[window_rule.border_shader]
enabled = false
```

## Following the configured colours

`palette = false` is the default and leaves every shader on the colours written
into its own GLSL. Setting it true publishes four `[colors]` entries to the
shader in this order:

| Index | Key |
| --- | --- |
| 0 | `accent_primary` |
| 1 | `accent_secondary` |
| 2 | `warning` |
| 3 | `error` |

Only those four are published. The remaining entries are backgrounds, near-greys
and duplicates, which muddy a cycle rather than extend it.

```toml
[appearance.border_shader]
shader = "shaders/barrulus/rings/rainbow-ripple.glsl"
palette = true
```

A shell that rewrites `[colors]`, such as one regenerating a scheme from the
wallpaper, therefore moves the ring with it on the next config reload. Nothing
restarts and no shader is recompiled.

The same key exists on window, output and global presets; see
[following the configured colours](#following-the-configured-colours-1) for those.

Of the supplied rings, `pulse.glsl`, `rainbow-ripple.glsl` and `lightning.glsl`
read the palette. `lightning` takes its body from the ramp and drives its core
toward white, so the bolt stays readable whatever the scheme.

The others keep their own colours whatever this key says, because their
appearance is the effect: `fuse.glsl` is a burning fuse and `portal-lava.glsl`
is molten rock.

`speed` is clamped to 0–10 and padding to 0–1024 logical pixels. `animated = false`
or speed zero freezes the shader at time zero. A compiled program that does not
use time never requests animation frames; comments mentioning time do not count.
Only outputs containing visible animated decorations request those frames.
Client damage and normal animations retain their normal scheduling under the cap.

Overview cards use their own scaled border geometry. Closing snapshots freeze
the current ring time. Output captures include these normal scene decorations.
Shaders run in sRGB and their results are
converted for a linear working framebuffer. Physical HDR appearance still needs
hardware validation.

## Decoration illumination

Every ring can emit light from its actual bright details:

```toml
[appearance.border_shader.light]
enabled = true # otherwise off
spread = 80 # logical pixels, 1–256
intensity = 1.0 # 0–4
threshold = 0.5 # 0–1, measured after opacity
```

The lightning and fuse presets enable their original light settings. Emission
uses half-resolution logical pixels, float intermediates where supported,
Dual Kawase diffusion and exponential screen blending. Static emission is cached;
moving sparks update on the shader cadence. Turning lighting off releases its
textures. Padding still affects only the ring raster; spread adds no layout gap.

The spill is a separate scene layer above ordinary, floating, scratchpad and
overview content, below panels and overlays. Bumbriel keeps its existing
pinned/fullscreen stacking above panels, so those windows also cover spill.
Spill is suppressed during native fades and shader transforms, excluded from
closing snapshots and isolated window captures, and included in output captures.

## Shader pools

Include `shaders/barrulus/pools.toml` after `windows.toml` and any static ring rules
for an example with rotating terminal rings and a short Mod+S favourites list.
`Mod+T` can remain `spawn:ghostty`. The example adds `Mod+Alt+S` to cycle rings;
`Mod+Shift+S` keeps toggling the current window-content effect.

Pools contain preset names in your chosen order:

```toml
[shaders]
window_pool = "reading"

[shaders.pool.reading]
scope = "window"
presets = ["window.parchment-dark", "window.adaptive-text-v4"]

[shaders.border.fuse]
shader = "shaders/barrulus/rings/fuse.glsl"
padding = 48
speed = 1.0
[shaders.border.fuse.light]
enabled = true
spread = 90
intensity = 1.4
threshold = 0.5

[shaders.border.pulse]
shader = "shaders/barrulus/rings/pulse.glsl"

[shaders.pool.terminals]
scope = "border"
allocation = "unused-first"
presets = ["fuse", "pulse"]

[[window_rule]]
match.app_id = "^com[.]mitchellh[.]ghostty$"
[window_rule.border_shader]
pool = "terminals"
```

Border presets contain complete shader settings, including animation, speed,
padding, illumination and an optional inward overlay. A pool can also be selected globally with
`[appearance.border_shader] pool = "terminals"`. Pool entries replace the
selector's shader settings; `enabled = false` on the selector still disables it.

A paired ring sets `overlay` to a postprocess preset. The inward layer renders
**after the window-content shader**, so it can sit over CRT, parchment or another
window effect without replacing it. Both halves follow border selection, focus,
and `shader:border toggle`. Choosing an external-only ring removes the previous
inward layer. The overlay is clipped to the window's rounded content box and
follows `shaders.in_capture`. Like window-content effects, it is suppressed during
lifecycle fades. The border's `speed`, `animated` and `light` settings affect its
external shader; an overlay uses its own GLSL animation timing.

```toml
[shaders.border.neon-bleed]
shader = "shaders/barrulus/rings/neon-bleed.glsl"
padding = 14
overlay = "ring.neon-bleed"

[shaders.preset."ring.neon-bleed"]
scope = "border"
passes = [{ shader = "shaders/barrulus/window/neon-bleed-overlay.glsl" }]
```

Use `scope = "border"` for overlay presets to keep them out of window-effect
cycles. `pools.toml` includes paired `neon-bleed` and `portal-lava` entries; their
standalone files under `rings/` also use this overlay setting. These two effects
are authored for a 6px border and 10px outer corner radius. Replace legacy
focus-matched `window_rule.shader` assignments with `border_shader.overlay` when
migrating an existing paired ring.

A mapped window retains its ring assignment when focus or rules are refreshed.
`unused-first` chooses an unused entry, then a least-used entry if all are occupied,
breaking ties in pool order starting after the previous allocation. Closing or
unmapping releases the reservation. Hidden mapped windows retain theirs, including
windows whose ring is toggled off. `round-robin` always advances in pool order and
wraps, allowing duplicates. Pools allocate independently.

Reloads preserve assignments by preset name, even when reordered. Removing an
assigned entry selects another member; removing the pool restores the selector's
static shader or ordinary border. Shader source and settings edits update existing
assignments. Runtime choices last for the compositor session.

`shader:border cycle` advances within the window's pool, preferring unused then
least-used entries under `unused-first`. It changes the ring whenever the pool has
more than one member. `shader:border cycle:terminals` selects an explicit pool;
`shader:border fuse` selects a named ring directly. `toggle`, `off`, `on`, and
`default` are supported, as is an optional window identifier. `default` restores
the configured pool and keeps a still-valid assignment.

For window-content effects, `shader:window cycle` uses `window_pool` when set;
`shader:window cycle:reading` uses an explicit pool. These cycles follow the listed
order and wrap directly, with no extra default/off step. They advance from the
currently effective preset, including a rule or global default. Use `toggle` to
turn the effect off. Window pools do not allocate effects to newly opened windows;
`allocation` applies only to border pools. Without a window pool, the existing
alphabetical cycle is retained. Pools must have a nonempty list of unique known
names; window entries must have window scope or be builtins. Invalid pools produce
configuration diagnostics and are ignored.

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

## Window, screen and cursor presets

Register the collection, then select the effects you want. Registration alone
leaves persistent postprocessing off:

```toml
[include]
files = ["shaders/barrulus/collection.toml"]

[shaders]
window = "window.parchment-dark" # default for windows; empty or "off" disables
output = "temperature" # default output filter, 4000 K
# Global effects run independently on each output.
global = "cursor.comet-glow"
in_capture = false
reads_cursor = false
redraw = "auto"

[output.DP-1]
shader = "invert" # overrides the output default

[[window_rule]]
match.app_id = "^foot$"
shader = "window.adaptive-text-v4"
```

Use an installed package path for `collection.toml` just as for lifecycle presets.
Use `windows.toml` to register only the full window collection. It selects no
effect; `shader:window cycle` and `shader:window toggle` control the focused
window. Preset names follow filenames, for example `window.fire-tendrils`,
`window.snowfall`, and `window.cvd-deutan-alphabet`.

Alternatively include individual `window/*.toml`, `screen/*.toml`, or
`cursor/*.toml` files. Preset names are their directory and filename joined by a
dot. All shader paths remain relative to the declaring preset file.

| Scope | Presets |
| --- | --- |
| Window | See [the complete window inventory](../../examples/shaders/barrulus/windows.toml). This includes Barrulus's fire, weather, rainbow and CVD variants. |
| Screen | `crt`, `grayscale`, `vignette`, `warmtint` |
| Cursor | `adaptive`, `blueglow`, `comet`, `comet-glow`, `rainbow-tunnel`, `rainbow-tunnel-bare`, `ripple`, `shockwave`, `spotlight`, `trail` |

The separate builtins are `grayscale` (Rec.709 coefficients), `invert`,
`saturation` (1.5), and `temperature` (4000 K). `screen.grayscale` retains the
source's `(0.299, 0.587, 0.114)` coefficients. `screen.warmtint` retains its own
RGB multipliers. Temperature is exactly neutral at 6500 K.

### Following the configured colours

A preset opts in with `palette = true`, the same key and the same four `[colors]`
entries the [border shaders](#following-the-configured-colours) publish:

```toml
[shaders.preset."window.rainbow-smoke"]
scope = "window"
palette = true
passes = [{ shader = "shaders/barrulus/window/rainbow-smoke.glsl" }]
```

The uniforms are identical, so one shader body is correct in either scope:

- `umbriel_palette_count`: published colours, zero unless `palette = true`.
- `umbriel_palette_at(t)`: the ramp at `t`, wrapping so `t` and `t + 1` agree.

Where a ring falls back to `ring_base_color()`, a postprocess pass has no base
colour to fall back to, so `umbriel_palette_at` returns opaque white when the
count is zero. Test the count rather than relying on that:

```glsl
vec3 tint = umbriel_palette_count > 0
    ? umbriel_palette_at(hue).rgb
    : 0.5 + 0.5 * cos(6.2831853 * (hue + vec3(0.0, 0.33, 0.67)));
```

`rainbow-smoke`, `rainbow-waves`, `rainbow-radial`, `rgb-shimmer` and
`rgb-border` read it. Every other shipped effect keeps its own colours, because
their appearance is the effect rather than a theme.

The key is read off the preset naming each effect, so window, output, region and
global scopes opt in separately and nothing is inherited from another effect. A
window preset with `palette = false` sees a count of zero whatever the global
preset does. A shell rewriting `[colors]` moves every opted-in effect together on
the next reload.

Custom ordered chains contain 1–16 passes. A broken or missing pass disables
the entire chain until corrected. No partial chain or stale last-good program
is displayed. Source files use the native 256 KiB, regular-file, nonblank and
NUL-free checks and automatic dependency watching.

```toml
[shaders.preset.reading]
scope = "output" # window, output, global: cycle lists; border: inward overlays
[[shaders.preset.reading.passes]]
preset = "temperature"
kelvin = 6500
[[shaders.preset.reading.passes]]
preset = "saturation"
amount = 0.7
[[shaders.preset.reading.passes]]
shader = "my-effect.glsl"
# buffer = true # only if the source also defines postprocess_buffer

[[shaders.region]]
output = "DP-1" # omit to apply this rectangle on each output
preset = "reading"
x = 20
y = 40
width = 800
height = 600
```

Regions use output-local logical coordinates. They run in declaration order,
followed by the selected output chain, then the global chain. Window effects
run over their already-composited content and backdrop, before their borders.
They retain native clipping and input geometry, work in overview, and run inside
custom transition captures. Native opening/closing fades suspend the persistent
window pass until the fade ends. Isolated toplevel captures use only that
window's surfaces, without the desktop backdrop, decorations or light spill.

Only adaptive and blueglow use a cropped cursor region (130 logical pixels).
Their ring sizes remain physical pixels. Spotlight covers the whole output;
other cursor effects also use full-output histories. Pointer motion damages
both previous and new footprints, including across outputs. Hiding the pointer,
leaving an output or locking the session clears its cursor feedback.

`redraw` accepts `auto`, `on-damage`, and `continuous`. Auto uses executable GLSL
uniforms and feedback capabilities, so comments mentioning time do not animate.
On-damage suppresses output/global idle animation; continuous keeps a selected
global effect updating even if static. Visible animated window effects have their
own activity detection. The shared `appearance.shader_fps` cap applies only to
shader-driven idle frames. Normal animations and client damage retain their cadence.

An active sampling effect requires full composition of its affected output on a
real frame, even when only a cursor rectangle is shaded. Static effects do not
schedule further frames. This conservative implementation preserves arbitrary
sampling and backdrop damage; it does not claim partial direct scanout. Disabling
effects releases histories, capture intermediates and cursor locks and restores
the native rendering path. Each chain instance holds two source textures and
two result textures per pass, plus two textures for each explicit buffer pass.

## Runtime controls

Use `umbriel msg` or the same action text in a keybind:

```sh
umbriel msg 'shader:window window.crt'
umbriel msg 'shader:window toggle'
umbriel msg 'shader:window cycle'
umbriel msg 'shader:output reading DP-1'
umbriel msg 'shader:global cursor.trail'
umbriel msg 'shader:cursor cycle'
umbriel msg 'shader:screen cycle'
umbriel msg 'shader:global off'
umbriel msg 'shader:animation whirlpool'
umbriel msg 'shader:animation cycle'
```

Scopes are `window`, `border`, `output`, `global`, and `animation`. `cursor` and `screen`
alias the global selector but cycle only names beginning with `cursor.` or
`screen.`, preserving the original separate cycle bindings. Operations are a preset
name, `toggle`, `off`, `on`, `default`, or `cycle`. Window targets default to the
focused window; an optional target is the identifier from `umbriel windows --json`.
Output targets default to the preferred output and accept the usual connector or
output identity selector. Global and animation scopes take no target.

Without a pool, cycle order is alphabetical within each preset's declared scope, followed by the
configured default. Window and output runtime selections are independent. Reload
preserves a selection while its name exists; removed names fall back to the
configured default. Runtime selections last for this compositor session. To
persist a choice, set the corresponding TOML default. A named animation pair can
also be selected with `[animation] preset = "whirlpool"`; `animations.toml`
registers animation pairs without selecting one. Animation controls select both
directions and durations together and respect `[animation] enabled = false`.
Turning a pair off restores the default native animation styles and durations,
with custom shader sources disabled.

## Capture and colour

`in_capture = false` is the default for window, region, output and global effects.
Output protocol captures receive a separate unfiltered composition; enabling
inclusion captures the displayed result. Toplevel inclusion applies only its
window preset over its isolated scene. Capture draws never promote display
feedback. Toplevel sources have independent histories. Multiple output capture
consumers can retain earlier frames without changing trail decay or their pixels.
Decorations, lighting and lifecycle effects keep their existing capture policies.
A tool observing the final scanout buffer sees the displayed effects.

Ordinary cursor effects read pointer coordinates and keep hardware cursors.
`reads_cursor = true` forces a software cursor into the input before the global
pass and releases that requirement when disabled. Otherwise the cursor is drawn
after postprocessing.

Postprocess GLSL sees sRGB-compatible values. Linear/HDR composition uses float
intermediates, decodes source samples at that boundary and converts the result
back before the existing output transform. Values are not globally clamped to
SDR. Allocation failure leaves ordinary rendering in place. Physical HDR and
hardware cursor/scanout presentation still require testing on a live display.

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
- `umbriel_time`: monotonic seconds; windows share a clock, output chains start at activation.
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
