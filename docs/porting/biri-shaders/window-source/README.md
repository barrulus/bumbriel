# Live window shader source snapshot

Captured from `~/.config/biri/global-shaders/window/` on 2026-09-21.
These 31 sources include 21 shaders absent from the original repository bundle
and an updated pixel-mosaic shader. `manifest.json` records source paths and hashes.
The original frozen snapshot and its manifest remain unchanged.

Nine unchanged shaders reuse their existing Umbriel ports. The other 22 are
translated to native `postprocess` and `umbriel_*` names; reversed smoothstep
edges use the same defined falloff as the existing ports. Shader tuning constants
are preserved. The reference extractor uses these originals for GPU comparisons.

These Biri assets retain the GPL v3 license in `../original/LICENSE`.
