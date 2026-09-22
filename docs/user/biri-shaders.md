# Biri shader collection

The port provides 53 effect families: four matched lifecycle pairs, four
persistent borders with optional illumination, 31 window effects, four screen
effects and ten cursor effects. The complete inventory and rendering contracts are in [the porting reference](../porting/biri-shaders/README.md).

## Development environment

The repository already supplies `flake.nix` and `nix/devshell.nix`, pinned to
wlroots 0.20.2. Use `nix develop` and then the normal `just` commands. The local
`.envrc` contains `use flake`; enable it with `direnv allow` after reviewing it.
This follows Quixote's project-pin workflow and needs no changes to Quixote's
shared Rust or Qt development shells. Hardware renderer tests need access to
`/dev/dri/renderD*`; isolated compositor checks do not replace your running session.

## Lifecycle presets

Include one of `whirlpool.toml`, `melt.toml`, `ripple.toml`, or `lightning.toml`
from the installed `share/umbriel/shaders/biri` directory. Each preset selects
both directions, with the source's linear curve and 400/500 ms durations
(lightning uses 400/400 ms). See the [asset README](../../examples/shaders/biri/README.md)
for installation, editing, and the explicitly documented source bug fixes.

For Nix, derive paths from your selected package:

```nix
{ config, ... }: {
  programs.umbriel.settings.include.files = [
    "${config.programs.umbriel.package}/share/umbriel/shaders/biri/whirlpool.toml"
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
shader = "shaders/biri/rings/lightning.glsl"
animated = true
speed = 1.0
padding = 24
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
This is an intentional difference from Biri's above-all-windows light layer.
Spill is suppressed during native fades and shader transforms, excluded from
closing snapshots and isolated window captures, and included in output captures.

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

All converted Biri assets retain their original GPL v3 license, installed next
to them. The frozen source snapshot and manifest remain unchanged.

## Window, screen and cursor presets

Register the collection, then select the effects you want. Registration alone
leaves persistent postprocessing off:

```toml
[include]
files = ["shaders/biri/collection.toml"]

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
| Window | 31 presets; see [the complete window inventory](../../examples/shaders/biri/windows.toml). This includes the live Biri fire, weather, rainbow and CVD variants. |
| Screen | `crt`, `grayscale`, `vignette`, `warmtint` |
| Cursor | `adaptive`, `blueglow`, `comet`, `comet-glow`, `rainbow-tunnel`, `rainbow-tunnel-bare`, `ripple`, `shockwave`, `spotlight`, `trail` |

The separate builtins are `grayscale` (Rec.709 coefficients), `invert`,
`saturation` (1.5), and `temperature` (4000 K). `screen.grayscale` retains the
source's `(0.299, 0.587, 0.114)` coefficients. `screen.warmtint` retains its own
RGB multipliers. Temperature is exactly neutral at 6500 K.

Custom ordered chains contain 1–16 passes. A broken or missing pass disables
the entire chain until corrected. No partial chain or stale last-good program
is displayed. Source files use the native 256 KiB, regular-file, nonblank and
NUL-free checks and automatic dependency watching.

```toml
[shaders.preset.reading]
scope = "output" # window, output, global: chooses its cycle list
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

Scopes are `window`, `output`, `global`, and `animation`. `cursor` and `screen`
alias the global selector but cycle only names beginning with `cursor.` or
`screen.`, preserving the original separate cycle bindings. Operations are a preset
name, `toggle`, `off`, `on`, `default`, or `cycle`. Window targets default to the
focused window; an optional target is the identifier from `umbriel windows --json`.
Output targets default to the preferred output and accept the usual connector or
output identity selector. Global and animation scopes take no target.

Cycle order is alphabetical within each preset's declared scope, followed by the
configured default. Window and output runtime selections are independent. Reload
preserves a selection while its name exists; removed names fall back to the
configured default. Runtime selections last for this compositor session. To
persist a choice, set the corresponding TOML default. A named animation pair can
also be selected with `[animation] preset = "whirlpool"`; `animations.toml`
registers the four pairs without selecting one. Animation controls select both
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

The converted assets preserve the original editable constants. Descending
`smoothstep` edges are expressed through a defined helper, removing source GLSL
undefined behaviour while retaining the intended reversed falloff.
