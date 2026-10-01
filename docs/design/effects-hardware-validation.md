# Effects hardware validation procedure

This is the remaining controlled-session evidence procedure, not a record of
hardware acceptance. Run it only after switching to the disposable Umbriel
session. Current-session playback or microphone acquisition is not part of the
headless test runs. Keep audio fixtures and logs under `/tmp`; the feature probe
records numeric features and transport metadata, never PCM.

## Production artifact

The candidate is built with GCC 15, `-O3`, LTO, `AR=gcc-ar`, and
`RANLIB=gcc-ranlib`. `tests=disabled`, `test_ipc=disabled`, and
`audio_helper=enabled` exercise the installed production path. The local install
prefix is `/tmp/umbriel-effects-production-install`:

```sh
export PATH="/tmp/umbriel-effects-production-install/bin:$PATH"
umbriel --version
umbriel config validate -c /tmp/effects-device-session.toml
```

Start that configuration through the disposable session's normal session/TTY
entry. Preserve the configuration, binary hashes, GPU/driver, display modes,
PipeWire/WirePlumber versions, CPU governor, and build options alongside results.
Do not compare a debug candidate with an optimized baseline.

The current available hardware has one physical display and no microphone.
Do not execute the microphone commands below in this run; they describe a
future hardware gate only. Real microphone and two-physical-output acceptance
remain explicitly unverified. Continue with playback and the available display.

### Recorded playback checks, 2026-09-29

The disposable native session's actual default playback sink was serial `65`,
`alsa_output.pci-0000_80_1f.3.analog-stereo`, with FL/FR channels. Fixed-serial
and follow-default probes each acquired continuously for ten seconds with
exactly zero silent RMS (465 and 464 snapshots). Live PipeWire graph inspection
confirmed two active monitor links per helper: sink FL to helper FL and sink FR
to helper FR, F32P, with no channel remix. Both helpers exited cleanly on EOF,
and the graph afterward contained no helper stream.

A subsequent five-pulse 1 kHz stimulus passed for both selectors: all ten
rise/fall edges crossed the RMS threshold and returned to settled zero. The
analyzer rejects crossings spanning source loss, a generation replacement, or
a measurement gap over 100 ms; offline negative controls also reject unrelated
already-high audio. The producer was checked offline for stereo symmetry,
sample count, amplitude and quiet boundaries before physical playback.

Evidence is under `/tmp/umbriel-effects-native-audio/`: `functional-graph.json`,
`fixed.jsonl`, `default.jsonl`, `tone-graph.json`, `tone-edges.jsonl`,
`tone-{fixed,default}.jsonl`, and the corresponding summaries. These short runs
occurred alongside other validation work and establish functional playback
acquisition only. Their CPU and exploratory software latency values are not the
required steady-load or 100-transition latency acceptance measurements. No
microphone was acquired and no default device was changed.

Separately, a temporary low-priority null playback sink tested source lifecycle
through the real session manager, without changing defaults. Fixed serial
selection remained unavailable after removing that serial, even when another
node reused its name. Fixed name selection recovered across stereo, removal,
mono, removal, and stereo with generations 2, 3 and 4. No snapshots arrived
during either loss interval; all helpers and temporary nodes were removed.
`virtual-events.json`, `virtual-fixed-{serial,name}.jsonl`, and the stereo/mono
graph snapshots retain this evidence. This validates virtual routing lifecycle,
not removal or format negotiation of the physical device.

Playback selection also rejected a nine-channel null sink and an `Audio/Source`
identifier: both remained unavailable with zero snapshots. These checks did not
create a microphone capture stream. `unsupported-nine-channel.jsonl` and
`wrong-class-playback.jsonl` retain the protocol evidence; defaults and temporary
node cleanup were checked afterward.

The quiet-window playback latency run subsequently completed 100 pulse cycles
(200 rise/fall edges) for both fixed and default selection, with every edge
matched and no loss/generation/staleness rejection. At RMS threshold 0.05:

| Selection / edge | p50 | p95 | p99 | maximum |
| --- | ---: | ---: | ---: | ---: |
| Fixed / rise | 52.76 ms | 63.91 ms | 64.04 ms | 64.06 ms |
| Fixed / fall | 75.23 ms | 95.73 ms | 95.85 ms | 95.96 ms |
| Default / rise | 53.16 ms | 53.92 ms | 64.01 ms | 64.14 ms |
| Default / fall | 85.19 ms | 96.31 ms | 96.51 ms | 96.52 ms |

These measure first accepted producer PCM write to received feature crossing,
including producer buffering, PipeWire, analysis and transport. They do not
measure physical, acoustic or visible latency. Evidence and environment are in
`/tmp/umbriel-effects-native-audio/quiet/latency-*`.

The user then stopped audible testing. The subsequent steady-tone CPU case was
interrupted and cannot establish steady-tone cost, even though its first helper
probe completed. Its runner and playback producer were terminated; no further
audible case may run under this procedure without a new user instruction.
`quiet/audible-cancelled.json` records the cancellation, and the prepared runner
rejects restarting audible cases.

Silent CPU checks then completed with builds/profiling stopped, on an Intel
Core Ultra 9 275HX with the `powersave` governor. Each cell below contains three
30-second repetitions. CPU is percent of one core and includes helper startup
and shutdown. RSS is the observed sampled maximum, not allocation high-water.

| Workload | Helper CPU, repetitions 1 / 2 / 3 | Sampled RSS |
| --- | --- | --- |
| Physical stereo playback monitor, exactly silent | 2.79% / 2.81% / 2.97% | 10.09–10.18 MiB |
| Fixed unavailable playback source | 0.158% / 0.157% / 0.140% | 7.76 MiB |
| Four distinct silent virtual stereo sinks, four helpers | 11.14% / 11.57% / 11.28% total | 39.82–40.32 MiB, sum of individual sampled maxima |

All silent measurements were exactly zero; unavailable-source runs emitted no
snapshots. The four-source graph had four independent complete FL/FR monitor
connections. All helpers and fixture nodes were removed, and default metadata
was unchanged. Raw JSONL, CPU summaries and graph snapshots are in `quiet/` and
`four/`; `cost-summary.json` records the machine, governor and helper hash.
The four-source case measures actual helpers against virtual playback sinks; it
does not establish four physical devices or compositor demand-sharing cost.
Steady-tone CPU, physical microphone measurements and physical visible latency
remain unverified.

The later silent live-routing matrix also passed. Following playback moved from
the physical sink to a temporary virtual sink and back, using generations
2→3→4; fixed serial selection stayed on the physical sink throughout. Changing
only the source-default metadata to an absent marker neither rerouted nor
restarted either playback helper. No microphone stream was created. Removing
only the fixed helper's FR link made it unavailable with no snapshots while its
FL link remained connected; restoring FR resumed complete stereo acquisition in
a new generation. `routing/` contains each graph, timestamped operation, feature
stream and `validated-summary.json`. All original default metadata was restored
exactly and temporary helpers/nodes were removed. These results cover real
session-manager routing with virtual replacement targets, not physical-device
hotplug or hardware sample-format/rate negotiation.

During the later showcase session, existing Spotify playback exposed an authoring
problem in the shipped spectrum example: a linear, weak halo and a palette close
to the native border made real low-amplitude audio appear static. A temporary
RMS-to-colour diagnostic proved live receiver-to-uniform rendering worked; its
shader was automatically restored afterward. The example now compresses its
visual response to RMS and bands, changes halo reach, and varies palette sampling
with level. The linear audio profile itself is unchanged. A border-only 16×64
pixel strip changed red mean from 100 to 128 over twelve live samples, compared
with an effectively constant 78/94/130 mean before this revision. Evidence is in
`/tmp/umbriel-spectrum-pixels/summary.json` and
`/tmp/umbriel-spectrum-diagnostic-pixels/summary.json`. No test sound or microphone
acquisition was used. The new `effect/audio_spectrum` regression checks quiet
RMS with an empty frequency band, low and stronger band levels, frozen held
pixels, and return to exact zero; the original shader fails its visibility
control. The user then confirmed, "Yes, it responds now." Cleanup verified all
102 original configuration hashes unchanged, identical canonical/showcase
shaders, and only the legitimately demanded production audio helper remaining
(`/tmp/umbriel-spectrum-cleanup-audit.json`). This is functional visual response
evidence, not display latency.

### Recorded native cost, 2026-09-30

The corrected physical run produced 96 advancing-workload cells and 24
frozen-video cells. Independent review accepts 119 original cells plus one
matched supplemental sample, covering **120/120 planned single-display cells**.
One original baseline cell,
`run-1/baseline/video-light/declared-unused`, lost its active seat for about
7.4 seconds: the compositor logged an atomic-commit permission failure and DRM
pause/resume, and its final snapshot contained no output. Its raw evidence is
retained, but its reported timing/CPU/RSS values are excluded. The targeted
video recheck passed 8/8 cells under `/tmp/umbriel-cost-native-video-recheck`.
Its baseline declared-unused sample supplies the missing third repetition;
the other seven samples are confirmation and do not change repetition weights.
All six artifact hashes, eight video configurations and shader files match the
original run, and the recheck has no seat interruption or renderer error.
Original matrix validity remains 95/96 plus 24/24; no invalid data was replaced
in place. `/tmp/umbriel-effects-session/native-cost-review.json` records the
explicit selection and hashes. The supplemented baseline declared-unused video
median is 22.18% of one CPU core, 186.27 MiB sampled RSS and 0.289 ms per-run
render p95; the candidate medians are 23.22%, 187.07 MiB and 0.299 ms.
The [native cost review](effects-native-cost-review.md) preserves the complete
workload/phase comparison, supplemental selection and presentation cadence.

Reports are `/tmp/umbriel-cost-native-corrected/matrix.{json,md}` and
`/tmp/umbriel-cost-native-corrected-frozen/matrix.{json,md}`. The report now checks
that the configured outputs remain present and enabled, with matching mode,
scale, position, transform, adaptive sync and color at both boundaries. All
31 benchmark regression tests pass. Boundary snapshots cannot rule out a
mid-sample interruption; the independent compositor-log audit found no other
seat pause or renderer error in these matrices.

Valid samples used Intel rendering to physical eDP-1 at 1920×1080, 143.998 Hz,
scale 1, encoded SDR XR24, display-only composition. Baseline and candidate are
optimized LTO builds with identical Tracy instrumentation. Full hashes remain
in each matrix's metadata; baseline begins `40213999305e`, candidate
`03741fd7e1b7`. Each sample lasts 30 seconds; CPU includes the capture handshake
and profiling overhead, expressed as percent of one core. The active-workload
values below are medians of three repetitions; render p95 is the median of the
three per-run p95 values, not a pooled percentile.

| Active workload | Baseline compositor CPU | Candidate compositor CPU | Candidate synthetic helper CPU | Baseline / candidate CPU render p95 |
| --- | ---: | ---: | ---: | ---: |
| Unchanged idle desktop | 2.98% | 3.50% | 1.82% | No render calls |
| Small surface updates with window/overlay effect | 15.54% | 17.77% | 1.83% | 0.424 / 0.528 ms |
| Video with border lighting, time advancing | 23.22% | 24.85% | 0.95% | 0.411 / 0.557 ms |
| Video with border lighting, time frozen | 23.02% | 24.97% | 1.02% | 0.423 / 0.536 ms |
| Scene cycle versus corresponding native actions | 16.97% | 29.94% | 0.85% | 0.649 / 0.416 ms |

The scene row compares different visual implementations and includes rest,
landing, capture and lifecycle work. It is not a same-effect speed comparison.
Candidate active scene sampled process RSS is a median 201.46 MiB, versus
193.16 MiB for the native baseline; sampled RSS is not allocation high-water.
Maximum observed primary-output reservations were 58,060,992 bytes for melt,
116,125,888 for carousel and 146,270,316 for window presentation. These scopes
are not summed. All three scene repetitions admitted every required stage
without fallback. The closing effect still active immediately after the last
transient window was terminated is expected; returned-to-off checkpoints
released all presentation reservations.

All 45 candidate disabled/declared-unused/returned-to-off cells have zero
observed helper demand, helpers and live/reserved scene resources. All 24 idle
cells, including active unchanged silence, have zero CPU render calls and GPU
query zones. Active scene samples show up to twelve sequential helper lifetimes
as workspace visibility changes, but sampled simultaneous helper count never
exceeds one. This synthetic helper does not acquire playback or microphone;
physical PipeWire cost is recorded separately above.

GPU query elapsed union must **not** be read as hardware busy time. The submit
zone dominates some traces at approximately one refresh interval per submit:
one representative candidate cell has 29,970 ms in `render_pass_submit` over
roughly 30 seconds. Submission/query flushing plausibly includes the wait to
the next frame; the precise cause is not established. Individual query zones
remain in the raw CSV, but no total GPU execution or utilization claim is made
from that union. Requested presentation deadlines were not recorded, so missed
frame deadlines remain unmeasured. Physical HDR, two-output scanout isolation,
unfiltered-capture cost and separate profiler-overhead controls are not proven
by this scale-1 SDR matrix. The interrupted comparison now has its third valid
repetition through the explicitly identified supplement.

## Typed source matrix

In the disposable session, save `pw-dump` before starting any provider and list
only the selectable source classes:

```sh
pw-dump > /tmp/effects-audio-before.json
jq '.[] | select(.type == "PipeWire:Interface:Node") | .info.props |
    select(."media.class" == "Audio/Sink" or ."media.class" == "Audio/Source") |
    {serial:."object.serial", name:."node.name", class:."media.class",
     channels:."audio.channels", position:."audio.position"}' /tmp/effects-audio-before.json
```

Choose one explicit sink and one explicit microphone source from that inventory.
`Audio/Sink` is playback monitor acquisition; `Audio/Source` is microphone
acquisition. Monitor names or a source of the other class are not substitutes.
For each mode test both fixed selection and following that mode's default:

```sh
python3 tests/audio_device_probe.py --helper "$(command -v umbriel-audio)" \
  --mode playback --target "$SINK_SERIAL" --duration 30 \
  --output /tmp/effects-playback-fixed.jsonl --permit-device-acquisition --require-snapshots
python3 tests/audio_device_probe.py --helper "$(command -v umbriel-audio)" \
  --mode playback --follow-default --duration 30 \
  --output /tmp/effects-playback-default.jsonl --permit-device-acquisition --require-snapshots
python3 tests/audio_device_probe.py --helper "$(command -v umbriel-audio)" \
  --mode microphone --target "$MIC_SERIAL" --duration 30 \
  --output /tmp/effects-microphone-fixed.jsonl --permit-device-acquisition --require-snapshots
python3 tests/audio_device_probe.py --helper "$(command -v umbriel-audio)" \
  --mode microphone --follow-default --duration 30 \
  --output /tmp/effects-microphone-default.jsonl --permit-device-acquisition --require-snapshots
```

The probe records its helper PID immediately, verifies the negotiated type,
wire profile, epoch, monotonic generations/sequences/timestamps, normalized
finite features, clean EOF shutdown, packet counts, transport age, and helper
CPU time. `READY` alone does not mean acquisition succeeded. Require sustained
snapshots under a controlled tone and an available, exactly zero settled silent
input. Save `pw-dump` during every acquisition and trace every incoming helper
link back to the chosen node, including channel count and positions. Verify the
real session manager creates the expected complete per-channel link topology;
protocol type confirmation alone cannot establish correct physical routing.

For each mode, repeat these operations while the probe runs:

1. Change only that mode's default to a second same-class node. The following
   provider must change; fixed selection must remain on its original serial.
2. Change the other mode's default. No provider under test may reroute.
3. Remove the selected node. Fixed selection becomes unavailable and emits no
   replacement-source measurements; the compositor fades held input to zero.
4. Restore/recreate the node and repeat with default following. A complete new
   analysis window starts a new generation, with no previous-generation bands.
5. Change supported sample format/rate and native channel layout; verify no
   channel remix, truncation, or old queued measurement survives the change.
6. Exercise unsupported channels/formats and partial/disconnected links. They
   must remain unavailable, without falling back to another type or source.
7. Stop the helper/probe and the last compositor consumer. Verify no helper,
   input stream, analysis work, or retry timer remains. A declared unused source
   must never create one in the first place.

Record the monotonic time of every operation in the result directory. Preserve
both PipeWire graph snapshots and feature JSONL so generation/loss behavior can
be correlated with actual source changes. Restore the disposable session's
defaults after each case.

## Latency and CPU

The probe's `transport_age_ms_*` is helper-ingest-to-host-receive time. It is
**not** acquisition, FFT-window, display, or acoustic latency. The profile has a
2048-sample window at 48 kHz (about 42.7 ms support) and an 800-sample hop.

For a repeatable software measurement, drive a known quiet/tone/quiet pattern to
the selected playback sink, timestamp each producer write with `CLOCK_MONOTONIC`,
and correlate it with the first matching rise/fall in received features. Include
warm-up, at least 100 transitions, p50/p95/p99, worst case, and the chosen feature
threshold. State whether the timestamp is before producer write or graph
presentation: process-launch timestamps include launch/buffering overhead and
must be labelled accordingly. For microphone acquisition use the same controlled
speaker stimulus and an independently recorded reference; acoustic propagation,
DAC/ADC buffering, and loopback calibration belong in the reported result.

Repeat through an authored audio shader on a physical display. Correlate the
stimulus with the successful presentation timestamp and a photodiode/high-speed
camera when claiming visible or acoustic-to-visible latency. Polling `effects`
and taking screenshots does not measure physical display latency. Run at the
requested effect cap and display refresh; report provider, host scheduling, and
physical end-to-end measurements separately.

Each probe summary includes helper CPU seconds and percent of one core, including
startup/teardown, and RSS sampled from the helper process every 100 ms. The
sampled RSS maximum can miss shorter peaks and is not an allocation high-water
measurement. Use 30-second steady workloads and three repetitions for
silence, tone, source loss, and all four distinct demanded sources. Measure the
compositor separately; shared consumers must leave only one helper per source.
Also record the no-source baseline and declared-but-unused sources: they must
have no helper cost. Record CPU model, frequency/governor, process affinity, and
other system load with the results.

## Real CPU/GPU profiling

Use the same real Tracy 0.13.1 static client for baseline and candidate, with
`TRACY_ENABLE`, `TRACY_ON_DEMAND`, localhost-only, and no broadcast. Its temporary
prefix is `/tmp/umbriel-effects-tracy-client`. Append its pkg-config directory to
the Nix development shell's existing `PKG_CONFIG_PATH`; replacing that variable
loses the Wayland dependency paths. Both builds retain `-O3` and LTO. Instrumented
builds have harness IPC enabled only for repeatable contained workload setup;
the installed production artifact above does not.

Both binaries use completed GPU timestamp-query calibration at context creation.
The NVIDIA GLES driver was observed to leave `glGetInteger64vEXT`'s timestamp
destination unchanged without an error; query-based calibration fixes the
resulting invalid origin. A contained four-second capture verified overlapping
CPU/GPU timelines and nonempty exports. Use the signal-preserving
`/tmp/umbriel-effects-session/launch-profile` launcher for either binary: Tracy
starts worker threads before the compositor installs its signalfd masks.

Use the frozen patched Tracy 0.13.1 capture/export tools for both baseline and
candidate. The capture waits for worker shutdown and counts actual serialized
callstack rows; the exporter waits for GPU reconstruction and rejects EOF
over-read. The patch is
`/tmp/umbriel-effects-profile-final/tracy-tools.patch`, and tool SHA256 hashes are
in `/tmp/umbriel-effects-profile-final/manifest.json`. Stock capture/export
output is not the accepted evidence pipeline:

```sh
/tmp/umbriel-effects-profile-final/tools/tracy-capture -a 127.0.0.1 -s 10 -f -o /tmp/effects-candidate.tracy
/tmp/umbriel-effects-profile-final/tools/tracy-csvexport -u -f Output::render /tmp/effects-candidate.tracy > /tmp/effects-candidate-cpu.csv
/tmp/umbriel-effects-profile-final/tools/tracy-csvexport -g /tmp/effects-candidate.tracy > /tmp/effects-candidate-gpu.csv
```

The patched `tracy-csvexport -m` exports the `umbriel.output` frame/commit/present messages,
including raw presentation time, sequence, refresh prediction, flags, and output.
Requested deadline zero means unknown. Count cadence misses only during a
specified continuous workload using actual successful presentations and valid
refresh information; idle gaps and long CPU zones alone do not establish misses.
Unrestricted CPU CSV contains unescaped multiline zone text, so raw line counts
are not event counts. Filter named CPU zones or use the aggregate exporter.

Only one profiled compositor should listen on the default Tracy port. Preserve
the raw trace and require nonempty GPU events; a linked no-op client is not
profiling evidence. Compare identical renderer/driver/output/color mode, workload,
window geometry, source inventory, effect cap, and capture participation. Report
CPU zone duration, GPU event duration, frame interval distributions, allocation
high-water/reserved bytes, capture/pass counts, and failed/retried compositions.
Keep nested GPU zones separate rather than summing overlapping events. Repeat
three times and also measure disconnected-client runs to quantify profiler cost.
Headless traces establish rendering cost but do not establish physical scanout,
HDR correctness, or refresh-locked presentation pacing.
Pipeline smoke evidence only validates the capture/export plumbing. GPU query
interval union measures observed elapsed GPU-zone time, not summed utilization;
requested presentation deadlines and direct scanout remain unmeasured.
