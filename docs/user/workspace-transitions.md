# Workspace pair transitions

`scene-v1` presets with `scope = "workspace_pair"` compose a transition between two whole workspaces. Bind them to `animation.workspaces.effect`:

```toml
[animation.workspaces]
enabled = true
duration_ms = 400
effect = "melt"

[effects.preset.melt]
kind = "animation"
interface = "scene-v1"
scope = "workspace_pair"
shader = "/usr/share/umbriel/effects/scene/melt/shader.glsl"
```

The installation prefix may differ. Wipe and iris examples are installed beside melt. Shader paths are relative to the configuration file that declares them. An optional `common_shader` supplies shared GLSL; `[effects.preset.NAME.parameters]` supplies up to 32 named scalar or vector uniforms. `palette = true` enables the existing configured palette.

The fragment shader implements `vec4 transition(vec2 output_uv)`. `umbriel_sample_from(uv)` samples the frozen outgoing workspace; `umbriel_sample_to(uv)` samples the live destination. Both use normalized whole-output coordinates. Samples outside the source return transparent black. Return the outgoing sample at progress zero and the destination at progress one.

The interface supplies `umbriel_progress`, `umbriel_clamped_progress`, `umbriel_linear_progress`, `umbriel_direction`, `umbriel_axis`, `umbriel_random_seed`, `umbriel_output_size`, `umbriel_scale`, `umbriel_time`, `umbriel_viewport`, `umbriel_scene_count`, and `umbriel_role`. Palette presets also receive `umbriel_palette`, `umbriel_palette_count`, and `umbriel_palette_at(t)`. This branch supports the pair scope and fragment stage, with optional common code.

Timed switches retain the requested destination if composition fails. Swipes use the same two sources, support reversal, and settle to the accepted or original workspace. Retargeting starts a new transition from an authoritative workspace. Pointer or touch activation dismisses the transition and consumes its matching release before normal input resumes.

Capture produces separate display and unfiltered images so the existing `effects.in_capture` policy remains effective. Source images, history, and output candidates share per-output and aggregate memory budgets. Unsupported renderer capabilities, exhausted budgets, unavailable sources, active input grabs, session locking, output changes, or renderer loss fall back to native presentation. Reloading or disabling the workspace effect ends an incompatible active transition.

Inspect the current transition and fallback reason with `umbriel effects --json`, under each output's `workspace_transition` entry.
