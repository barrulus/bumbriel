# Working configuration and shader library

This library was imported from Barrulus's live Umbriel configuration on
24 September 2026. It includes the desktop configuration, all current GLSL
sources, shader generators, and screenshot and recording helpers. Keep new
effects and their definitions here as the collection grows. The packaged
[Barrulus presets](../../examples/shaders/barrulus/README.md) remain a separate
collection; this directory is a complete working configuration.

**Effects define appearance. Pools group effects. The config assigns pools.**

| File | Purpose |
| --- | --- |
| [config.toml](config.toml) | Entry point: includes, application rules, pool assignments and shortcuts |
| [defaults.toml](defaults.toml) | Local snapshot of the baseline desktop configuration |
| [effects.toml](effects.toml) | Named outer rings, paired inner overlays and independent window effects |
| [pools.toml](pools.toml) | Ordered lists of border and window effects |
| [wobbly.toml](wobbly.toml) | Elastic opening, closing, animated movement and resize settings |
| [SHADER-USAGE.md](SHADER-USAGE.md) | Inventory of every GLSL file and its current configuration references |
| [shaders/](shaders/) | GLSL sources and the Python generators for paired effects |
| [screengrab.py](screengrab.py), [screen-record.py](screen-record.py) | Optional capture helpers used by the keyboard shortcuts |

## Try the configuration

Use this fork's Umbriel build with shader pools and paired border overlays.
From the repository root, validate and start a nested compositor:

```sh
umbriel validate -c "$PWD/docs/examples/config.toml"
UMBRIEL_EXAMPLE_DIR="$PWD/docs/examples" umbriel -c "$PWD/docs/examples/config.toml"
```

Includes and shader paths resolve relative to their declaring TOML file.
`UMBRIEL_EXAMPLE_DIR` locates the capture helpers when running directly from
this checkout. It is used by the example shortcuts, not by Umbriel's config
loader. To use the library as your regular configuration, copy its contents
into `${XDG_CONFIG_HOME:-$HOME/.config}/umbriel/` after saving any configuration
you want to keep. The capture shortcuts use that location when the variable
is unset. Validate the installed configuration with `umbriel validate`.

This is a personal desktop example: application shortcuts use Foot, Ghostty,
VS Code and Kitty; the baseline launcher uses Noctalia. Adjust these to your
installed applications. The original private `foot-rainbow` launcher has been
replaced with `foot --app-id foot-rainbow` so the example needs no external
personal script. Screenshot shortcuts require Python 3, Grim (with `-T`
toplevel capture support), Slurp and Satty. Recording requires
`gpu-screen-recorder` and a working desktop screencast portal. Notifications
use `notify-send` when available.

## Outer rings, inner overlays and window effects

An **outer ring** is a decoration shader from `shaders/rings/`, selected by a
`[shaders.border.NAME]` definition. Its padding provides space outside the
window for the effect. Optional lighting adds illumination around the window.

An **inner overlay** carries the same pattern into the client area. Its border
definition names a second preset with `overlay`; that preset has
`scope = "border"` and uses an adapter from `shaders/window/`. The directory
name does not determine the scope. This layer is clipped to the rounded client
box and draws after any independent window-content effect.

For example, these two definitions select neon's outer and inner halves together:

```toml
[shaders.border.neon-bleed]
enabled = true
animated = true
speed = 1
padding = 14
shader = "shaders/rings/neon-bleed.glsl"
overlay = "ring.neon-bleed"

[shaders.preset."ring.neon-bleed"]
scope = "border"
passes = [{ shader = "shaders/window/neon-bleed-overlay.glsl" }]
```

| Border preset | Outer shader in `shaders/rings/` | Inner shader in `shaders/window/` |
| --- | --- | --- |
| `flowering-vine` | `flowering-vine.glsl` | `flowering-vine-overlay.glsl` |
| `rainbow` | `rainbow-ripple.glsl` | `rainbow-ripple-overlay.glsl` |
| `neon-bleed` | `neon-bleed.glsl` | `neon-bleed-overlay.glsl` |
| `portal-lava` | `portal-lava.glsl` | `portal-lava-overlay.glsl` |
| `faerie-magic` | `faerie-magic.glsl` | `faerie-magic-overlay.glsl` |

Both halves follow border selection, focus and border toggling. Selecting a
ring without an overlay removes the previous inner layer. The inner layer
follows `shaders.in_capture` and is suppressed during lifecycle fades. Border
`speed`, `animated` and `light` settings control the outer shader; the inner
adapter has its own GLSL timing.

An **independent window effect**, such as CRT or parchment, has
`scope = "window"`. It processes window content underneath the inner overlay.
Cycling it leaves the chosen border pair in place. See the
[shader reference](../user/barrulus-shaders.md) for the host APIs and behavior.

## Pools and shortcuts

The `desktop` border pool assigns flowering-vine by default. The `terminals`
pool assigns eleven ring choices to Foot, Ghostty and Kitty, using
`allocation = "unused-first"` to spread choices across windows. The
`favourites` pool contains sixteen independent window effects.

| Shortcut | Action |
| --- | --- |
| Mod+Alt+S | Cycle the focused window's border through `terminals`, including paired inner overlays |
| Mod+Alt+Shift+S | Toggle the border and its paired overlay |
| Mod+Alt+Ctrl+S | Restore the configured border selection |
| Mod+S | Cycle independent window effects through `favourites` |
| Mod+Shift+S | Toggle the independent window effect |
| Mod+Ctrl+S | Select `window.sentient-circuit-v2` directly |
| Print / Ctrl+Print | Capture a region / the active window in Satty |
| Mod+Print / Mod+Shift+Print | Start / stop a portal recording |

Edit the `presets` arrays in `pools.toml` to add, remove or reorder choices.
Define each effect in `effects.toml` first, then assign pools in `config.toml`.
Keep paired inner presets at `scope = "border"` so they stay out of window
effect cycles. Several extra GLSL files are retained as library material but
are not registered; [the inventory](SHADER-USAGE.md) identifies them.

## Flap board

`window.flap-board` turns little rectangular tiles around horizontal hinges in
travelling waves. Moving faces show inverted live window pixels; each tile
returns completely to the original image after its turn, with no lasting grid
or tint. Window transparency is preserved, and paired border overlays still
draw above it.

With this configuration loaded, select it on the focused window:

```sh
umbriel msg 'shader:window window.flap-board'
# Restore the unfiltered window:
umbriel msg 'shader:window off'
```

It is also in the Mod+S cycle. Adjust `FLAP_SIZE`, `FLAP_DURATION`,
`FLAP_PERIOD`, `FLAP_GAP` and `FLAP_SPEED` at the top of
[flap-board.glsl](shaders/window/flap-board.glsl). Sizes are in logical pixels;
the default is 38 × 26, with a 1.15-second turn every 5.5 seconds per tile.

## Wobbly movement and lifecycle animations

[wobbly.toml](wobbly.toml) is included after the defaults. It selects an elastic
sheet deformation for opening, closing, animated movement and resize, including
layout changes, maximize and restore. Opening grows and bends into place;
closing reverses that motion. Movement settles back to the unchanged window.
The shaders use event progress, so idle windows do not wobble continuously.

The open/close pair is also selectable at runtime:

```sh
umbriel msg 'shader:animation wobbly'
```

`WOBBLE_STRENGTH` in each file under `shaders/animations/` controls the bend.
Durations are in `wobbly.toml`: 620 ms opening, 460 ms closing, and 650 ms for
movement. Remove the `wobbly.toml` include to return to the baseline settings;
if you explicitly selected the pair, also run `umbriel msg 'shader:animation default'`.

With a build supporting `animation.windows_move.wobble`, this profile also
enables pointer-driven jelly motion. The grabbed point follows the pointer while
a spring grid bends behind it; reversing direction or shaking the mouse excites
the sheet, which continues settling after release. Grab near a corner for a
stronger asymmetric bend. Set `wobble = false` to disable this part while keeping
the timeline shaders. This requires a compositor rebuild and session restart;
it cannot be added to an older running compositor by reloading GLSL alone.

Pointer wobble runs through the built-in `umbrielfx` shader and expands its draw
area to accommodate the bend. It applies to window moves; direct mouse resizing
and overview-card dragging keep their existing behavior. Input geometry remains
rectangular. The separate timeline shaders reserve room inside their bounds.

## Liquid glass

`window.liquid-glass` adds frost across the captured pane, a milky tint, a
refractive bevel, slight colour separation, and slowly shifting cyan/pink rim
reflections. It runs entirely in the window shader, with compositor backdrop
blur disabled. Some original detail is retained, but text also softens because
the shader receives the combined window image. It preserves captured alpha and
works beneath paired inner border overlays.

```sh
umbriel msg 'shader:window window.liquid-glass'
# Restore the unfiltered window:
umbriel msg 'shader:window off'
```

It is also in the Mod+S cycle. Tune `GLASS_BEVEL`, `GLASS_REFRACTION`,
`GLASS_RADIUS`, `GLASS_REFLECTION`, `GLASS_TINT`, `GLASS_FROST_RADIUS` and
`GLASS_FROST` in
[liquid-glass.glsl](shaders/window/liquid-glass.glsl). Dimensions are logical
pixels; the default client radius of 4 matches this configuration's outer
radius of 10 and border width of 6.

`GLASS_FROST_RADIUS` controls the diffusion's reach (3 logical pixels by
default). `GLASS_FROST` mixes the diffused image with the original: lower it for
clearer lettering or raise it for heavier frost. `GLASS_TINT` controls the milky
finish. These settings apply only while this window shader is selected.

The shader refracts the window composition it receives, including any visible
backdrop already composited into it. Applications need a transparent background
to reveal that backdrop; this effect does not change application opacity or
expose a desktop hidden by opaque content. Isolated window captures do not
include the backdrop.

## Editing paired shaders

The generated adapters currently assume `border_width = 6`,
`outer_border_width = 0`, `corner_radius = 10` (a client radius of 4), and
border animation speed 1. Padding is 14 for neon, portal-lava and rainbow,
and 8 for flowering-vine and faerie-magic. If these settings change, update
the corresponding generator's geometry or timing and regenerate both halves
as needed to keep the seam aligned.

Run these commands from `docs/examples` after editing the canonical sources:

```sh
# Shared liquid geometry and pigments in shaders/bleed/*.glsl:
python3 shaders/generate-bleed-overlays.py
# Ring sources in shaders/rings/:
python3 shaders/generate-ripple-overlay.py
python3 shaders/generate-vine-overlay.py
python3 shaders/generate-magic-overlay.py
umbriel validate -c config.toml
```

The bleed generator produces both outer rings and inner adapters. The other
three generators derive an inner adapter from the corresponding ring source.
Edit these sources instead of the generated outputs. Umbriel watches config
and shader edits; validation checks configuration, while shader compilation
and appearance must be checked in a running compositor.

The imported [edge-seam patch](patches/umbriel-inner-overlay-seam.patch) is kept
for reference; its fix is already present in this repository. Backup archives,
the duplicate shader ZIP, and the old `.before-inner` snapshot are omitted.
