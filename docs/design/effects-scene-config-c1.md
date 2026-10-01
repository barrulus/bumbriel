# Typed scene configuration checkpoint

The configuration and cache can load `interface = "scene-v1"` presets while
runtime integration and acceptance continue. This checkpoint is not a stable
shader ABI release. The private descriptor remains in UmbrielFX's internal
headers; the implementation plan's rendering, lifecycle and hardware gates
still decide when authored profiles are ready to ship.

Omitting `interface` preserves the legacy shader path and all 13 animation
slots. Scene presets require `kind = "animation"` and one explicit scope:

| Scope | Compatible binding | Required stages |
| --- | --- | --- |
| `workspace_pair` | `animation.workspaces.effect` | `shader` |
| `window_scene` | `animation.windows_in.effect`, `animation.windows_out.effect` | `vertex_shader`, `shader` |
| `workspace_set` | `workspace_presentation.effect` | `vertex_shader`, `shader` |

All scopes allow `common_shader`; window and workspace-set profiles additionally
allow `composite_shader`. Stage paths resolve relative to the file declaring the
preset, including included configuration files. Every declared stage stays
watched when another stage is absent or invalid. A bad stage or parameter rejects
the complete candidate bundle; an existing in-flight bundle owns its entire old
version until the composition retires it. New transactions see readiness failure
until repair. The cache compiles only referenced roots; inspection never compiles
or starts audio providers.

`parameters` is a table of at most 32 finite scalar or vec2/vec3/vec4 values.
Names must be valid nonreserved GLSL identifiers of at most 31 characters.
Each stage is bounded to 256 KiB and the complete bundle to 512 KiB. Unknown
interfaces, incompatible scope bindings and legacy-only stage/preset keys produce
configuration diagnostics instead of selecting a different shader interface.

The presentation binding is independent of the native animation enable flag:

```toml
[workspace_presentation]
effect = "carousel" # empty disables the mode
framing = "viewport" # or "fit_all"
```

`palette = true` fills the frame from the same current four-color palette used
by legacy effects. Disabled palette inputs are exact zero. The named `audio`
source uses the existing output composition latch; declaring or inspecting a
scene preset acquires no source. The active scene transaction owns one demand
lease and the final output submission alone acknowledges its input revision.

Evidence: parser tests cover typed forward references, include-relative stages,
atomic missing-stage repair, incompatible bindings, empty presentation defaults
and framing validation. `effect/scene_registry` exercises referenced-only GPU
compilation, common-stage compile failure and repair, missing-file recovery,
presentation root release and legacy cache independence. These checks establish
configuration/cache behavior, not complete workspace/window runtime acceptance.
