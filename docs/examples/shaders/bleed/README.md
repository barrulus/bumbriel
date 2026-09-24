# Inward liquid focus rings

- `rings/neon-bleed.glsl`: rainbow wax, with a slow 22-second colour cycle.
- `rings/portal-lava.glsl`: violet, orchid and amethyst wax with sparse green
  filaments and narrow dark-purple seams.

Both use the Bewitching Frame video as a motion reference: an uneven liquid
edge swells into rooted tongues, stretches, pinches and sheds small fragments.
The continuous band reaches roughly 2–6 logical pixels into the client. Three
staggered roots at each corner make compact clusters, with sparser tears along
the sides. There is no broad inner vignette. A broad patch of the inner edge
pulls into a tapered strip; its middle pinches until the tip tears free, then
the tip briefly travels inward and shrinks.
There is no independently growing circular head or spherical highlight.
Shedding is confined to the client interior, including the rounded corners;
the exterior border contour remains fixed.
Each root pulls along its own inward direction, without downward drift.

Geometry uses orthonormal coordinates in logical pixels, so corner lobes do not
stretch across diagonals or change proportions with the window's aspect ratio.
The maximum inward evaluation region is 64px; small windows proportionally
reduce the lumps. Perimeter projection is used for pigment only. Both host
passes evaluate the same liquid surface, including its roots and highlights.

The adapters match a 6px border, 14px raster padding, corner radius 10 and
animation speed 1. Update the window adapter if that geometry or speed changes.
Both effects are defined in `effects.toml` and listed in the terminal choice in
`choices.toml`. Use Mod+Alt+S to cycle the focused window's border and its matching
overlay together. Each named effect defines `border.outer` and `border.inner` leaves.

Edit `bleed/droplets.glsl` for the shared geometry and motion, and
`bleed/neon.glsl` or `bleed/portal.glsl` for pigment and band edges. Regenerate
all four host files from `docs/examples` (or the installed Umbriel config directory):

```sh
python3 shaders/generate-bleed-overlays.py
umbriel validate -c config.toml
```

`BLEED_SPEED` controls each material's shedding rate; `bleed_lump` defines the
grow/pull/pinch/release lifecycle. Corner roots are in `ring_color`.
`bleed_edge_wave` sets the short waves along the band. The separate rainbow-ripple
and portal effects are independent of this generator.
