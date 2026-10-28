# Capture exclusion feasibility investigation

Status: bounded investigation complete, 28 October 2026. This is an implementation recommendation, not a shipped feature or a release validation result.

The findings below describe the upstream baseline. The [PRD](capture-exclusion-prd.md) reconciles them into the feature contract; the final section records the subsequent harness-only implementation and its validation.

The required behaviour is invisible exclusion: a protected floating window remains visible locally while a capture shows the updating scene beneath it. Scrolling, dwindle, and master must support this. Protected tiled windows retain their layout allocation; neighbours do not reflow. The earlier draft PRD is absent from the current checkout and reachable Git history, so this note records the findings against the current upstream implementation.

## Baseline and boundary

- Umbriel: `c028006b26d18b757fc0b1777b08dedbe3ee1b7d`, fetched from `noctalia-dev/umbriel` `main` on 28 October 2026.
- Branch: `investigate/289-capture-exclusion`.
- Worktree: `/tmp/umbriel-289-capture-exclusion`.
- Dependency: the worktree's Nix shell supplies wlroots `0.20.2`, library `/nix/store/ak5zv5nrsb3ihcsqcyfrcm13lpk0qdnk-wlroots-0.20.2`, with source `/nix/store/r2zcb3d3dgmz09qjaqsy945inal2h41r-source`.
- Portal: upstream's lock file pins `xdg-desktop-portal-umbriel` `e24762d9c75e294f89dbe7a0e3fd55c5cdc3ff0a`, inspected at `/nix/store/3s62hihfq53p3gsjz3qyygfgn3vm3v8z-source`.
- The original checkout and its uncommitted session-lock changes remain untouched.

Questions: can raw export honour exclusion through the existing API, and can a window source be refused or revoked without destroying its displayed window? Stop after tracing those paths, checking the pinned portal's protocol use, and one CPU-only lifecycle probe with a negative control. Rendering implementation, GPU workloads, performance profiling, and full portal integration are outside this round.

## Recommendation

Proceed with capture exclusion using the existing screencopy and image-copy protocols and Umbriel's owned scene-capture helper. For the first implementation, remove raw export-DMA-BUF advertisement unconditionally from compositor startup. This is a proposed compatibility change, not applied by this investigation. If supporting raw-export-only clients is required, defer that decision to an explicit wlroots integration change before implementing the feature.

Introduce an owned source-revocation entry point in `umbrielfx`, refuse new protected-window sources with a valid inert protocol object, and create a fresh source only after protection is removed. There is no need to destroy the real window or replace the image-copy protocol implementation.

The next rendering prototype must build capture visibility independently and account for client buffers used by either composition. Extending the existing effects capture pass with only a draw-time exclusion flag is insufficient.

## Raw export reads the displayed buffer

Umbriel creates `wlr_export_dmabuf_manager_v1` in [server.cpp](../../src/server/server.cpp). Its [output capture-lock accounting](../../src/output/output.cpp) deliberately subtracts raw export requests from capture-specific rendering demand.

In the pinned wlroots `types/wlr_export_dmabuf_v1.c`, `frame_output_handle_commit()` obtains DMA-BUF attributes directly from `event->state->buffer` and sends the descriptors to the client. There is no renderer import at which Umbriel's alternative capture image can be selected. This is distinct from an image-copy client supplying a DMA-BUF destination, which can receive filtered content through a renderer copy.

The manager exposes no capture-request approval callback, alternate-buffer provider, or public per-frame cancellation function. Editing private listeners or exporting a different identity for the physical scanout buffer would create additional ownership and compatibility obligations; neither is a supported integration established by this investigation.

The CPU probe confirms that `wl_global_remove(manager->global)` does not revoke an already-bound manager. A subsequent request reaches the normal handler and receives its disabled-output cancellation response. The probe does not export pixels; the displayed-buffer bypass is established by source inspection.

Startup-wide removal avoids the existing-binding problem when users add protection through config reload. Creating the manager whenever no rule currently matches, then withdrawing it later, is insufficient. An explicitly designed startup mode would be another product surface and is not needed for the recommended first implementation.

The pinned portal's `src/wayland/wayland.cpp` uses `ext_image_copy_capture_manager_v1` for output and window capture and has no raw-export or screencopy path. Removing raw export therefore preserves the portal's selected capture protocol. This is a source compatibility finding, not an end-to-end portal pass. External clients that require raw export will lose that capability. Update the user-facing protocol documentation and the global-presence expectations in [security_context.sh](../../tests/harness/checks/protocol/security_context.sh) with any removal.

## Window source refusal and revocation are feasible

[Server::onToplevelCaptureRequest](../../src/server/server_events.cpp) caches one source per view. The source is created from a separate capture scene; [view.cpp](../../src/view/view.cpp) constructs that scene for both XDG and Xwayland client surfaces.

The implementation of `wlr_ext_image_capture_source_v1_create_with_scene_node()` is owned by [umbrielfx capture.c](../../umbrielfx/types/scene/capture.c). Its `source_destroy()` removes source listeners, calls `wlr_ext_image_capture_source_v1_finish()`, destroys the private scene output, and finishes its private output/backend. A scene-node destruction currently invokes it. The public [scene_capture.h](../../umbrielfx/include/umbrielfx/types/scene_capture.h) only exposes scale adjustment, so add a deliberate destruction/revocation operation for sources owned by this helper.

Do not call the base `finish()` alone on the helper's allocation: that would leave helper-owned listeners, output, and allocation alive and allow later double teardown. Route through its complete owned destructor. Preserve the capture scene and display scene so a newly authorized request can create a fresh source.

The pinned wlroots base-source finish operation synchronously notifies all attached image-copy sessions. Each session fails a pending frame with `STOPPED`, calls the source stop hook, sends the session `stopped` event, and becomes inert. Base finish also makes previously issued source resources inert. Reusing those old handles cannot restart a session.

For a new protected-window request, call `wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(request, NULL)`. Despite its name, this produces a valid inert source; creating a session from it sends `stopped`. Simply returning from the request listener leaves the requested object uncreated: the manager does not supply an automatic refusal after emitting `new_request`.

The pinned portal handles `stopped` through its registered callback. Whether a recorder clears or retains its last delivered image still requires real-client testing; revocation cannot retract previously shared pixels.

### Reentrancy and rule evaluation

Current upstream includes `match.is_cast_target`. New-session and session-destroy callbacks synchronously refresh window rules in [server_events.cpp](../../src/server/server_events.cpp). Revoking a source can therefore re-enter rule evaluation while its sessions are being torn down.

The implementation needs a revocation guard and a safe teardown boundary outside an active source render/copy stack. If destruction is deferred, a protection flag must immediately prevent new copies; deferring the destructor alone is not enforcement. Test multiple sessions and a rule that changes as the last session ends.

Define protection persistence in terms of effective rules and inheritance. A rule matching `is_floating = true` legitimately stops matching on tiling. Application-wide matching persists across layout changes. Retained images need stable ownership and protection state even after their originating view disappears.

## Protection begins before copying into client memory

The pinned image-copy implementation writes SHM destinations through `wlr_texture_read_pixels()` and submits rendering into DMA-BUF destinations before it sends `ready`. Cancelling a notification cannot retract those writes or submitted GPU work.

The implementable contract is: once protection takes effect on the compositor event loop, no subsequent copy submission may include protected content. Cancel pending requests that have not crossed that boundary, invalidate compositor-owned capture images and histories, and regenerate safe output as needed. Already exposed or submitted client-buffer content is outside the revocation guarantee even if its completion notification has not yet arrived.

The installed dependency is stock `0.20.2`; the separate local wlroots checkout contains asynchronous-copy changes and is not this worktree's dependency. Revisit submission, fence completion, and cancellation ownership if that dependency changes.

## Rendering work still required

The current [scene output implementation](../../umbrielfx/types/scene/wlr_scene.c) computes one render list and background visibility, renders the effects-excluded composition, then uses the same list for the display. It suppresses the display pass's `output_sample` notifications with `sampled_earlier`.

Invisible exclusion changes which buffers each pass actually uses. Capture can expose buffers culled from the display; the display still samples the protected window that capture omits. A correct implementation needs independent visibility and damage for the two compositions and sampling/release accounting covering buffers used by either pass. Display-only protected buffers cannot inherit a blanket assumption that the capture already sampled them.

Keep capture exclusion independent of `effects.in_capture`. It must preserve session-lock suppression, workspace visibility, clipping, and output boundaries. Do not expose inactive workspaces or lock-hidden desktop content while reconstructing the scene behind a protected window. Verify frame callbacks and suspension for clients visible only in capture; current upstream has explicit background-render and cast-target handling.

Xwayland is now managed directly by wlroots, replacing the PRD's xwayland-satellite assumption. Owned X11 views have an isolated capture scene, but override-redirect windows and related-surface ownership need a separate audit in the rendering prototype.

## CPU probe evidence

Artifacts are outside the repository at `/tmp/umbriel-289-investigation`:

- `probe.c`: a private Wayland client/server socket pair using the actual pinned library, a synthetic source, and a disabled headless output. No renderer is created.
- `run.py`: generates protocol bindings, compiles with `-Wall -Wextra -Werror`, and runs each probe with a ten-second timeout.
- `positive.log` and `negative.log`: observed results.

Reproduce from this worktree:

```sh
nix develop --command python3 /tmp/umbriel-289-investigation/run.py
```

Verified:

1. Two sessions can share one source; revocation stops both.
2. A real SHM buffer attached to a pending capture receives `failed(STOPPED)`, with no `ready` event.
3. An already-issued source cannot restart capture after revocation.
4. `request_accept(NULL)` produces a valid inert source and clean refusal.
5. A fresh source permits a new session after protection is removed.
6. Withdrawal of the raw-export global leaves an existing manager binding usable.
7. Client/server teardown completes cleanly.

The negative control omits the source-finish call and fails at the expected session-stop assertion. The positive run passes all checks. Initial probe setup failures were corrected before these runs: missing libdrm include flags and omission of the mandatory ARGB8888 SHM format.

The probe exercises wlroots protocol lifecycle, not Umbriel's proposed destructor API, GPU buffer cancellation, actual pixel filtering, or a real portal/recorder. It is a scratch investigation artifact, not a new permanent harness check. No compositor installation or desktop session was changed.

## Next bounded implementation step

First reconcile the PRD with this baseline: direct Xwayland, the copy-submission boundary, unconditional raw-export removal as a compatibility decision, explicit inert-source refusal, revocation reentrancy, session-lock precedence, and effective-rule semantics. The absent draft must be restored or reconstructed from the prior agreed requirements before treating it as the implementation contract.

Then implement the source-revocation entry point and a narrow capture-visibility prototype in this isolated branch. Use one protected floating window above two updating tiled clients, with independently observable display and capture images. Run that same scenario in scrolling, dwindle, and master, including movement and floating/tiled changes. Verify that source revocation leaves the local window intact and handles multiple sessions.

Exit criteria are correct local/captured pixels, continued updates beneath the protected window, safe source lifecycle, and buffer lifetime accounting. Keep production completeness separate: retained workspace/close images, blur and feedback, transient and X11 ownership, HDR/scaling, all capture transports, failures, and recorder integration remain required before release. Performance assessment begins once representative protected composition exists; this investigation makes no performance claim.

## Prototype implementation and results

The bounded implementation is complete on the same branch and baseline. `capture-exclude <window-id> <on|off>` exists only with test IPC enabled. No public `block_capture` rule is exposed.

- The owned scene-source destructor stops existing sessions without destroying the local window or its capture scene. Protected requests use an inert source; a fresh source works after protection is removed. The test client now stops retrying when the protocol reports `STOPPED`.
- Raw export-DMA-BUF advertisement and its special render-lock subtraction are removed. User and maintainer protocol documentation and the host-global test reflect the compatibility change.
- Capture draws an independent list, respecting enabled ancestors, output bounds, and clipping, while leaving display visibility intact. The prototype draws all eligible nodes and uses full-output damage.
- Buffers sampled in capture retain per-output membership for frame callbacks and synchronization notifications. Display-only protected buffers still emit sampling notifications. Membership resets on each capture build; ordinary display visibility remains unchanged.
- Unsupported effects and eligible blur nodes reject capture composition. Capture-image save failure rejects the output state rather than exposing the local buffer. This prototype can hold the previous local frame in such cases; independent local-display progress is a production requirement.

Validation on this worktree:

| Check                                | Result                                                                                                                                                                                                                                                                  |
| ------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Meson unit and renderer suite        | 146 passed, none failed or skipped                                                                                                                                                                                                                                      |
| Renderer `effects-capture-exclusion` | Independent local/capture pixel reads; fully occluded blue-to-green update; one sample per capture-only/display-only buffer; disabled ancestor; ancestor clip; save failure; removal of protection                                                                      |
| Harness `render/capture_exclusion`   | Opaque output-sized float above two live tiles in scrolling, dwindle, and master; frame-callback-driven repaints; movement; floating/tiled transitions; unchanged protected tile geometry on toggles; two revoked sessions; new request refused; fresh session restored |
| Focused harness regressions          | Seven passed: capture exclusion, both security-context checks, window capture scale, window effects capture, screen/cursor effects, and renderer recovery                                                                                                               |
| Concurrent stress                    | 32 capture-exclusion checks and 32 copies of the exact security-context check passed                                                                                                                                                                                    |
| Negative controls                    | Removing capture membership stalls the fully obscured client's repaint; suppressing protected display-only sampling fails its renderer assertion. Both mutations were restored and rebuilt                                                                              |
| Formatting and lint                  | Repository formatting and targeted clang-tidy on all seven changed C++ translation units passed                                                                                                                                                                         |

Logs and scratch scripts are in `/tmp/umbriel-289-investigation`. The protocol stress recipe's substring selector also matches `security_context_rule`, so that run used 32 isolated copies of the exact `security_context.sh` script through the same harness. No compositor was installed or run against the user's desktop session.

These results establish the ordinary-scene mechanism, including capture-only client progress and sampling bookkeeping. They do not establish complete privacy enforcement across retained images, effect feedback, popup/transient and X11 ownership, real session-lock transitions, multiple outputs, HDR, DMA-BUF destination synchronization, renderer recovery while protected, or portal/recorder integration. The renderer's disabled-ancestor check covers scene suppression, not the full session-lock lifecycle. Reentrant rule-driven revocation remains deferred: the prototype invokes the destructor from IPC outside source rendering and clears view ownership before session callbacks run.

The next implementation step is stable protection ownership for live and retained surfaces, with a guarded rule-driven revocation boundary. Complete that and effect/history isolation before exposing the public rule. Preserve the prototype's independent visibility and buffer-accounting checks while replacing conservative overdraw and damage with measured production behaviour.
