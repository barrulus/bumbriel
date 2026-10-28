# Invisible window capture exclusion PRD

Status: implementation contract for issue [#289](https://github.com/noctalia-dev/umbriel/issues/289), reconstructed from the agreed requirements and [bounded investigation](capture-exclusion-investigation.md). The current branch contains a harness-only prototype; it does not expose a supported privacy rule.

## Problem and intended result

A user sharing a desktop needs selected windows to remain visible and usable locally while being completely absent from screenshots and recordings. A black rectangle still obscures shared work. Exclusion must instead reveal the scene underneath, including its ongoing updates.

All four workspace layouts are in scope: scrolling, dwindle, master, and floating. This includes floating windows above tiled content in the three tiling layouts and overlapping windows in the floating layout. Layout mode must not determine whether a window can be protected. A protected tiled window keeps its allocation: the capture shows the background or eligible content behind that allocation, and other tiles do not reflow.

## User behaviour

The proposed public configuration is a boolean window-rule property, `block_capture`, defaulting to false. It follows normal rule precedence and reevaluation. The final schema name remains subject to upstream review; the prototype uses only `capture-exclude <window-id> <on|off>` in builds with test IPC enabled.

- Protection changes capture visibility only. Local pixels, focus, input, stacking, geometry, and layout membership retain their ordinary behaviour.
- Protection follows a window through movement, resizing, output moves, fullscreen, and floating/tiled transitions while its effective rule continues to match. A rule matching only floating windows stops applying when the window is tiled; an application-wide rule persists.
- Output and region captures omit protected surfaces and their associated decorations, shadows, popups, and retained images. No replacement rectangle or identifying label is drawn.
- Direct capture of a protected window is refused. Enabling protection stops all existing sessions for that window. Removing protection permits a fresh session; stopped sessions remain stopped.
- An allowed parent cannot override a protected ancestor. Owned transient and popup surfaces inherit protection. An independently protected child does not protect its unrelated parent or siblings. X11 related-surface ownership must be established before claiming equivalent protection there.
- Window titles and app identifiers remain available through existing metadata protocols. This feature protects captured pixels; it does not conceal the existence of a window.

## Capture contract

Once protection takes effect on the compositor event loop, no subsequent copy submission may include protected content. Cancel pending requests that have not reached that point, discard compositor-owned capture images and feedback histories that contain protected pixels, and regenerate safe output. Previously copied or submitted client buffers cannot be recalled, even if a completion event has not yet arrived.

Captures use their own scene visibility and occlusion. They must not reuse display visibility that has already culled content behind a protected opaque window. Client frame callbacks, buffer synchronization, and presentation sampling must account for content used by either composition. Capture-only content must continue updating.

Exclusion remains effective regardless of `effects.in_capture`. Blur, effects, animations, overview thumbnails, workspace transitions, closing snapshots, and feedback must not reintroduce protected pixels from another composition or an older image. Retained resources need stable protection ownership after the original view disappears.

Session lock and ordinary scene visibility take precedence. Removing a protected window from capture must never expose a disabled desktop, inactive workspace, content outside an ancestor clip, or another output. Allocation, renderer, and copy failures must fail closed for capture. Production handling must preserve local display progress independently; the prototype may decline the whole composed frame for unsupported scenarios.

## Protocol compatibility

Support output and window image-copy plus output/region screencopy, with SHM and DMA-BUF destinations where negotiated. Verify the pinned Umbriel portal and a real recording application before release.

Remove `zwlr_export_dmabuf_manager_v1` advertisement unconditionally at startup. Its raw export path exposes the displayed buffer and bypasses the renderer's alternative capture image. Withdrawing the global only after protection is enabled leaves already-bound clients usable. Clients requiring raw export lose that capability; image-copy DMA-BUF destinations remain a separate, supported path. The pinned portal uses image-copy.

The current dependency manages Xwayland directly through wlroots. Xwayland-satellite is not part of this implementation. Managed X11 windows, override-redirect surfaces, and transients need explicit ownership tests.

## Acceptance matrix

| Scenario                                                         | Required result                                                                                                             |
| ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| Protected float over two updating tiles, each tiling layout      | Local float unchanged; capture shows both live tiles, without protected pixels or black masking                             |
| Protected float over two updating floats in floating layout      | Local windows unchanged; capture reveals live underlying windows; protection survives switching to a tiling layout and back |
| Move, resize, fullscreen, output move, floating/tiled transition | Effective protection follows the window; layout geometry is unchanged by protection toggles                                 |
| Fully covered client visible only in capture                     | Fresh frames continue; buffer release and presentation accounting remain correct                                            |
| Protection enabled during two window capture sessions            | Both stop; pending copies before submission fail; new requests are valid stopped sessions                                   |
| Protection removed                                               | A fresh window session works; old handles cannot restart                                                                    |
| Protected tile                                                   | Allocation retained; neighbouring tiles do not reflow                                                                       |
| Popup, transient, X11 related surface, decoration, shadow        | Pixels follow effective owner protection                                                                                    |
| Close, overview, workspace switch, effect feedback               | No retained protected image appears after protection takes effect                                                           |
| Lock, inactive workspace, clipping, multiple outputs             | Capture never reveals content excluded by ordinary scene visibility                                                         |
| SHM and DMA-BUF; screencopy and image-copy; portal recorder      | Equivalent pixel protection, including scale, transform, and HDR transitions                                                |
| Allocation failure, renderer recovery, policy reload             | No fallback to unprotected capture; valid protocol lifecycle and usable local display                                       |

## Delivery steps

1. **Bounded investigation:** establish raw-export compatibility and owned source refusal/revocation against the pinned wlroots. Completed; see the investigation.
2. **Harness-only prototype:** independent ordinary-scene capture draw list, owned source destruction, simultaneous session revocation, and protected-window checks in scrolling, dwindle, master, and floating, including transitions between the floating and tiling layouts. Independently observe local and capture pixels. Keep unsupported effects and lifecycle paths outside the public API.
3. **Complete ownership and policy:** add effective rules, inherited protection and retained-resource ownership; guard synchronous `is_cast_target` rule reevaluation and enforce the copy-submission boundary during deferred teardown. Cover lock, output changes, X11, overview, and animations.
4. **Complete composition and failure handling:** integrate blur/effects/history, capture-only scheduling, buffer synchronization, colour/scale/transforms, all transports, and local-display continuity on capture failure.
5. **Release validation:** adversarial lifecycle and failure tests, real portal/recorder checks, and performance measurements on representative workloads. Confirm compatibility documentation before exposing `block_capture`.

The prototype uses conservative full-output damage and draws all eligible capture nodes. No performance target is claimed yet. Before release, measure no-capture and ordinary-capture regressions, protected-capture frame time, memory, and power impact on integrated and discrete GPUs; use those measurements to decide capture occlusion and damage optimizations.

## Scope limits

Protection does not stop an application from sharing its own content, another process from reading that application's data, or a camera from recording the display. It cannot clear frames retained by a recorder. Selective content inside a window, metadata hiding, and per-recorder permission UI are separate features.
