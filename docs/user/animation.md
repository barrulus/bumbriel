# Animation

Animation settings live under `[animation]`. The top-level values provide
defaults, and each event can override them.

```toml
[animation]
enabled = true
duration_ms = 250
curve = "easeout"

[animation.windows_in]
enabled = true
curve = "spring:1,900"
style = "popin"
scale = 0.5

[animation.windows_out]
enabled = true
curve = "spring:1,1400"
style = "popin"
scale = 0.8

[animation.windows_move]
enabled = true
curve = "spring:1,900"

[animation.workspaces]
enabled = true
curve = "spring:1,800"

[animation.overview]
enabled = true
curve = "spring:1,800"
workspace_curve = "spring:1,1000"

[animation.scratchpad]
enabled = true
curve = "spring:1,800"
dim = 0.8
blur = false
scale = 0.0
maximize = false
fullscreen = false

[animation.border]
enabled = true
curve = "spring:1,900"

[animation.dim_unfocused]
enabled = false
dim = 0.0

[animation.layers]
enabled = false
```

## Defaults

| Key | Default | Description |
| --- | --- | --- |
| `enabled` | `true` | Master switch for every transition. |
| `duration_ms` | `250` | Default duration for non-spring curves. |
| `curve` | `"easeout"` | Default easing curve. |

Each event also accepts `enabled`, `duration_ms`, and `curve`. A spring curve
chooses its own duration, so `duration_ms` has no effect on that event.

## Drag physics

Set `drag_physics = true` in `[animation.windows_move]` to make a dragged window behave
like an elastic sheet. The grab point follows the pointer; the rest trails your
movement, responds to reversals and repeated shaking, and settles after release.
Grabbing near a corner produces an asymmetric bend. This is independent of the
custom movement shader and its duration or curve; both animation enable switches
still apply. It is disabled by default.

```toml
[animation.windows_move]
drag_physics = true
```

Select a named drag preset for a different spring response. Selecting one enables
pointer deformation without `drag_physics = true`; the native animation master and
`windows_move.enabled` switches still apply. The custom-effect master switch gates
the selected preset. Without a selected custom preset, `drag_physics = true` retains jelly.

```toml
[appearance]
effects = ["taffy"]

[effects.taffy.drag]
stiffness = 36
coupling = 48
damping = 4.8
pointer_response = 2
stiffness_gradient = -0.55
lag_gradient = 0.9
downward_pull = 18
motion_gain = 8
decay = 2.8
```

Taffy's bottom trails further, droops into a curved pouch, and swings into a stretch.
Motion builds the downward pull, which fades at rest. These are CPU simulation
parameters, separate from timeline shaders and their durations or curves. Reloads
update the coefficients even during a grab, preserving displacement and pinning.
The temporary `wobble_style` key has been removed.

For broad lateral sway with less local wobble and no added downward stretch,
select a uniform, more strongly coupled preset:

```toml
[appearance]
effects = ["lateral-wobble"]

[effects.lateral-wobble.drag]
stiffness = 18
coupling = 170
damping = 8
pointer_response = 2.8
stiffness_gradient = 0
lag_gradient = 0
downward_pull = 0
```

The renderer bends one smooth bicubic surface across the whole window. The
cursor remains pinned at the exact grab position while the other parts of the
sheet trail and respond to direction changes. A horizontal drag with no
downward pull adds no vertical displacement.

| Drag parameter | Range | Jelly default |
| --- | --- | --- |
| `stiffness` | 1–1000 | 36 |
| `coupling` | 0–500 | 100 |
| `damping` | 0.5–60 | 6.5 |
| `pointer_response` | 0–10 | 2 |
| `stiffness_gradient` | −0.9–1 | 0 |
| `lag_gradient` | −0.9–4 | 0 |
| `downward_pull` | 0–50 | 0 |
| `motion_gain` | 0–32 | 8 |
| `decay` | 0.1–30 | 2.8 |

Gradients vary linearly from top to bottom: a stiffness gradient of −0.55 gives
the bottom 45% of the top's stiffness; a lag gradient of 0.9 gives it 190% of the
pointer response. Motion gain loads a bounded 0–1 pull reservoir from normalized
pointer travel. Decay is its exponential decay rate per second. Downward pull
scales acceleration by window height and distance below the grab point.

`drag` is window-only and accepts simulation parameters instead of shader passes.
It supports the same named selectors, choices, and disabled-leaf replacement rules.
Native geometry, input, inverse sampling, and rendering bounds remain unchanged.

The effect covers compositor and client-requested window moves, including tiles
after they detach for dragging. It does not change input geometry or client
buffers. Mouse resizing and overview-card dragging retain their existing behavior.
Disabling it during a drag restores the normal presentation immediately.

## Event tables

The [Barrulus collection](barrulus-shaders.md) provides matched open/close presets and
persistent active-border shaders. Persistent rings have their own settings and
continue after the `animation.border` colour transition ends.

| Table | Additional fields | Transition |
| --- | --- | --- |
| `[animation.windows_in]` | `style`, `scale` | Window opening |
| `[animation.windows_out]` | `style`, `scale` | Window closing |
| `[animation.windows_move]` | `drag_physics` | Move, resize, reflow, maximize, and restore |
| `[animation.workspaces]` | none | Workspace switching |
| `[animation.overview]` | `workspace_curve` | Overview opening, closing, and filmstrip movement |
| `[animation.scratchpad]` | `dim`, `blur`, `scale`, `maximize`, `fullscreen` | Scratchpad windows and backdrop |
| `[animation.border]` | none | Focus-border color |
| `[animation.dim_unfocused]` | `dim` | Unfocused-window opacity |
| `[animation.layers]` | none | Layer-shell map and unmap |

`windows_in` accepts `popin`, `zoom`, `slide`, `fade`, or `none`.
`windows_out` accepts `fade`, `slide`, `popin`, or `zoom`. `scale` applies to
`popin`: an opening window grows from it to full size, and a closing window
shrinks toward it, while both fade.

`animation.overview.workspace_curve` controls filmstrip movement after wheel,
keyboard, and touchpad navigation.

Scratchpad `dim` and `blur` remain active without a fade when animation is
disabled. `scale`, `maximize`, and `fullscreen` set the presentation applied
when a window enters a scratchpad.

## Curves

Use a built-in curve such as `linear`, `ease`, `easeout`, `snappy`, `bounce`, or
`elastic`; a cubic Bézier string; or a spring:

```toml
curve = "0.05,0.9,0.1,1.0"
# Or use a spring:
# curve = "spring:1,1000"
```

For Bézier curves, x coordinates must be between 0 and 1. Spring syntax is
`spring:<damping>,<stiffness>`:

- Damping below 1 overshoots.
- Damping 1 reaches the target without overshoot.
- Damping above 1 approaches more slowly.
- Greater stiffness settles faster.

Register reusable names when several events share a curve:

```toml
[animation.beziers]
myBezier = [0.05, 0.9, 0.1, 1.05]

[animation.springs]
myBounce = { damping = 0.5, stiffness = 200 }
```

Then set `curve = "myBezier"` or `curve = "myBounce"`.

## Custom GLSL shaders

Every animation event can use a custom fragment shader. The event's enabled
state and curve still control its timeline.

Select named lifecycle and movement leaves while keeping native enables and clocks:

```toml
[appearance]
effects = ["elastic"]

[effects.elastic.open]
duration_ms = 300
curve = "easeout"
passes = [{shader = "/usr/share/umbriel/shaders/barrulus/animations/reveal.glsl"}]

[effects.elastic.close]
duration_ms = 250
curve = "easeout"
passes = [{shader = "/usr/share/umbriel/shaders/barrulus/animations/reveal.glsl"}]

[effects.elastic.move]
passes = [{shader = "/usr/share/umbriel/shaders/barrulus/animations/squash.glsl"}]

[effects.elastic.resize]
passes = [{shader = "/usr/share/umbriel/shaders/barrulus/animations/squash.glsl"}]
```

Adjust `/usr/share` for the package prefix. Relative paths resolve from the file
containing the pass. Source edits reload automatically; active events retain their
starting generation, while invalid generations leave the working configuration
in place. See the [effects reference](barrulus-shaders.md) for choices, parameters,
runtime selection, inspection and capture policy.

For Nix, derive each `passes` entry's `shader` path from
`${config.programs.umbriel.package}/share/umbriel/shaders/barrulus/animations/`.

The `shader` value must name a regular GLSL file smaller than 256 KiB. Inline
GLSL and recursive includes are not supported.

### Shader interface

Write GLSL ES 1.00 with this entry point. Do not add a `#version` declaration
or your own `main`:

```glsl
vec4 animation(vec2 uv) {
    return umbriel_sample(uv);
}
```

Umbriel supplies `main`, precision declarations, and these commonly used
values:

| Name | Meaning |
| --- | --- |
| `uv` | Normalized target coordinates |
| `umbriel_sample(vec2 uv)` | Sample the rendered target |
| `umbriel_sample_previous(vec2 uv)` | Sample this target's previous shader result |
| `umbriel_size` | Target width and height in logical units |
| `umbriel_progress` | Eased progress, including overshoot |
| `umbriel_clamped_progress` | Eased progress clamped to 0 through 1 |
| `umbriel_linear_progress` | Progress before easing |
| `umbriel_direction` | `1` for entering and `-1` for leaving |
| `umbriel_random_seed` | Four stable random values for this transition |

Return premultiplied RGBA. Preserve sampled alpha when modifying colors so a
shader does not fill transparent parts of its target.

`umbriel_sample_previous` enables feedback and allocates two additional buffers
for the active target. Avoid it when an effect does not need feedback,
especially for workspace and overview shaders.

### Targets and composition

Window shaders process the window, subsurfaces, and border as one target.
Workspace and overview shaders process their corresponding scene trees.
Shaders change presentation only; they do not affect layout, client sizes,
input coordinates, or focus.

Window shadows follow the alpha shape produced by window and border shaders.
The compositor still applies configured color, softness, and offset.

### Reload and failures

Shaders compile on startup or configuration reload. A missing source or compile
failure produces a diagnostic and falls back to the built-in effect. Compiler
details appear in the Umbriel log.

Custom shaders are trusted local GPU code. Expensive or nonterminating shaders
can stall the driver, and active effects disable direct scanout. Prefer short,
inexpensive effects.
