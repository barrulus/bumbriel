# Scene-effect cost matrix

`scene_cost.py` records compositor and explicitly named helper CPU/RSS separately.
It attaches read-only to an already running session and does not launch clients,
change configuration, infer frame time from polling, or claim GPU measurements.
Use the same release build options, physical outputs, workload seed and sample
duration for PR2 baseline `b1e33849` and the candidate. Warm up before sampling;
record at least three runs per cell. Keep baseline and candidate artifacts apart.

Run each workload in four configurations: effects disabled; identical presets
and sources declared but unused; selected active effects; then returned to off
after resources have retired. Preserve the complete configuration and revision
beside the report. For baseline cells unavailable in PR2, record native rendering
of the same workload and label the active feature as candidate-only.

| Workload | Fixed stimulus and additional evidence |
| --- | --- |
| `idle` | Stationary desktop, then unchanged silent audio. Record native/effect commit counters before/after and process tree; unused definitions must add no helper. |
| `small-updates` | Fixed-size terminal-like surface updates (one cursor rectangle at 2 Hz), overlapping window/overlay effect boxes. Capture damage/copy counts alongside CPU/GPU. |
| `video-light` | Repeated deterministic full surface updates at display cadence under border light; repeat with effect time frozen. Helper CPU is a separate PID, and unchanged shader time must not suppress required emission updates. |
| `scene-two-output` | Repeat melt, carousel enter/navigation/accept and water open/close with two moving neighbours; second output has the same small-update workload in both revisions. Record peak scene reservation and restoration to zero after each transition. |

Repeat at 1920×1080 and 3840×2160 where supported, scale 1 and a fractional scale,
RGBA8 encoded, unmanaged ten-bit encoded FP16, and managed linear FP16. Run both
display-only and display plus unfiltered capture. Record atomic resource-budget
fallbacks as fallbacks, not as fast successful scene rendering. Full-resolution
pair/window profiles are allowed to decline when complete reservation exceeds
256 MiB per output; do not silently reduce their sources.

Example (replace actual PIDs and revision):

```sh
python3 tests/bench/scene_cost.py \
  --compositor-pid 1234 --helper-pid 1235 \
  --revision candidate-commit --phase active --workload video-light \
  --resolution '1920x1080@60+1920x1080@60' --scale '1,1' \
  --format 'RGBA8 encoded' --roles display+unfiltered \
  --inputs 'pulse border; video seed=7; playback linear16-v1; time advancing' \
  --duration 30 --output /tmp/active-video.json
```

Record actual presentation/render trace frame durations separately, including
missed presentation deadlines. Export one actual frame per CSV row with
`frame_ms,missed` columns and pass `--frame-times-csv` to summarize mean, p50,
p95, p99, maximum and missed count. Do not substitute IPC polling timestamps,
headless FPS, or `clock-advance` intervals for these durations. Attach the GPU
profiler's raw trace and derived GPU active time separately; the sampler leaves
GPU cost explicitly unmeasured. Keep tracing overhead identical in both runs.

Physical two-output direct-scanout restoration, HDR appearance, touchpad reversal
and the permitted overlap handoff need separate session recordings/review.
Headless regression timing cannot certify any of those properties.

## Repeatable runner

`scene_matrix.py` launches a private compositor session and keeps that same
session alive through disabled → declared-unused → active → returned-off. It
uses a private runtime directory, disables autostart and Xwayland, removes the
D-Bus session address, and never acquires a physical audio device. The candidate
uses an external synthetic silent source; the baseline has no audio/scene
schema and is explicitly labelled as the ordinary native equivalent. The
baseline declares only its supported legacy presets in the unused phase.

Before starting any compositor, every generated phase/workload configuration is
checked by the exact baseline or candidate binary with `config validate`. The
runner archives shader siblings, validator output, exit status, and configuration
hashes under `config-validation/`; any diagnostic or validator failure stops the
run. `--validate-configs-only` performs this check without opening a display and
can run from an existing desktop. Reports reject cells without successful
preflight evidence or whose measured configuration differs from the checked
configuration. Earlier runs without this evidence cannot certify acceptance.
During a native run, Super+Shift+Escape exits the benchmark compositor and aborts
the runner; partial results remain available for diagnosis.

Build the deterministic Wayland client in the development shell:

```sh
nix develop --command python3 tests/bench/scene_matrix.py \
  --build-client /tmp/umbriel-cost-client
python3 -m unittest discover -s tests/bench -v
```

The client uses release-tracked shared-memory buffers, follows configure sizes,
and commits fresh buffers for native resize synchronization. `small-updates`
changes an 8×16 cursor rectangle at 2 Hz; `video-light` produces deterministic
full-surface frames paced by actual Wayland frame callbacks. Its JSONL log
records configure, commit request and callback events using CLOCK_MONOTONIC.
These are workload evidence, not physical presentation timestamps. Seed and
pixel sequence are fixed in the source.

The runner accepts explicit, identically configured baseline/candidate release
binaries and Tracy tools. Instrument both revisions with the same CPU/GPU zones
and `umbriel.output` event messages. The GLES timer path must use completed
query timestamps for context calibration and the canonical unsigned query
availability getter. On some NVIDIA EGL implementations the signed EXT getter
and direct `GL_TIMESTAMP_EXT` getter leave their output untouched despite
reporting no GL error. Verify calibrated traces in both revisions before using
them for comparison.

Validate the capture/export tools themselves with a short complete phase cycle.
The packaged Tracy 0.13.1 capture can serialize before its processing thread has
stopped; partially received callstack rows can also disagree with its inferred
row count. A complete compressed file can therefore still fail to load. The
validation tools wait for the worker to stop, count the actual serialized rows,
and reject reads beyond the compressed block sequence. Preserve these tool
patches and tool hashes with the comparison. An export failure invalidates the
cell; do not repair its timing data or silently treat it as zero work.

The runner also refuses an occupied Tracy port before launching each session.
This catches a compositor left behind by an interrupted runner; the advisory
file lock alone cannot prevent capture from attaching to that old session.

Example disposable headless smoke (one run and short cells validate plumbing;
they are not the acceptance performance report):

```sh
python3 tests/bench/scene_matrix.py \
  --baseline /path/to/baseline/umbriel --candidate /path/to/candidate/umbriel \
  --capture /path/to/tracy-capture --exporter /path/to/tracy-csvexport \
  --client /tmp/umbriel-cost-client/scene-workload \
  --helper /path/to/audio-synthetic \
  --backend headless --resolution 1280x720 --duration 10 --runs 1 \
  --output /tmp/umbriel-cost-smoke
```

Native runs must be launched **from an unused TTY after leaving the current
compositor's active seat**, with no inherited `WAYLAND_DISPLAY` or `DISPLAY`:

```sh
python3 tests/bench/scene_matrix.py \
  --baseline /path/to/baseline/umbriel --candidate /path/to/candidate/umbriel \
  --capture /path/to/tracy-capture --exporter /path/to/tracy-csvexport \
  --client /tmp/umbriel-cost-client/scene-workload \
  --helper /path/to/audio-synthetic \
  --backend drm --allow-native-session --outputs eDP-1 \
  --resolution 1920x1080 --scale 1 \
  --output /tmp/umbriel-cost-native
```

Default duration is 30 seconds with three repetitions, alternating revision
order between repetitions. The runner additionally includes
`scene-single-output`, which performs the same melt/carousel/window lifecycle
cycle with two neighbours on one display. On a single physical display,
`scene-two-output` produces an explicit unavailable artifact; it is never
substituted with a virtual output and described as physical isolation. The
feasible single-display default matrix takes approximately 48 minutes plus
warmups and exports. Run the frozen-time video variant separately by adding
`--workloads video-light --time-mode frozen` and a new output directory. This
sets the preset's speed to zero while real client updates continue; it does not
freeze the compositor's native clock. That full variant adds about 12 minutes.

The scene cycle switches workspaces, enters/navigates/accepts the carousel,
then opens and closes another window while neighbours follow native layout.
Before opening, it focuses the primary window and allows the resulting native
workspace switch to land with the same 250 ms pause in both revisions. Both use
an explicitly configured 180 ms linear animation timeline; the default spring
curves determine their own duration. IPC uses
the native Unix-socket protocol directly: repeatedly starting the instrumented
CLI would skew the stimulus. Cycle steps execute sequentially and their actual
execution is recorded; elapsed time alone does not establish a complete cycle.
The baseline performs corresponding native workspace/window actions.
Inspection snapshots record observed reservations and fallback reasons. A
candidate scene cell is marked `scene_cost_validated` only when every requested
pair, carousel and window phase was observed active with a nonzero reservation
on the primary output. Opening and closing are separate requirements. Each
request retains its observed state, fallback or missing admission; a successful
close cannot hide a failed opening. Whole-cell timing includes rest and native
landing work as well as active effects; it is not an isolated per-effect cost.
Short smoke cells under eight seconds cannot cover a complete cycle. Observed
reservation maxima are not allocator high-water measurements, and nested
reservation aliases are never summed. Keep fallbacks in the report; they do not
count as fast successful effects.

Each cell retains the configuration and shader snapshots, raw `.tracy` file,
filtered CPU CSV, GPU CSV, presentation-message CSV, client request/callback
logs, process samples, output/color metadata, and before/after effect state.
`summary.json` separates compositor CPU/RSS from compositor-child helper
CPU/RSS. Process sampling spans the capture process lifetime, including its
connection handshake; raw monotonic samples allow a tighter comparison to the
recorded output-event interval. A child that exits is reported with its last
observed CPU/RSS and is not silently counted as a surviving zero-cost helper.
Process sampling itself runs outside the compositor.

`scene_trace.py` can reparse exports independently:

```sh
python3 tests/bench/scene_trace.py --cpu cpu.csv --gpu gpu.csv \
  --messages messages.csv --backend headless --output summary.json
```

`scene_report.py RESULT_DIRECTORY` checks the complete matrix declared in its
metadata. The default single-display native run requires 96 measured cells plus
24 explicitly unavailable two-output phase cells. Missing/interrupted cells,
unexpected or duplicated cells/traces, malformed exports, stale timing summaries,
incomplete process samples, and failed scene admission make the report exit
nonzero while retaining a diagnostic `matrix.json` and `matrix.md`. A physical
two-output unavailable marker is accepted only for a declared single-display
DRM run. Headless runs must execute their two-output workload. A new retry needs
its own directory; reports do not silently merge partial runs.

CPU export must use `tracy-csvexport -u -f Output::render`; unrestricted zone
text can contain unescaped multiline text. The parser rejects missing GPU rows
when actual successful buffer commits exist, malformed exports, and GPU/CPU
clock misalignment. A valid idle trace with no render zones or messages remains
zero observed work. GPU time is the wall-time union of exported query intervals,
which avoids nested-zone double counting; it is not summed utilization across
GPUs and not a per-output latency measurement. CPU render duration and
commit-to-present observation latency remain separate distributions.

No output event currently records a requested presentation deadline, so missed
deadlines remain **unmeasured**, even for continuously updating clients. Client
frame requests, successful buffer commits, backend refresh values and actual
presentation flags remain available for independent pacing analysis. Neither
an idle interval, a headless timer gap, nor `--backend drm` proves missed frames
or direct scanout. The parser never certifies physical scanout from a caller's
backend label.

The automatic runner currently covers display-only encoded SDR. Actual color
and output format are preserved from IPC, rather than inferred from the
requested mode. Unmanaged ten-bit, managed HDR, unfiltered-capture cost,
physical two-display isolation and real PipeWire analysis require separately
prepared supported sessions. With one connected display and no microphone,
those physical gates remain explicitly unverified. The synthetic helper cost
must not be labelled microphone/playback acquisition or FFT-analysis cost.
