# Native effects cost review, 2026-09-30

The physical scale-1 SDR matrix now has three valid repetitions for every
available workload/revision/phase: **120 selected samples**. This combines 119
valid original samples with one explicitly identified supplemental sample; it
does not change the original matrix's failed result or certify unavailable
hardware gates. All 31 benchmark/report regression tests pass.

## Evidence and selection

| Evidence root under `/tmp` | Valid / captured | Selected |
| --- | ---: | ---: |
| `umbriel-cost-native-corrected` | 95 / 96 | 95 |
| `umbriel-cost-native-corrected-frozen` | 24 / 24 | 24 |
| `umbriel-cost-native-video-recheck` | 8 / 8 | 1 |

The excluded original is `run-1/baseline/video-light/declared-unused`. It lost
its DRM seat, logged atomic-commit permission errors and ended without an
output. The original raw trace, summary and failing matrix remain intact.
The selected substitute is the same relative cell in the video recheck. Its
other seven valid cells are confirmation evidence, excluded from the combined
medians to keep exactly three samples per comparison. A separate earlier
attempt, `umbriel-cost-native-final`, remains invalid due to unsupported
configuration keys and contributes no measurements.

The audit revalidated all 128 captured cells against exact-binary configuration
preflight, current CPU/GPU/message exports, raw process samples, complete
output/color boundary snapshots and scene admission where applicable. Exactly
the known seat-interrupted cell failed. All 128 raw trace hashes are distinct.
The recheck's eight configurations and accompanying shader files are byte
identical to the original advancing video configurations. All six artifact
hashes agree across the three metadata records and the current files.
Compositor logs contain no further DRM pause/resume or renderer error. Startup
seatd-to-logind fallback and unavailable D-Bus activation environment are
recorded separately from renderer failures.

Machine-readable sources, all trace hashes, the excluded sample, selected
sample provenance and unrounded medians are in
`/tmp/umbriel-effects-session/native-cost-review.json`. Reproduce the read-only
source audit with `/tmp/umbriel-effects-session/audit-native-cost.py`; it writes
only that supplemental review, without rewriting original matrices.

## Measurement conditions

Intel rendering to physical eDP-1, 1920×1080 at 143.998 Hz, scale 1, encoded SDR
XR24, display-only composition; fixed optimized LTO baseline and candidate builds with
matching Tracy instrumentation. Samples last 30 seconds with two seconds of
warmup. Process CPU includes capture handshake/profiling overhead and is
percent of one core. The helper is a silent synthetic transport source; it
acquires no audio device. Physical playback and microphone evidence is a
separate acceptance scope.

The scene workload compares the candidate's canonical transitions with
supported native baseline actions. It includes capture, lifecycle, landing and
rest periods. All three candidate active scene runs admitted every required
phase; the complete sample is not exclusively active-effect execution time.
The physical two-output scene workload is unavailable (24 planned cells),
explicitly excluded rather than counted as passed.

## Process cost and render distribution

Every entry below is the median of three independent samples. Render p95 is
the median of three per-sample p95 values, not a pooled percentile. RSS is
sampled process peak, not allocation high-water. Baseline has no audio helper.
Idle has zero render calls, so its render p95 is absent.

| Workload / time | Phase | Compositor CPU %, baseline / candidate | Candidate helper CPU % | RSS MiB, baseline / candidate | CPU render p95 ms, baseline / candidate |
| --- | --- | ---: | ---: | ---: | ---: |
| idle / advancing | disabled | 3.069 / 3.265 | 0.000 | 167.570 / 168.512 | — / — |
| idle / advancing | declared-unused | 2.969 / 3.101 | 0.000 | 168.102 / 168.855 | — / — |
| idle / advancing | active | 2.978 / 3.503 | 1.818 | 169.703 / 171.051 | — / — |
| idle / advancing | returned-off | 2.970 / 3.166 | 0.000 | 170.172 / 171.430 | — / — |
| small-updates / advancing | disabled | 13.834 / 14.685 | 0.000 | 182.184 / 183.395 | 0.321 / 0.317 |
| small-updates / advancing | declared-unused | 14.218 / 13.592 | 0.000 | 184.773 / 185.246 | 0.307 / 0.251 |
| small-updates / advancing | active | 15.536 / 17.769 | 1.833 | 190.629 / 191.969 | 0.424 / 0.528 |
| small-updates / advancing | returned-off | 14.753 / 13.920 | 0.000 | 190.516 / 192.074 | 0.301 / 0.308 |
| video-light / advancing | disabled | 22.254 / 22.893 | 0.000 | 184.926 / 186.133 | 0.291 / 0.310 |
| video-light / advancing | declared-unused | 22.180 / 23.218 | 0.000 | 186.266 / 187.070 | 0.289 / 0.299 |
| video-light / advancing | active | 23.218 / 24.846 | 0.951 | 189.250 / 191.793 | 0.411 / 0.557 |
| video-light / advancing | returned-off | 23.278 / 23.145 | 0.000 | 189.438 / 191.980 | 0.291 / 0.296 |
| scene-single-output / advancing | disabled | 15.389 / 15.471 | 0.000 | 188.520 / 188.020 | 0.429 / 0.445 |
| scene-single-output / advancing | declared-unused | 15.715 / 15.345 | 0.000 | 190.336 / 189.820 | 0.416 / 0.432 |
| scene-single-output / advancing | active | 16.968 / 29.939 | 0.851 | 193.164 / 201.465 | 0.649 / 0.416 |
| scene-single-output / advancing | returned-off | 15.900 / 15.407 | 0.000 | 193.504 / 201.305 | 0.435 / 0.438 |
| video-light / frozen | disabled | 22.134 / 22.786 | 0.000 | 185.238 / 186.492 | 0.286 / 0.298 |
| video-light / frozen | declared-unused | 22.002 / 22.727 | 0.000 | 186.488 / 187.430 | 0.283 / 0.288 |
| video-light / frozen | active | 23.022 / 24.971 | 1.016 | 189.746 / 190.871 | 0.423 / 0.536 |
| video-light / frozen | returned-off | 23.549 / 23.325 | 0.000 | 190.496 / 191.434 | 0.294 / 0.292 |

The supplemented baseline video/declared-unused median uses original runs 2
and 3 plus recheck run 1: 22.179616% CPU, 195,313,664 bytes RSS and 0.289480 ms
render p95. Candidate medians retain its three original samples: 23.217649%,
196,157,440 bytes and 0.299323 ms. The active rows are unchanged by supplementation.

## GPU queries and presentation cadence

Raw CSVs retain timestamp-query elapsed durations for named GPU operations.
The report's `gpu_query_elapsed_union_ms` (legacy alias
`gpu_busy_wall_union_ms`) is an elapsed span union, **not hardware busy time or
utilization**. `render_pass_submit` often spans approximately one refresh
interval, including submission/wait effects. Removing that zone can expose
per-operation query distributions, but cannot turn them into pure shader
execution time or justify summing overlapping zones. CPU render duration,
GPU query elapsed duration and presentation cadence are separate measurements.

Per-zone review of all 120 selected samples is retained in
`/tmp/umbriel-effects-session/native-gpu-zone-review.json`, including source
CSV hashes and each zone's count/mean/p50/p95/p99/max. The following active
operation measurements exclude `render_pass_submit`; each value is a median
of three per-run values. Counts describe operations, not frames. They provide
GPU-side elapsed-query cost evidence without requiring a utilization claim.

| Active workload / time | GPU query zone | Query p95 ms, baseline / candidate | Query count, baseline / candidate |
| --- | --- | ---: | ---: |
| small-updates / advancing | `draw_animation_texture` | 2.053802 / 1.966563 | 17752 / 17600 |
| small-updates / advancing | `fx_render_pass_read_to_buffer` | 0.070166 / 0.078182 | 720 / 720 |
| video-light / advancing | `draw_animation_texture` | 1.952553 / 1.952917 | 21670 / 21656 |
| video-light / advancing | `fx_render_pass_add_border` | 0.021354 / 0.021250 | 8664 / 8660 |
| video-light / advancing | `fx_render_pass_add_rect` | 0.210833 / 0.424218 | 4337 / 4333 |
| video-light / frozen | `draw_animation_texture` | 1.952812 / 1.952864 | 21641 / 21672 |
| video-light / frozen | `fx_render_pass_add_rect` | 0.210886 / 0.424948 | 4336 / 4334 |
| scene-single-output / advancing | `fx_render_pass_add_texture` | 0.215758 / 0.390047 | 1864 / 3322 |
| scene-single-output / advancing | `draw_animation_texture` | 2.070711 / 1.969635 | 6502 / 6944 |

These spans may include pipeline bubbles and CPU/driver scheduling between
query commands. `draw_animation_texture` covers authored legacy texture-shader
operations; border and buffer-copy zones identify those operations. There is
no dedicated GPU zone inside the new scene program, so the scene row does not
isolate authored scene geometry/composite execution. Different scene operation
counts also prevent treating that row as a same-effect speed comparison.
No overlapping zone durations are summed.

An independent raw-message review of the eight supplemental video cells is
retained at `/tmp/umbriel-cost-native-video-recheck/presentation-cadence-review.json`.
Baseline active had three intervals skipping one retrace among 4,333 recorded
presentations; candidate active had one among 4,335. Their actual presentation
interval p95 was 6.949 / 6.948 ms and maximum was 13.890 / 13.889 ms.
All six nonactive revision/phase cells had zero skipped retraces. These are
observed presentation cadence gaps under the declared continuous video
workload, not proof that a client buffer was ready at each skipped retrace.
Requested deadlines were not recorded, so requested-deadline misses remain
unavailable; this review does not relabel cadence gaps as deadline failures.

Output boundaries and compositor logs cannot exclude every transient event.
Physical two-output scanout isolation, HDR/unmanaged ten-bit appearance,
unfiltered-capture cost, pure GPU execution/utilization and separate
profiler-overhead controls are not established by this matrix. No additional
native session or audible test is launched by this review.

## Artifact identity

| Artifact | SHA256 |
| --- | --- |
| baseline | `40213999305e81ea3aea71f17b447c3f1d1c34b7a9a68efb5b46c11aa9278248` |
| candidate | `03741fd7e1b7702aafb6c5b0cf6b1b8254150abd7b02c0633ed2ba4cd4a35d37` |
| helper | `552735f7ccf8b2f53c63c645d5a9e8e5fef47550d712b9156ca28a793e61cf82` |
| client | `c53a027c83f33232832574c721530cbae27b297c8911d02018c212dbcd119061` |
| capture | `3f0e553c1507f622e1f3a91fdfd53a1cf9450c75a32d758936c882224e5445d4` |
| exporter | `9c03d9e11c9372fe8877f1aead62c8d9afe06adfa4c96bb18c938d33b759ff3d` |
