# Working configuration and shader library

This library was imported from Barrulus's live Umbriel configuration on
24 September 2026. It includes the desktop configuration, all current GLSL
sources, shader generators, and screenshot and recording helpers. Keep new
effects and their definitions here as the collection grows. The packaged
[Barrulus presets](../../examples/shaders/barrulus/README.md) remain a separate
collection; this directory is a complete working configuration.

**Effects define appearance. Choices group effects. The config selects effects.**

| File | Purpose |
| --- | --- |
| [config.toml](config.toml) | Entry point: includes, application rules, effect assignments and shortcuts |
| [defaults.toml](defaults.toml) | Local snapshot of the baseline desktop configuration |
| [effects.toml](effects.toml) | Named outer rings, paired inner overlays and independent window effects |
| [choices.toml](choices.toml) | Ordered lists of border and window effects |
| [elastic.toml](elastic.toml) | Elastic opening, closing, animated movement and resize settings |
| [SHADER-USAGE.md](SHADER-USAGE.md) | Inventory of every GLSL file and its current configuration references |
| [shaders/](shaders/) | GLSL sources and the Python generators for paired effects |
| [screengrab.py](screengrab.py), [screen-record.py](screen-record.py) | Optional capture helpers used by the keyboard shortcuts |

## Try the configuration

Use this fork's Umbriel build with named effects and paired border overlays.
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

An **outer ring** uses `effects.NAME.border.outer` and a `ring_color` pass.
Padding reserves drawing space and optional light illuminates nearby pixels.
An **inner overlay** uses `effects.NAME.border.inner`, clipped to the rounded
client box above the independent content effect. Both leaves share one name:

```toml
[effects.neon-bleed.border.outer]
padding = 14
passes = [{shader = "shaders/rings/neon-bleed.glsl"}]

[effects.neon-bleed.border.inner]
passes = [{shader = "shaders/window/neon-bleed-overlay.glsl"}]
```

| Border preset | Outer shader in `shaders/rings/` | Inner shader in `shaders/window/` |
| --- | --- | --- |
| `flowering-vine` | `flowering-vine.glsl` | `flowering-vine-overlay.glsl` |
| `rainbow` | `rainbow-ripple.glsl` | `rainbow-ripple-overlay.glsl` |
| `neon-bleed` | `neon-bleed.glsl` | `neon-bleed-overlay.glsl` |
| `portal-lava` | `portal-lava.glsl` | `portal-lava-overlay.glsl` |
| `faerie-magic` | `faerie-magic.glsl` | `faerie-magic-overlay.glsl` |

Both halves follow focus by default and can be selected or toggled together.
External-only border definitions explicitly disable their inner leaf. Each leaf
has independent `animated`, `speed` and `palette` settings. Inner effects follow
`render.effects.in_capture` and remain inside opening and closing captures.
Content effects occupy `effects.NAME.content` and cycle independently.

## Choices and shortcuts

The `desktop` choice assigns flowering-vine by default. `terminals` allocates
eleven paired borders across Foot, Ghostty and Kitty with `unused_first`.
`favourites` supplies sixteen content effects for explicit `round_robin` cycling.

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

Edit `choose` arrays in `choices.toml` to add, remove or reorder candidates.
Definitions live in `effects.toml`; selectors and bindings live in `config.toml`.
Each border candidate defines both leaves so changing it clears its predecessor's
complete contribution. [The inventory](SHADER-USAGE.md) identifies unused sources.

## Flap board

`window.flap-board` turns little rectangular tiles around horizontal hinges in
travelling waves. Moving faces show inverted live window pixels; each tile
returns completely to the original image after its turn, with no lasting grid
or tint. Window transparency is preserved, and paired border overlays still
draw above it.

With this configuration loaded, select it on the focused window:

```sh
umbriel msg 'effect:window set window.flap-board --scope content'
# Restore the unfiltered window:
umbriel msg 'effect:window off --scope content'
```

It is also in the Mod+S cycle. Adjust `FLAP_SIZE`, `FLAP_DURATION`,
`FLAP_PERIOD`, `FLAP_GAP` and `FLAP_SPEED` at the top of
[flap-board.glsl](shaders/window/flap-board.glsl). Sizes are in logical pixels;
the default is 38 × 26, with a 1.15-second turn every 5.5 seconds per tile.

## Elastic movement and lifecycle animations

[elastic.toml](elastic.toml) is included after the defaults. It defines an elastic
sheet deformation for opening, closing, animated movement and resize, including
layout changes, maximize and restore. Opening grows and bends into place;
closing reverses that motion. Movement settles back to the unchanged window.
The shaders use event progress, so idle windows do not wobble continuously.

The named effect is also selectable at runtime:

```sh
umbriel msg 'effect:window set elastic --scope open,close,move,resize'
```

`WOBBLE_STRENGTH` in each file under `shaders/animations/` controls the bend.
Durations are in `elastic.toml`: 620 ms opening, 460 ms closing, and 650 ms for
movement. Remove the `elastic.toml` include to return to the baseline settings;
remove `elastic` from the appearance selector and, if selected at runtime, run `umbriel msg 'effect:window default --scope open,close,move,resize'`.

Drag physics uses a CPU spring simulation and an inverse-sampling deformation shader.
`animation.windows_move.drag_physics = true` enables native Jelly. Selecting a named
`[effects.NAME.drag]` preset enables it directly, without that switch. The grab point
stays pinned, drawing bounds expand with deformation, and the window settles back
to its original rectangle after release.

### Taffy pointer stretch

[`taffy.toml`](taffy.toml) selects a named drag preset with weaker lower springs,
greater pointer lag, reduced damping, and movement-driven downward pull. Edit its
coefficients and reload the configuration to change the simulation without restarting.
The old `wobble` and temporary `wobble_style` settings are rejected.

## Liquid glass

`window.liquid-glass` adds frost across the captured pane, a milky tint, a
refractive bevel, slight colour separation, and slowly shifting cyan/pink rim
reflections. It runs entirely in the window shader, with compositor backdrop
blur disabled. Some original detail is retained, but text also softens because
the shader receives the combined window image. It preserves captured alpha and
works beneath paired inner border overlays.

```sh
umbriel msg 'effect:window set window.liquid-glass --scope content'
# Restore the unfiltered window:
umbriel msg 'effect:window off --scope content'
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
