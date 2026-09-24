# Barrulus shaders

Editable GLSL for lifecycle animations, content, paired borders, screens and cursors.

`collection.toml` registers content, screen, overlay and lifecycle definitions.
`windows.toml` registers only content; `animations.toml` registers lifecycle effects.
`choices.toml` adds named border definitions and favourites. Registration selects nothing.

```toml
[include]
files = ["shaders/barrulus/collection.toml", "shaders/barrulus/choices.toml"]

[appearance]
effects = ["whirlpool", "cursor.comet", "lightning"]
border_width = 6
outer_border_width = 0

[[window_rule]]
match.app_id = "^foot$"
effects = ["window.parchment-dark", "terminals"]
```

Individual TOML files register one definition and can be included independently.
Paths resolve relative to their declaring file. Edit GLSL or typed pass parameters
and reload; active transitions retain their starting generation. Invalid changes
retain the working configuration. The lightning lifecycle name is `lightning-melt`.

Use an absolute path under `share/umbriel/shaders/barrulus` for installed assets.
[Configuration and authoring](../../../docs/user/barrulus-shaders.md) covers all
scopes, shader interfaces, choices, runtime actions and inspection. Pointer-driven
[Jelly and Taffy](../../effects/drag.toml) use reloadable CPU physics parameters.
