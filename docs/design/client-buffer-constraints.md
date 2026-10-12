# Client buffer constraints

Clients size and format their own `wl_shm` buffers from what the compositor
tells them. Getting either wrong is a protocol error, so the client dies.

## Initial layer-surface configure

`LayerSurface::handleCommit` arranges the output synchronously on
`initial_commit`. A client that requests 0x0 learns its size from that first
configure; deferring it to the next frame lets the client allocate first and get
`Invalid size (0)`.

The `Dirty::LayerArrange` flag recorded alongside is the retry:
`Output::arrangeLayers` returns early while the output has no effective
resolution.

## Stale window geometry

`View::committedContentBox` widens a tiled view's committed window geometry
towards the size the view was configured to, capped by what the surface
actually holds from the geometry origin. Presentation, the surface clip,
borders, blur, shadow, the resize-animation snap, and the size the `windows`
IPC listing reports all read that box.

Electron acks a configure and redraws at the new size while leaving
`set_window_geometry` at the size it had before, permanently. Clipping to that
box crops the content the client just drew and leaves the window occupying a
fraction of its tile, with no resize available because the tile itself is
already correct. Growing the box is bounded by the surface, so a client that
genuinely refuses the configured size, a minimum-size hint for instance, is
still presented at the size it committed.

## Capture readback format

`fx_texture_preferred_read_format` in `umbrielfx` never reports packed 24-bit or
packed 10-bit for shared-memory capture. For XR30 and XB30 targets it publishes
XRGB8888 when BGRA readback is available, and XBGR8888 otherwise. GLES performs
the depth conversion during readback. The NVIDIA blob reports `GL_RGB` /
`GL_UNSIGNED_BYTE` for opaque 8-bit targets, which maps to
`DRM_FORMAT_BGR888`. Clients assume a 4-byte pixel, derive `width * 4`, and
wlroots rejects it because a stride must divide by the pixel size. GLES2 always
allows `GL_RGBA` / `GL_UNSIGNED_BYTE` readback, so publishing a 32-bit format
avoids that mismatch.

Do not fix this by dropping `DRM_FORMAT_BGR888` from `umbrielfx`'s pixel format
table: the table also drives `wl_shm` advertisement and texture upload, and it
is identical to wlroots' gles2 table.

Explicit texture readback must also accept the bound framebuffer's
`GL_IMPLEMENTATION_COLOR_READ_FORMAT` / `GL_IMPLEMENTATION_COLOR_READ_TYPE`
pair. NVIDIA supports AB30 readback without advertising its texture-upload
extension. Applying only the upload capability check rejects valid 10-bit
readback and prevents the FP16 renderer tests from checking their pixels.

To reproduce without NVIDIA, hardcode `gl_format = GL_RGB`,
`gl_type = GL_UNSIGNED_BYTE`, `alpha_size = 0` after the `glGetIntegerv` calls
and capture with `grim`.

## HDR shader precision

Client texture conversion and the output transform both require unconditional
`highp float`. This preserves the output shader as the renderer-wide precision
gate: implementations without fragment highp cannot initialize the renderer,
and HDR is never exposed with arithmetic known to be insufficient.

On NVIDIA 610.57.04, relying only on the shader macro selected mediump and
corrupted PQ decoding: `color-pq-roundtrip` returned red 90 instead of 96.
Forcing highp fixed the test, while explicit highp samplers alone did not.

GLES3 detection remains limited to the packed 2_10_10_10 texture type, which
is core functionality in GLES3. The capability is enabled by either the
context version or the extension string. Framebuffer readback retains its
separate implementation-format check.

## HDR output transitions

Every enabled configured-state commit must carry a scene buffer built for that
same pending output state. This is especially important for HDR image-description
and render-format changes. Without a buffer, wlroots may supply a cleared buffer
for the reconfiguration, and a following nonblocking scene commit can race the
DRM modeset and leave that cleared buffer visible.

Dynamic HDR policy changes therefore schedule a full frame. At the frame
boundary, `Output::applyConfiguredState` enables reconfiguration, damages the
whole output, builds the scene into a copy of the staged state, requires both
`WLR_OUTPUT_STATE_BUFFER` and a nonnull buffer, then commits the state and
rendered frame together. Output creation also creates its scene output before
initial configuration, preserving this invariant when DRM outputs are recreated
after resume.

Failures before the backend commit are retryable. A pending-state copy failure,
scene build failure, missing buffer, or temporary neutral-mask failure stops the
format sequence without trying SDR, VRR, or mode fallbacks. The requested policy
remains pending for the delayed frame retry because none of these failures proves
that the staged output format is unsupported.

A backend commit failure is instead treated as a rejection and retains the
existing format fallback behavior. If a fallback commits successfully,
`HdrTransition` records the requested policy as handled and reports the fallback
reason instead of retrying HDR every frame. If no candidate commits, the
transition remains pending for a later frame.

A full geometry commit made while the session is locked, or before the output is
bound to the logical output layout, renders through a temporary opaque
backdrop-colored mask. Its logical extent is derived from the pending mode,
transform, and scale, and it remains above the scene through state building and
the backend commit. Grouped output-management commits use the same protection.
This prevents an unbound output from sampling the scene origin and prevents a
geometry change from exposing content beyond a lock blank that has not yet been
resized.

`output-hdr-transition` covers transition bookkeeping, build-before-commit
ordering, and the rendered-buffer guards. `output-format-sequence` covers the
distinction between retryable preparation failures and backend rejection. The
headless color-management check covers policy entry and release, but a real HDR
monitor is still required to validate the KMS transition.

## Implicit scene-buffer primaries

A raw `wlr_scene_buffer` uses zero primaries as an internal unset sentinel.
This can remain visible while a new surface or a copied snapshot enters an
animation, before protocol color metadata has been applied. Unset colorimetry
has the same rendering meaning as implicit sRGB.

The scene render boundary snapshots the named value and resolves zero to sRGB
before calling `wlr_color_primaries_from_named`. The converter remains strict
for invalid named values. `color-scene-unset-primaries` builds a composited
frame from a raw scene buffer and covers this boundary; removing the resolution
causes the check to abort in the converter.

## HDR capture view

Capture protocols advertise their shared-memory and DMA-BUF constraints before
they lock the output for an attach-render frame. For XR30 and XB30 sources,
shared-memory capture is advertised as 8-bit and GLES converts the SDR sidecar
during readback. The sidecar itself must continue to use the output buffer's
DRM format because DMA-BUF capture may already have negotiated that 10-bit
storage, and texture substitution must remain storage-compatible. Its stored
values are Gamma 2.2 SDR even when its DRM format is 10-bit.

Each output owns one shared FP16 blend buffer and one shared SDR capture
sidecar. Their lifetime is tied to that output, rather than to its swapchain
buffers, so changing swapchain depth does not duplicate either allocation. The
blend buffer always represents the latest complete composed frame. Incremental
rendering updates the region drawn for the current frame while preserving the
previous frame everywhere else.

Creating or resizing the shared blend buffer requires whole-output damage for
that frame. A new buffer is cleared and has no valid pixels outside partial
damage, including pixels that blur may sample. The full redraw establishes the
complete-frame invariant before incremental damage resumes. An untransformed
composition or direct scanout bypasses the shared target and invalidates it, so
the next transformed composition also performs a full redraw.

`umbrielfx` creates the capture sidecar lazily from the pre-output-transform
linear blend buffer. The sidecar is generation-matched to that blend buffer:
capture can reuse it only when the requested output buffer has the same blend
generation, dimensions, and DRM format. A conversion for a newer generation
does not overwrite the sidecar while a capture texture still holds it. This
conversion can happen during texture import, outside the normal `umbrielfx`
render pass, so its EGL context must be current while the framebuffer is
allocated. Export-DMA-BUF frames bypass the SDR sidecar and retain the output's
native representation.

Output color LUTs are cached as renderer-local textures keyed by the immutable
LUT transform. Reusing an output transform therefore reuses its uploaded
texture, while transform and renderer destruction both release the cache entry.

## Windows-scRGB luminance

Windows-scRGB and generic extended-linear content share the same transfer
function, but not the same reference-white convention. Windows-scRGB defines a
linear value of 1.0 as 80 cd/m2 and uses 2.5375, or 203 cd/m2, when compositor
processing needs an assumed reference white. Treating every extended-linear
buffer as Windows-scRGB would incorrectly dim parametric extended-linear
content.

Umbriel therefore marks only image descriptions created by
`create_windows_scrgb` with an `umbrielfx` luminance multiplier of `80 / 203`.
`umbrielfx` applies that multiplier while normalizing the buffer into its
reference-white-relative blend space. The output transform subsequently maps
the normalized reference white to the configured output `sdr_white` level.
Buffers using this multiplier cannot use direct scanout because scanout would
bypass the conversion.

## Overview color mirrors

Overview cards use raw scene buffers that mirror each committed surface. A
normal scene surface is reset to wlroots' protocol-owned color state on every
commit. Umbriel repairs Wine compatibility descriptions at the render
boundary, but a raw overview mirror is not a scene surface and is not included
in that repair pass.

After copying ordinary scene-buffer properties, the overview must therefore
apply the compatibility manager's authoritative committed transfer function,
primaries, and luminance multiplier directly to its mirror. Otherwise a game
commit while overview is open reinterprets Windows scRGB as SDR and loses PQ
BT.2020 metadata.
