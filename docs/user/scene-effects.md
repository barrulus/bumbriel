# Scene effects

Scene presets can draw a whole workspace transition, a selectable workspace
presentation, or the windows participating in an opening or closing event. They
use `interface = "scene-v1"`. Omitting `interface` keeps the existing animation,
border, window, screen and cursor shader interface.

## Select a bundled preset

The editable bundles install under `share/umbriel/effects/scene/`. Include a
bundle and select its name; including a file alone does not activate it.

```toml
[include]
files = [
  "/usr/share/umbriel/effects/scene/melt/effect.toml",
  "/usr/share/umbriel/effects/scene/carousel/effect.toml",
  "/usr/share/umbriel/effects/scene/water/effect.toml",
]

[animation.workspaces]
effect = "melt"

[workspace_presentation]
effect = "carousel"
framing = "viewport"

[animation.windows_in]
effect = "water"

[animation.windows_out]
effect = "water"
```

Append to existing tables instead of declaring the same TOML table twice. The
installation prefix may differ from `/usr`. An empty selector disables that
binding. Native event duration and easing still own opening, closing and
workspace-switch timelines.

| Preset | Scope | Editable stages |
| --- | --- | --- |
| `melt` | `workspace_pair` | Outgoing-image drip distance and shape in `shader.glsl`; destination stays intact. |
| `wipe` | `workspace_pair` | Directional reveal boundary in `shader.glsl`. |
| `iris` | `workspace_pair` | Aspect-correct circular reveal in `shader.glsl`. |
| `carousel` | `workspace_set` | Perspective, orbit and spacing in `shader.vert`; face appearance in `shader.frag`. |
| `water` | `window_scene` | Waves and formation in vertex/fragment stages; output-wide ripple tint in `composite.glsl`. |
| `portal` | `window_scene` | Neighbour displacement, target aperture and output-wide ring in separate stages. |

Workspace presentation uses `workspace-presentation-enter`, `-next`, `-previous`,
`-select`, `-accept` and `-cancel` actions. `-select` accepts the normal workspace
selector. `framing = "viewport"` retains each workspace's normal viewport;
`"fit_all"` fits workspace content into an equal-size face while shared output
layers keep their normal framing. The selected face returns to native quality
before ordinary presentation resumes. Carousel navigation wraps in both directions,
independently of the output’s `cyclic_workspaces` setting.

Click a visible window in the carousel to select its workspace and focus that
window after native presentation returns. The selecting press and release are
consumed, so they do not activate a control inside the application. Clicking
face background accepts that workspace with its remembered focus.

Picking follows the authored vertex geometry, depth and the fragment's sampled
source coordinates. A fragment must sample its item once at the clicked pixel;
final composites and ambiguous multiple-source samples cannot be picked. Those
clicks leave presentation open, and keyboard or swipe selection remains available.

## Copy and edit a bundle

Copy the complete preset directory into your configuration directory and include
its `effect.toml`. Stage paths resolve relative to the declaring TOML file.
Rename `[effects.preset.<name>]` when keeping both the original and edited preset.
The compositor watches every declared stage. A broken or missing stage rejects
the whole candidate; an active transaction retains its complete earlier bundle.
A later transaction can use the repaired version.

The vertex file controls geometry; the fragment file controls each item; the
optional composite controls the complete output after those items are drawn.
For example, increase water's `amplitude` in `shader.vert` to change displacement,
then change the tint vector in `composite.glsl` to change appearance independently.
Keep displacement and tint multiplied by `water_envelope()`: that analytic
`4*p*(1-p)` envelope makes them disappear at both native endpoints. No compositor
source change is needed for either edit.

To expose a value in TOML, declare a matching uniform in a stage or in the common
file and add a parameter:

```glsl
uniform float wave_height;
// In transition_vertex, instead of the example's fixed amplitude:
// float amplitude = water_envelope() * wave_height;
```

```toml
[effects.preset.my_water.parameters]
wave_height = 12.0
```

There may be at most 32 parameters, each a finite number or an array of 1–4 finite
numbers. A scalar binds `float`; arrays bind the corresponding float/vector
uniform. Names are at most 31 characters. GLSL keywords, entry-point names,
`umbriel_`, `gl_`, `_fx_` prefixes and double underscores are reserved. Declare
uniforms yourself; the parameter table supplies values, not GLSL declarations.
Each file is limited to 256 KiB and the complete bundle to 512 KiB. GPU uniform,
texture and memory limits can reject a bundle or source before presentation.

## Entry points and coordinates

The wrapper supplies precision declarations, `main`, attributes and varyings.
Do not declare them in an authored stage. `common_shader` is prepended to each
stage, so it must be valid in both vertex and fragment contexts.

```glsl
// workspace_pair: required shader
vec4 transition(vec2 output_uv);
// umbriel_sample_from(uv), umbriel_sample_to(uv)

// workspace_set and window_scene: required vertex_shader and shader
vec4 transition_vertex(vec2 mesh_uv);
vec4 transition_fragment(vec2 item_uv, vec2 output_uv);
// umbriel_sample_item(item_uv)

// Optional composite_shader for the geometry profiles
vec4 transition_composite(vec2 output_uv);
// umbriel_sample_composed(output_uv)
```

UV coordinates start at the top left. Samplers return transparent outside the
input, account for the source's orientation, and retain perspective-correct item
UVs. Vertex results are homogeneous clip coordinates, including W. For an
undeformed item:

```glsl
vec4 transition_vertex(vec2 uv) {
    vec2 point = umbriel_capture_extent.xy + uv * umbriel_capture_extent.zw;
    return vec4(point / umbriel_output_size * 2.0 - 1.0, 0.0, 1.0);
}
```

`output_uv` is the actual rendered output location, not an interpolated position
that bends with vertex W. Boxes use output-local logical pixels. Output scale
and transform are handled by the renderer; do not rotate the returned position
or multiply the vertex position by output scale again.

Colors are premultiplied RGBA in the current composition's value space. Managed
outputs use linear working values; ordinary unmanaged SDR retains native encoded
values, including ten-bit outputs stored in FP16 intermediates. FP16 storage
alone does not mean linear values. Sampled colors already contain unrelated
native/client opacity: do not multiply by `umbriel_native_opacity` again. Preserve
premultiplication when changing alpha, and avoid clamping HDR values to 1.0.

## Frame and item inputs

| Input | GLSL type | Meaning |
| --- | --- | --- |
| `umbriel_output_size`, `umbriel_scale` | `vec2`, `float` | Logical output dimensions and device scale. |
| `umbriel_progress`, `umbriel_linear_progress` | `float` | Eased and linear lifecycle clocks; `umbriel_clamped_progress` clamps the eased value to 0–1. |
| `umbriel_direction` | `float` | Navigation sign for a workspace pair; +1 opening and −1 closing for window events. |
| `umbriel_axis` | `vec2` | Workspace navigation axis. |
| `umbriel_random_seed` | `vec4` | Held transaction seed; use it for repeatable analytic variation. |
| `umbriel_time` | `float` | Effect time; reading it requests ongoing effect frames. |
| `umbriel_navigation_position`, `umbriel_navigation_velocity` | `float` | Continuous workspace-set navigation state. |
| `umbriel_scene_count` | `int` | Workspace-set faces, two for a pair, or window participant owners. |
| `umbriel_role` | `int` | 0 display, 1 unfiltered capture; both use one coherent frame input snapshot. |
| `umbriel_target_token` | `int` | Triggering window's transaction-local token; zero when absent. |
| `umbriel_item_token`, `umbriel_item_ordinal` | `int` | Shared owner identity and native draw order. Zero token denotes a static band. |
| `umbriel_item_kind` | `int` | 0 face, 1 content, 2 shadow, 3 border, 4 emission, 5 static band. |
| `umbriel_current_box`, `umbriel_source_box`, `umbriel_destination_box` | `vec4` | Position and size `(x,y,width,height)` for current/native start/native destination geometry. |
| `umbriel_motion_progress`, `umbriel_linear_motion_progress` | `float` | Independent native neighbour-motion clocks, distinct from the event clock. |
| `umbriel_capture_extent`, `umbriel_content_bounds`, `umbriel_coverage_box` | `vec4` | Captured canvas, content bounds and conservative visual coverage. |
| `umbriel_viewport`, `umbriel_framing` | `vec4`, `int` | Normal viewport; framing 0 viewport or 1 fit-all. |
| `umbriel_framing_transform` | `vec4` | Exact source logical point → face-local point: `point * xy + zw`. |
| `umbriel_native_opacity` | `float` | Informational native opacity, already present in sampled content. |

Window content, shadow and emission companions share their owner's token and
native geometry. Their capture canvases can differ because of transparent padding
and effect halos. Position sampled UVs using `umbriel_capture_extent`; use
`umbriel_current_box` to anchor deformation and formation to the native owner. A shadow item samples an opaque geometry mask with native corners,
independent of a translucent client's pixels. Its authored formation/deformation
feeds the native shadow kernel. An emission item samples raw border emission;
its authored deformation runs before the configured light threshold, blur and
screen blend. Native endpoint companions converge to the ordinary presentation.
Do not treat these inputs as a second copy of the window's content or apply the
light kernel in authored GLSL.

Target sources bypass only the lifecycle stage being replaced. Other opacity,
client subsurfaces, decoration and admitted persistent effects remain in the
source. Neighbours keep their own native geometry and deadlines. Backdrop-dependent
in-place shaders, blur participants and feedback shaders using previous-frame
input are excluded from `window_scene` admission. Such an event continues
natively with its selected effects intact. Workspace pair/set sources retain
independent feedback histories and are not subject to this window restriction. A
presentation never extends closing content's native lifetime. Closing targets
follow the native retained-copy rule and emit no border light; surviving windows
keep their admitted illumination.

Ordinary reflow follows the current native stacking order and preserves each
window's independent resize crossfade. A new overlapping opening or closing event
cancels the active window scene immediately to current native geometry and
opacity. Native clocks continue toward their original deadlines; the cancelled
effect is not queued or replayed. This handoff can be visibly discontinuous.
Pointer or touch dismissal consumes the complete first input sequence and waits
for a successful native frame before normal input resumes.

## Audio, palette and endpoints

Add `audio = "desktop"` to a scene preset to use an explicitly configured named
source, as described in [audio inputs](audio-effects.md). Without a source, audio
inputs are exact zero and the bundled presets remain usable. The examples use
`umbriel_audio_level()` and `umbriel_audio_envelope()` only inside endpoint-vanishing
terms, so audio changes the intermediate shape without moving the destination.
The same latched audio input is used across faces, companions and capture roles;
source acquisition does not consume it independently for each face.

`palette = true` supplies four colors from the configured palette through
`umbriel_palette_at(t)`; without it, palette inputs are zero. Audio and palette
change appearance, not workspace selection or lifecycle deadlines.

For reversible motion, derive geometry from progress, held seed and held input
instead of accumulating per-frame positions. Explicitly return the original
source at pair progress 0 and the intact destination at 1. Window examples use
`direction` to distinguish forming from disappearing, while all residual
neighbour displacement and output-wide shading vanish at completion.
