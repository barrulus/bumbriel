# Shader inventory

Snapshot: 24 September 2026, based on the TOML files in this directory.

“In use” means reachable through a configured pool or direct shortcut, not
necessarily visible right now. Registered effects can be selected by name.
Unregistered shaders are library sources; add a definition in `effects.toml`
before assigning them. Refresh this inventory when definitions or pools change.

See [the README](README.md) for outer rings, inner overlays, generators and shortcuts.

| Shader file | Status | Reference / reason |
| --- | --- | --- |
| [shaders/animations/wobbly-lifecycle.glsl](shaders/animations/wobbly-lifecycle.glsl) | In use | Opening and closing in `elastic.toml`; effect `elastic` |
| [shaders/animations/wobbly-move.glsl](shaders/animations/wobbly-move.glsl) | In use | Animated movement and resize in `elastic.toml` |
| [shaders/bleed/droplets.glsl](shaders/bleed/droplets.glsl) | Generator source | Shared source for the neon-bleed and portal-lava outer/inner pairs |
| [shaders/bleed/neon.glsl](shaders/bleed/neon.glsl) | Generator source | Shared source for the neon-bleed and portal-lava outer/inner pairs |
| [shaders/bleed/portal.glsl](shaders/bleed/portal.glsl) | Generator source | Shared source for the neon-bleed and portal-lava outer/inner pairs |
| [shaders/rings/faerie-magic.glsl](shaders/rings/faerie-magic.glsl) | In use | Border `faerie-magic`; choices: `terminals` |
| [shaders/rings/flowering-vine.glsl](shaders/rings/flowering-vine.glsl) | In use | Border `flowering-vine`; choices: `desktop`, `terminals` |
| [shaders/rings/flowing-water.glsl](shaders/rings/flowing-water.glsl) | In use | Border `flowing-water`; choices: `terminals` |
| [shaders/rings/fuse.glsl](shaders/rings/fuse.glsl) | In use | Border `fuse`; choices: `terminals` |
| [shaders/rings/lightning.glsl](shaders/rings/lightning.glsl) | In use | Border `lightning`; choices: `terminals` |
| [shaders/rings/neon-bleed.glsl](shaders/rings/neon-bleed.glsl) | In use | Border `neon-bleed`; choices: `terminals` |
| [shaders/rings/portal-lava.glsl](shaders/rings/portal-lava.glsl) | In use | Border `portal-lava`; choices: `terminals` |
| [shaders/rings/portal.glsl](shaders/rings/portal.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/rings/pulse.glsl](shaders/rings/pulse.glsl) | In use | Border `pulse`; choices: `terminals` |
| [shaders/rings/rainbow-ripple.glsl](shaders/rings/rainbow-ripple.glsl) | In use | Border `rainbow`; choices: `terminals` |
| [shaders/rings/sentient-runner.glsl](shaders/rings/sentient-runner.glsl) | In use | Border `sentient-runner`; choices: `terminals` |
| [shaders/rings/sentient-spark.glsl](shaders/rings/sentient-spark.glsl) | In use | Border `sentient-spark`; choices: `terminals` |
| [shaders/window/adaptive-text-v3.glsl](shaders/window/adaptive-text-v3.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/adaptive-text-v4.glsl](shaders/window/adaptive-text-v4.glsl) | Registered, outside choices | Effect `window.adaptive-text-v4` |
| [shaders/window/autumn-leaves.glsl](shaders/window/autumn-leaves.glsl) | In use | Effect `window.autumn-leaves`; choices: `favourites` |
| [shaders/window/crt.glsl](shaders/window/crt.glsl) | In use | Effect `window.crt`; choices: `favourites` |
| [shaders/window/cvd-deutan-alphabet.glsl](shaders/window/cvd-deutan-alphabet.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-deutan-combo.glsl](shaders/window/cvd-deutan-combo.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-deutan-drift.glsl](shaders/window/cvd-deutan-drift.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-deutan-oriented.glsl](shaders/window/cvd-deutan-oriented.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-deutan.glsl](shaders/window/cvd-deutan.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-protan.glsl](shaders/window/cvd-protan.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/cvd-tritan.glsl](shaders/window/cvd-tritan.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/faerie-magic-overlay.glsl](shaders/window/faerie-magic-overlay.glsl) | In use | Paired inner overlay for `faerie-magic` (`border.inner`) |
| [shaders/window/film-grain.glsl](shaders/window/film-grain.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/fire-tendrils.glsl](shaders/window/fire-tendrils.glsl) | In use | Effect `window.fire-tendrils`; choices: `favourites` |
| [shaders/window/fire.glsl](shaders/window/fire.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/fisheye-rgb.glsl](shaders/window/fisheye-rgb.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/flap-board.glsl](shaders/window/flap-board.glsl) | In use | Effect `window.flap-board`; choice: `favourites` |
| [shaders/window/liquid-glass.glsl](shaders/window/liquid-glass.glsl) | In use | Effect `window.liquid-glass`; choice: `favourites` |
| [shaders/window/flowering-vine-overlay.glsl](shaders/window/flowering-vine-overlay.glsl) | In use | Paired inner overlay for `flowering-vine` (`border.inner`) |
| [shaders/window/mercury-sheen.glsl](shaders/window/mercury-sheen.glsl) | In use | Effect `window.mercury-sheen`; choices: `favourites` |
| [shaders/window/neon-bleed-overlay.glsl](shaders/window/neon-bleed-overlay.glsl) | In use | Paired inner overlay for `neon-bleed` (`border.inner`) |
| [shaders/window/parchment-dark.glsl](shaders/window/parchment-dark.glsl) | In use | Effect `window.parchment-dark`; choices: `favourites` |
| [shaders/window/parchment.glsl](shaders/window/parchment.glsl) | Registered, outside choices | Effect `window.parchment` |
| [shaders/window/pixel-mosaic.glsl](shaders/window/pixel-mosaic.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/portal-lava-overlay.glsl](shaders/window/portal-lava-overlay.glsl) | In use | Paired inner overlay for `portal-lava` (`border.inner`) |
| [shaders/window/prairie-wind.glsl](shaders/window/prairie-wind.glsl) | In use | Effect `window.prairie-wind`; choices: `favourites` |
| [shaders/window/rainbow-radial.glsl](shaders/window/rainbow-radial.glsl) | In use | Effect `window.rainbow-radial`; choices: `favourites` |
| [shaders/window/rainbow-ripple-overlay.glsl](shaders/window/rainbow-ripple-overlay.glsl) | In use | Paired inner overlay for `rainbow` (`border.inner`) |
| [shaders/window/rainbow-smoke.glsl](shaders/window/rainbow-smoke.glsl) | In use | Effect `window.rainbow-smoke`; choices: `favourites` |
| [shaders/window/rainbow-waves.glsl](shaders/window/rainbow-waves.glsl) | In use | Effect `window.rainbow-waves`; choices: `favourites` |
| [shaders/window/rainfall.glsl](shaders/window/rainfall.glsl) | Registered, outside choices | Effect `window.rainfall` |
| [shaders/window/rgb-border.glsl](shaders/window/rgb-border.glsl) | Registered, outside choices | Effect `window.rgb-border` |
| [shaders/window/rgb-shimmer.glsl](shaders/window/rgb-shimmer.glsl) | Registered, outside choices | Effect `window.rgb-shimmer` |
| [shaders/window/ripple-drops.glsl](shaders/window/ripple-drops.glsl) | In use | Effect `window.ripple-drops`; choices: `favourites` |
| [shaders/window/rolling-clouds.glsl](shaders/window/rolling-clouds.glsl) | In use | Effect `window.rolling-clouds`; choices: `favourites` |
| [shaders/window/rorschach.glsl](shaders/window/rorschach.glsl) | Unregistered | Retained library shader; no reference in the included configuration |
| [shaders/window/rorschach2.glsl](shaders/window/rorschach2.glsl) | In use | Effect `window.rorschach2`; choices: `favourites` |
| [shaders/window/sentient-circuit-v2.glsl](shaders/window/sentient-circuit-v2.glsl) | In use | Effect `window.sentient-circuit-v2`; choices: `favourites`; shortcuts: `Mod+Ctrl+S` |
| [shaders/window/sentient-circuit.glsl](shaders/window/sentient-circuit.glsl) | Registered, outside choices | Effect `window.sentient-circuit` |
| [shaders/window/snowfall.glsl](shaders/window/snowfall.glsl) | In use | Effect `window.snowfall`; choices: `favourites` |
