# Port status

All **53 effect families** are implemented in native Bumbriel configuration and
UmbrielFX: four lifecycle pairs (eight shaders), four borders, 31 window effects,
four screen effects and ten cursor effects. Optional ring illumination, output
filters, ordered scoped chains, feedback, file reload and runtime controls are
implemented. The converted bundle contains **59 editable GLSL files**.

| Milestone | Delivered | Evidence |
| --- | --- | --- |
| 1. Lifecycle collection | Eight shaders, matched timing, native pair selection/cycling | GPU endpoint/intermediate/premultiplication checks; existing lifecycle regressions |
| 2. Persistent decoration | Four rings, rule overrides, padding, time, reload, overview, frozen close snapshots | GPU head counts/clamping/hole/idling; checks 201 and 202 |
| 3. Decoration illumination | Actual bright-detail emission, Dual Kawase diffusion, screen blend, static cache | GPU threshold/opacity/inward-outward/lifetime checks; check 203 |
| 4. Persistent window pipeline | Ten presets, composited backdrop, ordered passes, rules and per-window controls | Every preset rendered and source-compared; checks 204 and 205 |
| 5. Output and simple cursor scopes | Four screen presets, four builtins, regions/output/global, seven simple cursor presets | Chain ordering and output isolation in 205; rotated mixed-scale pointer motion in 206 |
| 6. Feedback cursors | Comet, comet-glow and trail, separate per-pass accumulators and source history | Empty first frame, independent instances, 140-frame decay, changing backgrounds, capture/failed-submit isolation |
| 7. Integration | Capture inclusion policy, scoped scheduling, idle cap, reload/cycle, installed collection and documentation | Output and isolated-toplevel protocol captures, retained consumers, colour-managed capture, scene idling and lock tests |

See [configuration and authoring](../../user/biri-shaders.md) and
[the asset README](../../../examples/shaders/biri/README.md). The installation
includes `collection.toml`, which registers all postprocess presets and animation
pairs without selecting an effect.

## Live window collection extension (2026-09-21)

The 31 shaders from `~/.config/biri/global-shaders/window/` are now included:
9 existing ports match, pixel-mosaic is refreshed, and 21 missing effects are
ported. Sources and hashes live in [window-source/](window-source/); the original
frozen snapshot remains unchanged. `windows.toml` registers the complete window
collection without enabling other shader scopes.

The expanded GPU test renders all 45 postprocess families, checks static versus
animated behaviour and visible changes, and compares original-source rendering.
All 31 window presets match their live Biri source pixels exactly at the tested
frame; the 14 existing cursor/screen families also retain exact comparisons.
Feedback, capture, scheduling and cleanup checks pass in the same run.
The checks below document the earlier full renderer implementation.

## Original implementation verification

- The existing project `nix develop` shell works with pinned wlroots 0.20.2.
  The local `.envrc` uses that flake. Debug-shell Fortify is disabled because
  GCC rejects its optimization-dependent warning at `-O0`; package hardening
  is unchanged. No Quixote shared shell or running desktop was modified.
- **62/62** unit and renderer tests pass, including scene ABI, capture pacing,
  colour management and the two new shader collection executables.
- **31/31** selected compositor checks pass: animation shaders/shadows, tiled
  opening/closing, fractional borders/content, CSD cropping, subsurface corners,
  small borders, overview close and the six new `biri_shader` checks.
- `just gpu-test` passes on both available DRM render nodes. The postprocess
  collection, feedback, capture, scheduling and renderer-destruction tests also
  pass on both nodes.
- All 24 postprocess families produce **identical fixed-time pixels** to shader
  bodies extracted from the frozen source on both GPUs. Only interface aliases
  are added to reference bodies. Converted reversed `smoothstep` falloffs also
  match those drivers' source behaviour.
- Fixed-time lifecycle, ring and postprocess sheets were rendered and inspected.
  Three unchanged lifecycle functions match their reference pixels exactly;
  deliberate lifecycle fixes are documented with intermediate review frames.
- Temporarily bypassing postprocessing made the visible-effect and display-pixel
  assertions fail. The bypass was removed and the renderer rebuilt. Earlier
  lifecycle and border checks were similarly checked against bypasses.
- A clean build produced no compiler warnings. Static analysis covered all 138
  C++ translation units; its three findings were fixed and the affected units
  rechecked successfully. Staged installation matches all 74 bundle files.
- All **42 original manifest hashes** remain unchanged.

The early `197_tiled_close_vacancy` IPC failures were rerun successfully, both
alone and in the final selected suite. They are no longer an outstanding failure.

## Reproducing visual comparisons

From `nix develop`:

```sh
python3 tests/tools/biri_postprocess_reference.py /tmp/biri-reference
mkdir -p build-debug/biri-frames
meson compile -C build-debug umbrielfx/umbrielfx-postprocess-test
BIRI_SHADER_FRAMES=build-debug/biri-frames \
  build-debug/umbrielfx/umbrielfx-postprocess-test \
  examples/shaders/biri /tmp/biri-reference
```

`BIRI_SHADER_FRAMES` also makes `umbrielfx-biri-shader-test` emit lifecycle/ring
PPMs. Its optional reference-directory argument accepts mechanically translated
lifecycle sources. Normal `just test` requires no generated reference fixtures.
Review images are build artifacts and are not installed as shader assets.

## Deliberate differences and validation limits

- Procedural borders replace the complete native double-band border. Independent
  simultaneous border/focus-ring geometries and per-band shader choices are not
  exposed. Padding changes neither layout nor input geometry.
- The illumination layer is below panels/overlays. Existing pinned/fullscreen
  stacking is preserved above it. Spill is suppressed during native fades and
  arbitrary ancestor shader transforms and omitted from close snapshots and
  isolated captures.
- Window shaders see the composited backdrop on the display; isolated captures
  contain only the selected client's surfaces. Native opening/closing fades
  temporarily suspend the persistent window pass. Custom shader transitions
  process the persistent effect inside their captured subtree.
- Active sampling effects recompose their output on actual frames. Cursor-local
  presets reduce the shaded rectangle, but do not enable partial direct scanout.
  Static and hidden effects do not request idle frames. Shader-only frames are
  capped independently of ordinary animation and client damage.
- Failed compilation drops a whole chain and is cached until source changes.
  Histories reset on geometry, transform, colour mode, program changes and lock.
  Output captures reuse committed results or the separate unfiltered composition;
  isolated toplevel sources have independent history.
- GLSL operates across an explicit sRGB compatibility boundary, with float
  intermediates for linear/HDR rendering. Hardware HDR appearance, physical KMS
  scanout/cursor behaviour and long-running mixed-display stress remain live-display
  validation tasks; the headless/GPU tests do not establish those results.

Copied Biri assets retain GPL v3 attribution and license. Package metadata lists
both the compositor and asset licenses. Frozen sources remain reference material.
