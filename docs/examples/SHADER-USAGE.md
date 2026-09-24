# Shader inventory

Snapshot: 24 September 2026, based on the TOML files in this directory.

“In use” means reachable through a configured pool or direct shortcut, not
necessarily visible right now. Registered effects can be selected by name.
Unregistered shaders are library sources; add a definition in `effects.toml`
before assigning them. Refresh this inventory when definitions or pools change.

See [the README](README.md) for outer rings, inner overlays, generators and shortcuts.

| Shader file | Status | Reference / reason |
| --- | --- | --- |
| [shaders/bleed/droplets.glsl](shaders/bleed/droplets.glsl) | Generator source | Shared source for the neon-bleed and portal-lava outer/inner pairs |
| [shaders/bleed/neon.glsl](shaders/bleed/neon.glsl) | Generator source | Shared source for the neon-bleed and portal-lava outer/inner pairs |
| [shaders/bleed/portal.glsl](shaders/bleed/portal.glsl) | Generator source | Shared source for the neon-bleed and portal-lava outer/inner pairs |
| [shaders/rings/faerie-magic.glsl](shaders/rings/faerie-magic.glsl) | In use | Border `faerie-magic`; pools: `terminals` |
| [shaders/rings/flowering-vine.glsl](shaders/rings/flowering-vine.glsl) | In use | Border `flowering-vine`; pools: `desktop`, `terminals` |
| [shaders/rings/flowing-water.glsl](shaders/rings/flowing-water.glsl) | In use | Border `flowing-water`; pools: `terminals` |
| [shaders/rings/fuse.glsl](shaders/rings/fuse.glsl) | In use | Border `fuse`; pools: `terminals` |
| [shaders/rings/lightning.glsl](shaders/rings/lightning.glsl) | In use | Border `lightning`; pools: `terminals` |
| [shaders/rings/neon-bleed.glsl](shaders/rings/neon-bleed.glsl) | In use | Border `neon-bleed`; pools: `terminals` |
| [shaders/rings/portal-lava.glsl](shaders/rings/portal-lava.glsl) | In use | Border `portal-lava`; pools: `terminals` |
| [shaders/rings/portal.glsl](shaders/rings/portal.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/rings/pulse.glsl](shaders/rings/pulse.glsl) | In use | Border `pulse`; pools: `terminals` |
| [shaders/rings/rainbow-ripple.glsl](shaders/rings/rainbow-ripple.glsl) | In use | Border `rainbow`; pools: `terminals` |
| [shaders/rings/sentient-runner.glsl](shaders/rings/sentient-runner.glsl) | In use | Border `sentient-runner`; pools: `terminals` |
| [shaders/rings/sentient-spark.glsl](shaders/rings/sentient-spark.glsl) | In use | Border `sentient-spark`; pools: `terminals` |
| [shaders/window/adaptive-text-v3.glsl](shaders/window/adaptive-text-v3.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/adaptive-text-v4.glsl](shaders/window/adaptive-text-v4.glsl) | Registered, outside pools | Preset `window.adaptive-text-v4` |
| [shaders/window/autumn-leaves.glsl](shaders/window/autumn-leaves.glsl) | In use | Preset `window.autumn-leaves`; pools: `favourites` |
| [shaders/window/crt.glsl](shaders/window/crt.glsl) | In use | Preset `window.crt`; pools: `favourites` |
| [shaders/window/cvd-deutan-alphabet.glsl](shaders/window/cvd-deutan-alphabet.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-deutan-combo.glsl](shaders/window/cvd-deutan-combo.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-deutan-drift.glsl](shaders/window/cvd-deutan-drift.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-deutan-oriented.glsl](shaders/window/cvd-deutan-oriented.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-deutan.glsl](shaders/window/cvd-deutan.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-protan.glsl](shaders/window/cvd-protan.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-tritan.glsl](shaders/window/cvd-tritan.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/faerie-magic-overlay.glsl](shaders/window/faerie-magic-overlay.glsl) | In use | Paired inner overlay for `faerie-magic` (`scope = "border"`) |
| [shaders/window/film-grain.glsl](shaders/window/film-grain.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/fire-tendrils.glsl](shaders/window/fire-tendrils.glsl) | In use | Preset `window.fire-tendrils`; pools: `favourites` |
| [shaders/window/fire.glsl](shaders/window/fire.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/fisheye-rgb.glsl](shaders/window/fisheye-rgb.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/flowering-vine-overlay.glsl](shaders/window/flowering-vine-overlay.glsl) | In use | Paired inner overlay for `flowering-vine` (`scope = "border"`) |
| [shaders/window/mercury-sheen.glsl](shaders/window/mercury-sheen.glsl) | In use | Preset `window.mercury-sheen`; pools: `favourites` |
| [shaders/window/neon-bleed-overlay.glsl](shaders/window/neon-bleed-overlay.glsl) | In use | Paired inner overlay for `neon-bleed` (`scope = "border"`) |
| [shaders/window/parchment-dark.glsl](shaders/window/parchment-dark.glsl) | In use | Preset `window.parchment-dark`; pools: `favourites` |
| [shaders/window/parchment.glsl](shaders/window/parchment.glsl) | Registered, outside pools | Preset `window.parchment` |
| [shaders/window/pixel-mosaic.glsl](shaders/window/pixel-mosaic.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/portal-lava-overlay.glsl](shaders/window/portal-lava-overlay.glsl) | In use | Paired inner overlay for `portal-lava` (`scope = "border"`) |
| [shaders/window/prairie-wind.glsl](shaders/window/prairie-wind.glsl) | In use | Preset `window.prairie-wind`; pools: `favourites` |
| [shaders/window/rainbow-radial.glsl](shaders/window/rainbow-radial.glsl) | In use | Preset `window.rainbow-radial`; pools: `favourites` |
| [shaders/window/rainbow-ripple-overlay.glsl](shaders/window/rainbow-ripple-overlay.glsl) | In use | Paired inner overlay for `rainbow` (`scope = "border"`) |
| [shaders/window/rainbow-smoke.glsl](shaders/window/rainbow-smoke.glsl) | In use | Preset `window.rainbow-smoke`; pools: `favourites` |
| [shaders/window/rainbow-waves.glsl](shaders/window/rainbow-waves.glsl) | In use | Preset `window.rainbow-waves`; pools: `favourites` |
| [shaders/window/rainfall.glsl](shaders/window/rainfall.glsl) | Registered, outside pools | Preset `window.rainfall` |
| [shaders/window/rgb-border.glsl](shaders/window/rgb-border.glsl) | Registered, outside pools | Preset `window.rgb-border` |
| [shaders/window/rgb-shimmer.glsl](shaders/window/rgb-shimmer.glsl) | Registered, outside pools | Preset `window.rgb-shimmer` |
| [shaders/window/ripple-drops.glsl](shaders/window/ripple-drops.glsl) | In use | Preset `window.ripple-drops`; pools: `favourites` |
| [shaders/window/rolling-clouds.glsl](shaders/window/rolling-clouds.glsl) | In use | Preset `window.rolling-clouds`; pools: `favourites` |
| [shaders/window/rorschach.glsl](shaders/window/rorschach.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/rorschach2.glsl](shaders/window/rorschach2.glsl) | In use | Preset `window.rorschach2`; pools: `favourites` |
| [shaders/window/sentient-circuit-v2.glsl](shaders/window/sentient-circuit-v2.glsl) | In use | Preset `window.sentient-circuit-v2`; pools: `favourites`; shortcuts: `Mod+Ctrl+S` |
| [shaders/window/sentient-circuit.glsl](shaders/window/sentient-circuit.glsl) | Registered, outside pools | Preset `window.sentient-circuit` |
| [shaders/window/snowfall.glsl](shaders/window/snowfall.glsl) | In use | Preset `window.snowfall`; pools: `favourites` |
