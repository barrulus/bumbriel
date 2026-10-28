# Invisible window capture exclusion PRD

Feature contract for [issue #289](https://github.com/noctalia-dev/umbriel/issues/289). Configuration and compatibility are documented in [Capture protection](../user/window-rules.md#capture-protection).

## Intended result

Selected windows remain visible and usable locally while disappearing completely from screenshots and recordings. Capture reveals the scene underneath, including ongoing client updates. It draws no replacement rectangle.

All four workspace layouts are in scope: scrolling, dwindle, master, and floating. Floating windows above tiles and overlapping windows in the floating layout behave identically. Protected tiles retain their allocation; other tiles do not reflow.

## Policy and lifecycle

`block_capture` is a boolean window-rule property, defaulting to false, with normal last-matching-rule precedence and reevaluation. Protection follows effective rules through movement, resizing, fullscreen, output moves, and layout changes. A rule conditional on floating state stops applying when that condition stops matching.

- Local visibility, focus, input, stacking, geometry, and layout membership retain their normal behaviour.
- Popups and owned transients inherit protection; a child's explicit false cannot override a protected ancestor. An independently protected child does not protect its parent or siblings.
- Decorations, shadows, overview cards, and retained closing images follow owner protection. Retained resources preserve protection after owner destruction.
- X11 surfaces with no reliable owner are conservatively excluded while an X11 view is protected. This can hide unrelated unmanaged surfaces and must remain documented.
- Direct capture of a protected window is refused. Enabling protection stops every existing session; removing it permits fresh sessions without reviving stopped ones.
- Metadata remains available. This feature protects captured pixels, not the existence of a window.

Once protection takes effect on the compositor event loop, no subsequent copy submission may include protected content. Invalidate saved capture images and feedback histories immediately. Reentrant rules such as `match.is_cast_target` must block copies immediately and defer source destruction until the current protocol callback returns. Already copied or submitted client buffers cannot be recalled.

## Rendering contract

Capture uses independent scene visibility rather than display occlusion. Fully covered clients visible only in capture keep receiving frame callbacks and correct buffer-sampling notifications. Session lock, inactive-workspace visibility, ancestor clipping, and output bounds still apply.

Blur, effects, animations, workspace transitions, and feedback must use eligible capture inputs, regardless of `effects.in_capture`. Display and capture histories and light caches are separate. Allocation, renderer, and copy failures refuse capture without freezing the local display. Format negotiation remains possible even when pixel reads are refused.

Raw export-DMA-BUF is removed at startup because its raw displayed-buffer path bypasses filtering. Withdrawing the global after protection is enabled would leave already-bound clients usable. Screencopy and image-copy remain the capture interfaces, including image-copy DMA-BUF destinations.

## Acceptance and release validation

| Scenario                                                | Required result                                                                         |
| ------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Protected float in each of the four layouts             | No protected pixels; underlying windows continue updating                               |
| Protected tile; movement and layout transitions         | Allocation preserved; effective protection follows the window                           |
| Rule reload during multiple window sessions             | All sessions stop; new requests are refused; removing protection permits fresh sessions |
| Native/X11 transients and popups                        | Owner protection applies, with the documented conservative X11 fallback                 |
| Overview, close, blur, effects, workspace transitions   | No protected pixels from live or retained resources                                     |
| Lock, clipping, scale, transform, HDR, multiple outputs | Ordinary scene restrictions and colour handling remain correct                          |
| Capture allocation failure and renderer recovery        | No unprotected fallback; local display continues                                        |
| SHM/DMA-BUF, screencopy/image-copy, portal recorder     | Equivalent protection at copy submission                                                |

Automated renderer and headless compositor checks accompany the implementation. Before marking the PR ready, complete real portal/recorder validation, native multi-output and HDR/scale coverage, and representative integrated/discrete GPU performance measurements. The initial implementation uses full-output damage and draws every eligible capture node; no performance target is claimed.

## Scope limits

Protection cannot stop an application sharing its own content, another process reading its data, or a camera recording the display. Per-recorder permissions, selective content inside a window, and metadata hiding are separate features.
