# Biri shader port reference for Bumbriel

Prepared on 2026-09-21 for the agent implementing Barrulus's shader collection in
`barrulus/bumbriel`, the local Umbriel fork at `/home/barrulus/dev/bumbriel`.
This is an implementation handoff, with original assets, rather than an implemented
port. Its scope is every effect in Biri's repository shader bundle and the renderer
features needed to retain their appearance and everyday controls.

Current implementation and remaining work: [STATUS.md](STATUS.md).
The later live-window extension is preserved separately in [window-source/](window-source/);
this handoff and its original snapshot describe the initial repository bundle.
The reference below describes the full intended scope.

## Start here

1. Read this reference, then Bumbriel's `CONTRIBUTING.md`, `umbrielfx/README.md`,
   `docs/design/animation-shaders.md`, and `docs/design/border-rendering.md`.
2. Preserve the source snapshot under [original/](original/) unchanged. Implement
   converted, editable GLSL under `examples/shaders/` and native TOML configuration.
3. Implement the milestones below. Finish each with its renderer tests and visual
   checks before expanding the next part of the pipeline.
4. Track completion against the inventory: **32 effect families**, including four
   matched open/close pairs (**eight lifecycle shaders**), ten cursor effects, four
   screen effects, ten window effects, and four focus-ring effects. Also retain
   optional ring lighting, output filters, scoped chains, reload, and runtime controls.

All Bumbriel paths below are relative to its repository root. Biri paths are
relative to `/home/barrulus/dev/biri`. Use symbols as navigation anchors: line
numbers will move during implementation.

### Provenance and scope

| Item | Inspected state |
| --- | --- |
| Biri HEAD | `0cb779d1a4fd6d9a181711b130e1b5f871352a73` |
| Bumbriel HEAD | `75e35316ae295714a3869d34242743e4952c422b` |
| Biri working tree | Modified bundle README and `focus-ring/lightning.frag`; untracked `focus-ring/fuse.frag` and `focus-ring/fuse.kdl` are included. Other existing user changes were left untouched. |
| Bumbriel at inspection | Clean working tree. |
| Frozen material | Entire `resources/shaders/`, three documented open/close/resize examples, and Biri's root license. |
| Integrity | [manifest.json](manifest.json) records original paths, SHA-256, sizes, working-tree status, and shader symbols. |

The snapshot is deliberately of the **working tree**, not just the source commit:
checking out that SHA alone loses the fuse and latest multi-head lightning changes.
No personal configuration outside these two repositories was inventoried. This
reference therefore covers all repository-bundled effects, without claiming to
know additional shaders installed in the user's home configuration.

Biri includes a GPL v3 license text, copied to [original/LICENSE](original/LICENSE);
Bumbriel and UmbrielFX currently have MIT license files. Preserve source attribution
and the original license with copied reference material. Do not label copied source
MIT merely because it resides inside this repository. These facts are provenance,
not a determination of the licensing of a future implementation.

Umbriel's upstream `SCOPE.md` excludes cross-compositor compatibility readers and
effects outside UmbrielFX. This task is authorized work in Barrulus's fork: it does
not require upstream feature acceptance. Keep native TOML and the existing renderer
architecture; no KDL loader or plugin framework is needed to port these visuals.

## Complete visual inventory

Links point to exact preserved sources. KDL files contain the GLSL inline: extract
the shader body and translate its host interface; do not pass the KDL to a compiler.
Defaults and tuning constants in those files are the visual baseline.

### Focus rings and borders

These are persistent effects on an active decoration, not effects that end when a
focus transition finishes. Apply globally and override by window rule. Inactive and
urgent decorations retain normal configured styling.

| Source | Appearance and acceptance details |
| --- | --- |
| [rainbow-ripple.frag](original/resources/shaders/focus-ring/rainbow-ripple.frag) | Flowing pastel wax, uneven edges and drifting highlights. The supplied preset uses width 6 and padding 14. Maintain continuity around all corners. |
| [pulse.frag](original/resources/shaders/focus-ring/pulse.frag) | Simple cyan pulse confined to the nominal ring; no extra padding required. |
| [lightning.frag](original/resources/shaders/focus-ring/lightning.frag) | Travelling blue-white crackle, forks, halo and bright head. Baseline width 6, padding 24; optional light spread 80, intensity 1.0, threshold 0.5. `LIGHTNING_COUNT` supports 1–4 equally spaced heads, default 1; clamp out-of-range values. Preserve each head's width and lap time as count changes. Default lap time is 4 seconds with `SPEED = 1`. |
| [fuse.frag](original/resources/shaders/focus-ring/fuse.frag) | Stationary irregular braided cord, moving orange-white embers, recovering charred trail and flying sparks. Width 6, padding 48; light spread 90, intensity 1.4, threshold 0.5. `EMBER_COUNT` supports 1–4, default 1. `FUSE_SECONDS = 10`, `FUSE_WANDER = 1`, `FUSE_BRIGHTNESS = 1`. Brightness affects fire and sparks, not the unburnt cord. The cord stays within the nominal width, fitting a 6-pixel gap. |

The [fuse preset](original/resources/shaders/focus-ring/fuse.kdl) and
[rainbow preset](original/resources/shaders/focus-ring/rainbow-ripple.kdl) record
the intended geometry. Padding reserves raster space for sparks/glow; it must not
alter layout gaps, window sizes, input regions, or the cord position.

All four shader files can emit light through a host-provided emission pass without
new shader functions. Do not replace lightning/fuse lighting with a uniform shadow
or glow: the illumination must move with each actual bright head.

### Window content effects

“Static” here means the executable GLSL does not depend on time; source comments
sometimes mention `niri_time` anyway. Static effects must not keep an idle output
redrawing just because a comment contains a uniform name.

| Source | Timing | Appearance and acceptance details |
| --- | --- | --- |
| [crt.frag](original/resources/shaders/window/crt.frag) | Static | Barrel distortion, RGB phosphor/aperture grille, chromatic aberration, scanlines and vignette; preserve the black bezel where sampling exits the window. |
| [parchment.frag](original/resources/shaders/window/parchment.frag) | Static | Aged tan vellum, crackle/crumple texture and burnt edges; dark regions become paper while bright coloured content stays readable. |
| [parchment-dark.frag](original/resources/shaders/window/parchment-dark.frag) | Static | Dark-app variant: monotonic deep-brown-to-cream luminance mapping; keep colour in icons, mentions and emoji. This is a separate preset. |
| [pixel-mosaic.frag](original/resources/shaders/window/pixel-mosaic.frag) | Static | Coarse 10-physical-pixel blocks, posterization and a subtle grid. |
| [fisheye-rgb.frag](original/resources/shaders/window/fisheye-rgb.frag) | Static | Frozen radial edge lens and RGB fringe, crisp centre. |
| [adaptive-text-v4.frag](original/resources/shaders/window/adaptive-text-v4.frag) | Static | Transparent-terminal legibility: 17-tap backdrop estimate, gentle brightness-dependent dimming, continuous soft-knee detail gain. No binary text classification or speckled glyph edges. Defaults: radius 10 px, gain 2.2, knee 0.08–0.22, dim 0.65, luminance range 0.30–0.65. Requires already-composited backdrop pixels; see the input contract below. |
| [rgb-shimmer.frag](original/resources/shaders/window/rgb-shimmer.frag) | Animated | Moving RGB shimmer. Retain the original shader's displacement and colour strength. |
| [ripple-drops.frag](original/resources/shaders/window/ripple-drops.frag) | Animated | Procedural falling-drop ripples over window content. |
| [rorschach2.frag](original/resources/shaders/window/rorschach2.frag) | Animated | Flowing symmetric Rorschach ink pattern; preserve its mirror symmetry and content blending. |
| [mercury-sheen.frag](original/resources/shaders/window/mercury-sheen.frag) | Animated | Cool liquid-metal/chrome field with rolling surface relief and moving specular glints. Retain the editable opacity, scale, bump and specular constants. |

Window effects need application rules and per-window runtime selection. They must
remain active after map/move animations finish and continue correctly through
resize, scrolling, workspace changes and overview. Decorations remain separately
controlled. Shading must not change client sizes or input coordinates.

### Cursor effects

| Source | Required rendering | Appearance and acceptance details |
| --- | --- | --- |
| [adaptive.kdl](original/resources/shaders/cursor/adaptive.kdl) | Static, cursor-local | Luminance-adaptive blue ring: bright on dark content, dark on white. Ring radius 55 physical px, glow extends about 110 physical px; declared capture radius 130 logical px. |
| [blueglow.kdl](original/resources/shaders/cursor/blueglow.kdl) | Static, cursor-local | Blue ring and soft glow, same dimensions; blends visibly on white pages. |
| [comet.kdl](original/resources/shaders/cursor/comet.kdl) | Two passes, time and per-pass feedback | Pass 0 stores only a rainbow cursor dab and decaying tail. Pass 1 adds that trail to the original screen. Scrolling/video must never become part of the history. |
| [comet-glow.kdl](original/resources/shaders/cursor/comet-glow.kdl) | Two passes, time and per-pass feedback | Same accumulator; additive neon on dark pixels, painted rainbow on bright pixels, selected by scene luminance. |
| [rainbow-tunnel.kdl](original/resources/shaders/cursor/rainbow-tunnel.kdl) | Animated | Inward-flowing translucent concentric rainbow bands inside the adaptive blue ring and glow. |
| [rainbow-tunnel-bare.kdl](original/resources/shaders/cursor/rainbow-tunnel-bare.kdl) | Animated | Inward-flowing rainbow pool with soft falloff and no ring. Keep this separate from the ringed version. |
| [ripple.kdl](original/resources/shaders/cursor/ripple.kdl) | Animated sampling distortion | Water ripple around the pointer; displacement dies out around 130 physical px. |
| [shockwave.kdl](original/resources/shaders/cursor/shockwave.kdl) | Animated sampling distortion | Pulsing rainbow wave, displaced content near the ring, glow and visible centre. Nominal radius oscillates around 200 physical px. |
| [spotlight.kdl](original/resources/shaders/cursor/spotlight.kdl) | Static, whole output, pointer-driven | Dim the entire output outside the pointer's spotlight to 35%. A cursor-sized capture would give the wrong result. |
| [trail.kdl](original/resources/shaders/cursor/trail.kdl) | Dedicated feedback buffer | Cyan trail with screen-independent intensity history. Evaluate `global_buffer` before the visible colour pass. |

Only adaptive and blueglow request cursor-region optimization in the source bundle.
The animated effects are initially whole-output effects. `trail` animates through
feedback without referencing `niri_time`. Pointer motion must trigger updates even
when the cursor is rendered by a hardware plane and clients produce no damage.

### Screen effects and output filters

| Source | Appearance |
| --- | --- |
| [crt.kdl](original/resources/shaders/screen/crt.kdl) | Simple whole-screen scanlines; intentionally different from the richer window CRT. |
| [grayscale.kdl](original/resources/shaders/screen/grayscale.kdl) | Luma coefficients `(0.299, 0.587, 0.114)`. |
| [vignette.kdl](original/resources/shaders/screen/vignette.kdl) | Darkened screen edges, brighter centre. |
| [warmtint.kdl](original/resources/shaders/screen/warmtint.kdl) | RGB multipliers `(1.05, 0.92, 0.78)`; retain this look separately from a temperature filter. |

All four are static. The KDL files select a global shader, while output-specific
filters are additional host functionality. Retain grayscale, invert, saturation
(default amount 1.5), and temperature (default 4000 K; exactly neutral at 6500 K).
The generating code is `niri-config/src/shader_presets.rs`; reuse its actual formula
as the visual reference instead of substituting an unrelated temperature algorithm.

### Matched open/close animations

Each source owns **both** `window-open` and `window-close`; port both functions into
separate editable files or an explicitly direction-aware shared shader.

| Source | Open / close milliseconds | Appearance |
| --- | --- | --- |
| [close-whirlpool.kdl](original/resources/shaders/close/close-whirlpool.kdl) | 400 / 500 | Spiral into the centre and dissolve; matching inverse reveal. |
| [close-melt.kdl](original/resources/shaders/close/close-melt.kdl) | 400 / 500 | Melt downward into a hot puddle; rise from the puddle on opening. |
| [close-ripple.kdl](original/resources/shaders/close/close-ripple.kdl) | 400 / 500 | Concentric stone-in-water waves distort and fade/reveal the window. |
| [close-lightning.kdl](original/resources/shaders/close/close-lightning.kdl) | 400 / 400 | Electrical burn radiates outward from the centre. |

All bundled timelines use a linear curve. Preserve that before experimenting with
easing. Match start, intermediate and end frames, including premultiplied fading.
These are the first effects that can use Umbriel's existing API directly.

The preserved [open](original/docs/wiki/examples/open_custom_shader.frag),
[close](original/docs/wiki/examples/close_custom_shader.frag), and
[resize](original/docs/wiki/examples/resize_custom_shader.frag) examples document
Biri's broader animation interface. They are supplementary API references, not
three additional selected presets. No custom resize preset is in the bundle.
Umbriel's existing `windows_move` shader does not automatically provide Biri's
separate old/new resize textures; that would be additional work if requested.

## What Umbriel already provides

| Existing facility | Verified entry points | How to use it |
| --- | --- | --- |
| GLES2 / GLSL ES 1.00 custom animation wrapper | `umbrielfx/render/fx_renderer/shaders.c`, `fx_animation_shader_create` | Adapt lifecycle functions to `vec4 animation(vec2 uv)`. |
| Nine ordered effect slots on scene-node addons | `umbrielfx/include/umbrielfx/render/animation.h`, `src/scene/animation_shader.h` | Keep existing order and ABI. The slots describe transitions, not a ready-made persistent postprocess pipeline. |
| Recursive subtree capture and shader composition | `umbrielfx/types/scene/wlr_scene.c`, `render_animated_range` | Reuse capture/clip mechanics where input semantics match. |
| Target-local history and buffer pooling | `umbrielfx/render/fx_renderer/fx_pass.c`, `fx_render_pass_begin_animation`, `fx_render_pass_end_animation_with_history`, `animation_history_commit_updates`; `internal/render/fx_renderer/animation_history.h` | Reuse ownership and successful-submission promotion. Add distinct persistent output history where needed. |
| Source validation and file dependency watching | `src/config/animation_shader.cpp`, `src/config/config_watcher.cpp`, `src/scene/animation_shader.cpp` | Extend source loading/caching consistently to persistent shader classes. |
| Single hollow, double-colour border node | `src/view/decoration.cpp`, `src/view/border_ring.cpp`, `src/scene/border_rect.cpp`, `umbrielfx/render/fx_renderer/shaders/border.frag` | Extend with procedural decoration rendering, retaining fractional coverage and the client hole. |
| Blur, shadows, colour-managed intermediates | `umbrielfx/render/fx_renderer/fx_pass.c`, `fx_offscreen_buffers.c`, shader directory | Reuse for light emission/blur and proper working formats. |
| Frame scheduling | `src/output/output.cpp` (`Output::handleFrame`), `src/output/frame_schedule.h`, `src/server/server_events.cpp` | Add separate persistent-shader activity and capped idle scheduling. |
| Config, rules, presets and actions infrastructure | `src/config/config.h`, `config.cpp`, `resolve.cpp`, `change.cpp`; `src/server/actions.cpp`, `ipc_commands.cpp` | Add native settings and runtime controls through existing mechanisms. |
| Capture integration | `src/server/server.cpp`, `src/view/view_foreign.cpp`, `umbrielfx/types/scene/wlr_scene.c`, `umbrielfx/types/scene/surface.c` | Audit output and toplevel capture independently. Preserve shared-surface output membership and frame pacing. |

Umbriel's animation program cache is keyed by event, exact source and renderer;
failed compilations are cached. Active transitions retain a program through reload.
Shader files are regular, nonblank, NUL-free and at most 256 KiB; loading uses
nonblocking opens. Paths are relative to the declaring TOML file, including an
included file. Shell/environment expansion is not supported. Retain these native
rules in the new interfaces.

All rendering work belongs in the C UmbrielFX library. C++ owns configuration,
rule resolution, scene associations and scheduling policy. Public library APIs
belong in `umbrielfx/include/umbrielfx/`; do not make compositor code depend on
private renderer headers. Preserve wlroots-compatible struct prefixes, using
addons where appropriate. Existing scene-helper and ABI checks remain applicable.

Install converted editable shader files through the root `meson.build`, alongside
its existing `reveal.glsl` and `squash.glsl` data entries. Keep internal renderer
wrappers embedded through UmbrielFX's shader build path. Nix examples should derive
asset paths from the configured package, following `docs/user/animation.md`.
Do not hardcode this machine's home or Nix store paths in shipped configuration.

## Shader contracts and translation

### Lifecycle translation: implemented Umbriel interface

| Biri | Umbriel equivalent or required adaptation |
| --- | --- |
| `open_color(vec3 coords_geo, vec3 size_geo)` / `close_color(...)` | `animation(vec2 uv)`; `coords_geo.xy` becomes target-normalized `uv`. Audit target bounds because Umbriel's window target includes its border. |
| `texture2D(niri_tex, (niri_geo_to_tex * vec3(q, 1)).xy)` | `umbriel_sample(q)`; the helper handles target/output sampling transforms and out-of-bounds transparency. |
| `niri_clamped_progress` | `umbriel_clamped_progress`; advances 0→1 for both opening and closing. Do not reverse an already separate close function a second time. |
| `niri_progress` | `umbriel_progress`; can overshoot with springs, unlike clamped progress. |
| `niri_random_seed` | `umbriel_random_seed.x`; retain a stable seed for each transition. The destination has four channels. |
| Physical sizes / matrix arithmetic | `umbriel_size` is **logical** target size. It is not a drop-in physical-pixel replacement. The four selected pairs do not require a physical-size uniform in their actual shader bodies. |
| Output colour | Return **premultiplied RGBA**, as required by the existing Umbriel animation API. Preserve colour × alpha fading. |

For example, after conversion the existing configuration accepts:

```toml
[animation.windows_in]
enabled = true
duration_ms = 400
curve = "linear"
shader = "shaders/biri/whirlpool-open.glsl"

[animation.windows_out]
enabled = true
duration_ms = 500
curve = "linear"
shader = "shaders/biri/whirlpool-close.glsl"
```

Those file paths are proposed converted assets, not files already supplied by this
reference. Existing Umbriel window lifecycle shaders replace native fade/scale/slide
visuals. Do not additionally multiply by the native fade and fade twice. Keep
close-during-open snapshots, stable seeds, shadow silhouettes, vacancy masks, and
independent `windows_move` / `windows_out` timelines working as they do now.

### Persistent postprocess interface: new host support required

The following describes source semantics to preserve. The implementation may give
the ported API native `umbriel_*` names and translate each source. These names are
**not claimed to exist in current Umbriel**, and no runtime compatibility parser is
required. Keep entry points for scene postprocessing separate from lifecycle and
decoration entry points so alpha/coordinate conventions cannot be confused.

| Source symbol | Required semantics |
| --- | --- |
| `global_color(vec3 coord)` | Visible colour pass. `coord.z = 1`. Full-output and region coordinates are normalized over the output; window coordinates are normalized over the window rectangle. |
| `niri_size` | Current raster element dimensions in physical pixels. In a small region this is the region size, not the output size. |
| `niri_output_size` | Full output physical dimensions for output/region effects; Biri passes window physical dimensions for window effects. |
| `niri_scale` | Output scale. Distinguish it from logical scene sizes. |
| `niri_cursor` | Output-local physical pointer position for output/region effects, zero for current Biri window shaders. |
| `niri_region` | `(origin.xy, size.xy)` in output-normalized coordinates, or `(0,0,1,1)` for window/full-output targets. |
| `niri_time` | Seconds; provide a documented monotonic clock. Actual Biri global/region/output time starts per output on activation, while window time has a compositor-wide origin. Decoration time uses the layout clock × configured speed. |
| `tex2D_screen(uv)` | Current pass input; first pass sees the captured source. |
| `tex2D_source(uv)` | Original input before any passes, required by both comet shaders. See the scoped-source discrepancy below. |
| `tex2D_prev(uv)` | Previous successful visible result of this pass, independent per output and pass. Global first-frame seed is black. |
| `tex2D_screen_prev(uv)` | Previous frame's original scene; falls back to current scene on first frame. |
| `tex2D_buffer(uv)` | Dedicated accumulator if supplied; otherwise aliases this pass's previous result. |
| Optional `global_buffer(vec3 coord)` | Run first, reading last frame's accumulator. The visible colour pass reads the newly generated accumulator in the same frame. |

Preserve the physical-pixel mathematics of the cursor ring, CRT grille, mosaic and
blur radius. Converting `niri_size` directly to the existing logical `umbriel_size`
changes their appearance on scaled displays. Normalize Y down and apply output
rotation exactly once; never build screen coordinates directly from unadjusted
`gl_FragCoord`.

For regions, remap output-normalized UV to region texture coordinates and explicitly
define transparent sampling outside the capture. Retain enough capture padding for
the effect's sample footprint. Biri's helpers perform UV remapping but do not
themselves bounds-check; do not assume their comments establish GL sampler state.

**Window input is an important implementation detail.** In the inspected Biri
`src/layout/tile.rs`, `Tile` inserts a `ScopedSource::Capture` above normally rendered
window content. `ScopedShaderElement::draw` copies that framebuffer rectangle. For
transparent clients this includes the composited backdrop, which is why
`adaptive-text-v4` can estimate wallpaper brightness. The wiki's description of
sampling only the window's own content is incomplete. Umbriel's animation subtree
capture produces a different input. For faithful ports, provide a bounded
post-composite window-rectangle operation at the correct paint position, or another
explicit backdrop-aware capture mechanism. Do not silently run adaptive-text on
an isolated RGBA client texture and claim equivalent results.

That operation must handle damage underneath transparent windows, avoid capturing
later/overlapping windows or duplicating the backdrop when blending, and retain
native rounded client clipping. Biri's documented square-corner limitation is not
a visual requirement to reproduce. Separately specify isolated-window capture
behaviour: it must not accidentally include unrelated desktop content.

### Passes and feedback

For each output and frame, preserve this order:

```text
capture original scene S(t)
for each pass i, in order:
    input = S(t) for i=0, otherwise result(i-1,t)
    if a buffer function exists:
        buffer(i,t) = buffer_function(input, previous buffers/results, S(t))
    result(i,t) = colour_function(input, S(t), result(i,t-1), buffer(i,t))
composite the last result
promote histories only after successful submission
```

No pass may sample the texture it is currently rendering into. Use distinct read
and write attachments; retain history until the frame is submitted. Key history by
renderer, output, effect instance and pass. Reset safely when source, dimensions,
transform, working format or output lifetime changes; release it on disable/removal.
Keep screen history, visible-result history and dedicated accumulator separate.

Umbriel's existing `umbriel_sample_previous` starts with **current target content**.
Biri's global previous result and accumulator start with **black**. Reusing the
animation fallback unchanged seeds trails with desktop/window imagery. Add an
explicit empty-history policy for these persistent effects. The original comet's
old comment about a first-frame screen seed is stale; current renderer code is
authoritative.

Comet decay is `max(previous * 0.98 - 0.004, 0)`; cyan trail uses
`max(previous * 0.85 - 0.006, 0)`. The subtraction prevents quantized 8-bit values
sticking forever. Preserve it even when initial history becomes correctly empty.
These are per-rendered-frame decays: changing the shader FPS changes wall-clock
trail duration. First reproduce the source; any time-normalized decay is a
deliberate later visual change, not a mechanical port.

Biri's region/window/output scoped renderer currently has **no history**:
all five sampler bindings alias the advancing current pass input, and
`global_buffer` is ignored. This also means `tex2D_source` on later scoped passes
is **not** the original source despite the wiki saying it is. None of the ten
bundled window shaders needs this discrepancy. Implement and document a consistent
original-source contract in the new pipeline; do not copy that limitation into
the comet/global path.

### Decoration interface and illumination

| Source interface | Required meaning |
| --- | --- |
| `ring_color(vec2 coords)` | Return **straight RGBA**; the host applies base opacity, hollow-client mask, premultiplication and window opacity once. This differs from Umbriel's animation return convention. |
| `coords` | Logical pixels from client top-left, X right/Y down; negative outside. Shared continuous coordinates across the entire ring. |
| `ring_size`, `ring_width`, `ring_padding` | Client size, nominal width, reserved raster extent in logical units. |
| `ring_radius` | Client radii in top-left, top-right, bottom-right, bottom-left order. |
| `ring_distance(coords)` | Rounded-client signed distance, positive outside. |
| `ring_base_color(coords)` | Configured colour/gradient as straight RGBA; wrapper retains its alpha. |
| `niri_time`, `niri_scale` | Layout animation clock × speed, and scale for antialiasing. Static/frozen decoration time is zero in Biri. |

Host configuration baseline: enabled=true, animated=true, speed=1 in range 0–10,
padding=0 in range 0–1024. A per-window override replaces the inherited shader block;
an explicit disable restores ordinary styling. Missing/invalid shaders also fall
back to normal colours without widening the visible border to the padding box.

Umbriel has one double-band border node, while Biri can independently style a border
and a focus ring. Start by enabling these procedural effects on Umbriel's active
border, with an explicit choice of affected band or complete ring. Preserve the
other band's configured appearance. If independent simultaneous border and focus
ring effects are exposed, implement their separate geometry deliberately. The
existing `[animation.border]` only shades during focus-colour transitions and does
not satisfy the persistent effect requirement.

Retain the punched client hole even for transparent windows. Extend the border
raster bounds for padding while keeping content/seam/outer contours and fractional
coverage correct. Umbriel's CPU interior-cross clipping and default border path
must remain valid. Fullscreen/suppressed/hidden decorations must not animate or emit.

Light settings default to spread=80 (1–256 logical px), intensity=1 (0–4),
threshold=0.5 (0–1), disabled unless requested. The Biri implementation:

1. Renders the same ring at the same time into an emission texture, at 0.5 pixels
   per logical pixel, retaining the original AA scale.
2. Computes peak brightness from premultiplied RGB **after opacity**, and multiplies
   by `smoothstep(threshold, max(threshold + 0.001, 1), peak)`.
3. Pads by a conservative blur guard band and diffuses emission with Dual Kawase
   blur. Float intermediates are preferred, with compatible lower-precision fallback.
4. Converts blurred light using `1 - exp(-light * gain)` and screen-blends it:
   `out.rgb = light.rgb + scene.rgb * (1 - light.rgb)`.
5. Reuses static blurred emission; animated rings update on the shader cadence.

The light layer can extend inward/outward and illuminate neighbouring windows.
In the normal view it sits above window content, including floating/sticky windows,
but below top/overlay shell surfaces. It follows overview scale and window motion.
Biri suppresses spill during opening transforms and excludes it from closing
snapshots; output captures include normal decoration lighting, isolated window
captures do not include this scene lighting. Record any intentional destination
policy differences. This is local screen-space illumination, not ray tracing or a
request for window-texture access from ring shaders.

## Composition, scheduling, capture and reload

### Composition order

For the source postprocess scopes, the inner-to-outer order is:

```text
window-rectangle shaders within scene painting
    → region shaders
    → per-output shader
    → global shader
    → cursor, unless global reads-cursor places it inside the input
```

“Global” means configured across outputs, with distinct output-local history; do
not share a single desktop-sized history between monitors. Regions can select an
output or apply to each output, with geometry in logical pixels. Define overlap
ordering explicitly and verify with noncommutative filters rather than assuming
paint-list order. Place decoration lighting and shell layers deliberately before
the output-wide filters.

A grayscale output filter below a rainbow global cursor effect leaves the rainbow
coloured. To filter absolutely everything, the filter must be the final global
pass. Preserve this useful ordering and explain it in destination documentation.

Umbriel's working space is sRGB on ordinary SDR paths and linear in the FP16
colour-managed path. These presets were tuned against Biri's 8-bit framebuffer
captures. Define where they run relative to output conversion, and whether an sRGB
compatibility sampling boundary is required. Do not apply sRGB luma/temperature
constants blindly to linear HDR content or clamp HDR values globally to `[0,1]`.
Preserve premultiplied alpha and output colour management; validate SDR appearance
first and document hardware HDR validation separately.

### Redraw and resource requirements

- Disabled effects allocate no effect buffers and request no extra frames. Static
  effects redraw on relevant damage, including backdrop damage when applicable.
- Time or feedback causes animation only on outputs where the effect is visible.
  Hidden workspace/offscreen windows and hidden rings must not keep rendering.
  Overview visibility must account for displayed cards.
- Retain auto/on-damage/continuous policies for global effects and an idle shader
  FPS cap (0/unset means native refresh). Cap shader-only redraws with timers;
  pointer interaction, real client damage, window movement and normal animations
  must continue at their normal cadence.
- Inspect compiled capabilities or explicit metadata instead of treating mentions
  in comments as animation. `global_buffer` and previous-result use still require
  frames even without a time uniform.
- A cursor-local effect damages **both old and new bounds**, including sampling
  footprint. Handle pointer crossing outputs, pointer hiding and output rotation.
- Whole-output shaders require composition. A small cursor rectangle can reduce
  shaded pixels, but do not promise partial direct scanout on Umbriel: its current
  scanout path accepts a single eligible entry. Prove any plane-offload optimization
  against the actual backend.
- Umbriel currently treats active scene animations conservatively: scene-wide
  scanout/culling suppression and full damage. Keeping a transition slot alive
  forever would inherit this cost on unrelated outputs. Persistent effects need
  intentional output-local eligibility and damage decisions.
- Preserve unconditional client frame-done handling, session suspend/resume guards,
  output commit retry behaviour, and tearing constraints in `Output::handleFrame`.
- Clear output histories at lock/unlock and other scene-security boundaries so
  old desktop pixels cannot trail over a lock screen. Suppress effects on secure
  lock content unless that path is explicitly supported and tested.

### Cursor and capture policy

Reading cursor **position** does not require cursor **pixels**. Keep hardware cursors
for ordinary glow/trail effects. An explicit reads-cursor option must include a
software cursor before the pass and release that requirement when disabled.

Biri defaults global/region/window/output postprocessing out of protocol captures,
with `shaders-in-capture` to opt in. Decoration shaders/light use their normal scene
capture rules, and lifecycle animations are a separate existing visual path.
Retain that distinction in the port. Protocol output capture, toplevel capture and
direct scanout/KMS observation are different paths; a local policy cannot hide an
effect from a tool reading the final scanout buffer.

Do not assume Umbriel's current output capture can simply skip a pass applied to
the display buffer. Audit where wlroots copies each source and provide an unfiltered
render target or separate capture composition if needed. Capture rendering must not
advance display history; read it without committing, use a stable already-rendered
result, or maintain an independent capture history. Multiple recording clients must
not accelerate trail decay. Preserve capture-scene surface membership and frame
pacing (`umbrielfx/tests/capture_pacing.c`).

### Reload and runtime controls

Retain automatic shader-file watching, included-file-relative paths, diagnostics
with source labels, per-renderer program caches, and renderer-recreation handling.
Compile outside the paint loop. Cache failures so an invalid file does not compile
every frame; fixing or creating a missing file should recover on reload. Never run
a partially compiled pass chain. Define fallback clearly: ordinary rendering on a
failed initial load; either retain the last complete valid chain on edit failure
or revert the entire effect consistently, with a diagnostic. Existing lifecycle
transitions must retain their established reload semantics.

Expose native controls covering:

- Named window shader presets, toggle for focused/identified window, and cycle
  `rule/default → preset 1 → … → default`. Selection is per-window; cycle re-enables
  a disabled effect. Preserve selection by name across reload.
- Named output presets with the same toggle/cycle semantics, addressed by output
  identity or focused output. Resolve monitor identity through existing Umbriel code.
- A simple off state and cursor/screen/paired-animation selection suitable for binds.
  Original scripts relink KDL includes then call `niri msg action load-config-file`;
  preserve the interaction, not those commands. Umbriel already provides
  `umbriel msg config-reload` and automatic reload.
- Built-in output filters plus user presets. Biri resolves an output's top-level
  preset against user names then built-ins; preset definitions resolve built-ins
  only, preventing recursion. Use an equally explicit native rule.

The preserved cycle scripts are historical reference only: they point at Biri
configuration and invoke niri. Do not execute them as part of the Umbriel port.

## Source map for implementation questions

Use implementation over prose where they conflict. The local Biri repo remains
available alongside this frozen asset bundle.

| Question | Biri files / anchors |
| --- | --- |
| Global uniforms, helpers, wrappers | `src/render_helpers/shaders/global_prelude.frag`, `global_epilogue.frag`, `global_buffer_epilogue.frag`, `global_hypr_prelude.frag` |
| Global multipass and empty feedback seed | `src/render_helpers/global_shader_element.rs`, `GlobalShaderElement::draw` |
| Scoped sampler bindings / capture | `src/render_helpers/scoped_shader_element.rs`, `ScopedShaderElement::draw`; `src/layout/tile.rs` near `ScopedSource::Capture` |
| Compile/cache and failure handling | `src/render_helpers/shaders/mod.rs`, `set_custom_global_passes`, `set_scoped_programs`, `set_decoration_programs` |
| Composition, cursor bounds, timing, capture sinks | `src/niri.rs`, `render_inner`, `global_shader_start`, `window_shader_start`, `global_shader_chain` |
| Redraw capability scan | `niri-config/src/global_shader.rs`, `GlobalShaderCaps`, `RedrawMode`; `src/niri.rs` shader redraw gate |
| Region/window/output config and presets | `niri-config/src/region_shader.rs`, `window_shaders.rs`, `output_shader.rs`, `shader_presets.rs`; `src/output_shader.rs` |
| Ring config/reload | `niri-config/src/decoration_shader.rs`; `src/layout/focus_ring.rs`; `src/render_helpers/shaders/mod.rs` |
| Ring wrapper, hollow mask, emission threshold | `src/render_helpers/shaders/border.frag`, `custom_ring_color`; `src/render_helpers/border.rs` |
| Light generation and screen blending | `src/render_helpers/decoration_light.rs`; `shaders/decoration_light.frag`; `shader_element.rs`, `with_screen_blend`; `src/layout/monitor.rs`, `decoration_light_ids` |
| Lifecycle wrappers | `src/render_helpers/shaders/open_prelude.frag`, `open_epilogue.frag`, `close_prelude.frag`, `close_epilogue.frag`, resize equivalents |
| Existing checks to translate | `src/tests/shaders.rs`; `src/layout/focus_ring.rs` EGL tests; `src/layout/tests.rs`, `egl_decoration_light_precedes_floating_and_tiled_content`; config tests beside parser code |

Known documentation drift to avoid carrying forward:

- The global wiki's blanket “redraw every frame” warning predates damage-aware
  scheduling; static effects need not continuously redraw, although they still
  require composition when rendering.
- Window captures include the already-composited backdrop in the actual render path.
- Scoped `source` aliases advancing input, rather than remaining the original input.
- Window and global clocks do not in fact share the same origin in this checkout.
- Global display-pass compile failure drops the chain; buffer-program failure has
  a separate fallback. The wiki's conflicting last-good/disable statements are not
  a precise contract. Make the new implementation coherent.
- Postprocess shader-file reload has source/cache limitations that decoration file
  watching does not. Use Umbriel's existing dependency watcher for every new class.
- `docs/superpowers/specs/2026-08-17-shader-audio-design.md` describes proposed audio
  support. No `niri_audio`/shader-audio implementation was found in `src` or
  `niri-config/src`, and no bundled shader uses it. It is not an implemented feature
  to port or a prerequisite for this collection.
- Hyprland-mode compatibility exists in Biri, but none of these bundled effects
  requires it. It can remain a separate future decision in this native port.

## Implementation milestones and acceptance

| Milestone | Deliverable | Required evidence |
| --- | --- | --- |
| 1. Lifecycle collection | Eight converted open/close shaders, TOML examples, installed editable data files | All four pairs look correct mid-transition and at endpoints; existing lifetime/feedback/shadow tests pass. |
| 2. Persistent decoration | Four ring shaders, global/rule selection, padding, time, watcher, safe fallback | Runs after focus transition completes; hollow transparent client; 1–4 lightning/fuse heads; fractional/rotated geometry correct. |
| 3. Decoration illumination | Emission, diffusion and screen blend | Bright heads illuminate nearby content; dim cord does not; correct stacking, opacity, snapshots and overview. |
| 4. Persistent window pipeline | Ten presets, backdrop-aware window scope, multi-pass plumbing, toggle/cycle | Adaptive text works over bright wallpaper; all static effects idle; visible animated effects continue; no border corruption. |
| 5. Output and simple cursor scopes | Four screen presets, built-in output filters, fixed regions, seven cursor effects without feedback | Per-output isolation, correct pointer coordinates, full-scene spotlight, cursor old/new damage, ordered filter composition. |
| 6. Feedback cursor collection | Comet, comet-glow and trail | Empty first history, no scroll/video smear, true decay to black, independent per-pass/output state. |
| 7. Integration completion | Capture policy, idle cap, reload/cycling UX, packaging, docs | Every inventory row checked; disable restores native fast paths; multi-output and capture tests pass. |

Milestone 5 covers the **seven** cursor shaders without feedback (adaptive,
blueglow, both tunnels, ripple, shockwave, spotlight); milestone 6 covers the other
three. Scheduling and capture isolation are architectural requirements from the
start, even where their full user controls land in the final milestone.

Use existing Bumbriel commands, without replacing the user's running compositor:

```sh
just test
just check animation_shader animation_shadow fractional_border fractional_content
just check csd_crop subsurface_corner subsurface_border small_border
just check tiled_close tiled_open animation_squash overview_close
# After adding the port's focused headless checks:
just check biri_shader
```

`just check` selects name fragments and runs isolated headless instances. Confirm
the matching check names with `just check-names`; add new checks to
`tests/harness/checks/`. Unit tests live in `tests/unit/` and must be registered in
`tests/meson.build`. Run relevant UmbrielFX ABI/colour/capture tests via `just test`.
Use `just gpu-test` when changing renderer creation/ownership or EGL, as the existing
contributor instructions require. Physical HDR, GPU-specific filtering and hardware
cursor/scanout behaviour need suitable hardware, beyond headless tests.

New tests should exercise visible behaviour and lifetime, not just GLSL compilation:

| Area | Cases |
| --- | --- |
| Inventory | Compile every converted effect and every pass in a real renderer; no missing close counterpart or alternate variant. |
| Coordinates | Scale 1, 1.25, 2; rotated portrait output; mixed-scale monitors; pointer on output edge; window moved/resized; continuous ring corners. |
| Transparency | Transparent terminal over dark/white/coloured backgrounds; backdrop-only damage; rounded corners; no light/ring inside the client hole except the separate spill layer. |
| Timing | Idle animated/static effects, comment-only time mentions, FPS cap, ordinary move animations under the cap, hidden workspace, overview, output suspend. |
| Feedback | First frame, scrolling content, two passes, two outputs, reload, allocation failure, failed submit, capture without history advancement, lock/unlock reset. |
| Lighting | Head counts 1–4 plus clamp limits; fuse brightness never lifts the cord above threshold; sparks not clipped by nominal width; static-cache reuse; disable/removal clears spill. |
| Runtime | Rule/default and preset cycle order; per-window independence; removed preset name; output hotplug; invalid file recovery; retained in-flight lifecycle program. |
| Capture | Output and toplevel capture with inclusion off/on; cursor policy; two capture consumers; native frame pacing; no unrelated backdrop exposure in isolated captures. |
| Performance | No-effect baseline; static idle frame count; one animated window on one of two outputs; peak texture allocation; enable/disable cycles release history and render locks. |

Take comparative screenshots or short clips at fixed size, scale and shader time
for the source and port. Animated visuals require intermediate frames; endpoint-only
screenshots can pass with the effect entirely missing. For renderer checks, temporarily
bypass the relevant pass and confirm the intended pixel assertion fails, following
the existing Umbriel testing approach. Keep those deliberate breakages out of commits.

The port is complete when every selected effect is available through documented
native configuration, looks like the preserved source, reloads without rebuilding,
can be disabled cleanly, and passes its composition/lifetime checks. Record intentional
improvements and any remaining limitations explicitly rather than describing an
unimplemented capability as working.
