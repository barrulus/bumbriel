# Barrulus shaders

Editable GLSL shaders for Umbriel, with presets for animations, window content,
borders, screens and cursors.

| Directory | Effects |
| --- | --- |
| `animations/` | Opening, closing, movement and resize effects |
| `rings/` | Persistent borders with optional illumination and inward overlays |
| `window/` | Window-content effects |
| `screen/` | Screen effects |
| `cursor/` | Cursor effects |

`collection.toml` registers the postprocess presets and animation pairs without
enabling an effect. `windows.toml` registers window presets, and `animations.toml`
registers animation pairs. `pools.toml` supplies ordered window favourites and
rotating terminal rings, including paired `neon-bleed` and `portal-lava` effects;
include it after `windows.toml`. Inward ring overlays render over the selected
window-content effect and cycle with their external border.

See [configuration and authoring](../../../docs/user/barrulus-shaders.md) for shader
settings, runtime controls and pools.

## Using a preset

Copy this directory into your configuration's `shaders/` directory, or use an
absolute path to an installed preset under `share/umbriel/shaders/barrulus`.

Include a matched animation pair to enable it:

```toml
[include]
files = ["shaders/barrulus/animations/whirlpool.toml"]
```

The animation presets select opening and closing effects together. Edit their
TOML files to adjust duration and curves, or edit the GLSL files to tune the
visuals. File watching reloads edits; active transitions retain their current
program.

`animations/reveal.glsl` reveals or hides windows and layers and can animate
scratchpad show/hide. `animations/squash.glsl` adds compression and settling during
window movement and resizing. Assign these directly to an animation event:

```toml
[animation.windows_in]
shader = "shaders/barrulus/animations/reveal.glsl"

[animation.windows_move]
shader = "shaders/barrulus/animations/squash.glsl"
```

Remove an event's shader setting to restore its native animation, or set its
`enabled = false` to disable that animation. `[animation] enabled = false`
disables animations globally.

Animation shaders include the window border in their target. Sampling coordinates
are normalized to that target, and out-of-bounds samples are transparent. Window
geometry and input coordinates remain unchanged.
