# Biri shaders

Editable GLSL adapted from Barrulus's Biri collection. The eight lifecycle
shaders in this directory run on the native Umbriel animation interface.
The source collection's GPL v3 license is retained in [LICENSE](LICENSE).
These assets are not covered by the compositor's MIT license.

The `rings/` directory contains four persistent border presets; `window/`,
`screen/`, and `cursor/` contain the remaining 45 families (47 GLSL files).
`collection.toml` registers all postprocess presets and four animation pairs
without enabling any effect. `windows.toml` registers only the 31 window presets. See
[configuration and authoring](../../../docs/user/biri-shaders.md).

Include one matched pair in your configuration:

```toml
[include]
files = ["shaders/biri/whirlpool.toml"]
```

Copy this directory beside your configuration's `shaders` directory, or use
an absolute path to the installed preset. Installation places this directory
under `share/umbriel/shaders/biri`.

| Preset | Opening | Closing |
| --- | --- | --- |
| `whirlpool.toml` | 400 ms | 500 ms |
| `melt.toml` | 400 ms | 500 ms |
| `ripple.toml` | 400 ms | 500 ms |
| `lightning.toml` | 400 ms | 400 ms |

All curves are linear. Each TOML file selects both directions. Edit the GLSL
files to tune the effect; file watching reloads them, and existing transitions
retain their current program. Remove the shader settings to restore native
animations, or set each event's `enabled = false` to disable its animation.
A disabled global `[animation]` still disables the presets.

The target includes the window border. Sampling remains normalized to that
target and out-of-bounds pixels are transparent. Window geometry and input
coordinates are unaffected. The host does not apply an additional native fade.

## Deliberate source corrections

The frozen originals are under `docs/porting/biri-shaders/original/` in the
repository and are unchanged. These destination fixes avoid visible jumps:

- Opening endpoints are transparent then the complete source; closing endpoints
  are the complete source then transparent.
- Lightning opening expands its reveal instead of running it backwards. Both
  directions start the noisy edge outside the visible distance range.
- Melt closing moves its melt line downwards from the top instead of beginning
  with almost the entire window hidden.
- Ripple opening expands its reveal far enough to restore every corner.
- Whirlpool closing begins with full edge opacity. Its dissolve and melt's heat
  falloff use ordered `smoothstep` edges, avoiding undefined GLSL behaviour.
- Melt and lightning tint premultiplied input without multiplying its alpha
  twice. Procedural lightning light still has its own alpha, as in the source.

The procedural pattern constants, stable transition seed, displacement strengths,
matched durations and separate open/close functions otherwise follow the source.

The window collection also includes the user-configured Biri effects captured on
2026-09-21. Their original sources and hashes are preserved in
[`window-source`](../../../docs/porting/biri-shaders/window-source/). Matching ports
are retained, pixel-mosaic uses its current tuning, and 21 additional effects
include fire, weather, rainbow, grain and colour-vision variants.
