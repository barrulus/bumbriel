# Audio inputs for shaders (proposal)

The canonical shareable document is the
[design specification and implementation plan](effects-implementation-plan.md).
This supporting requirements note is aligned with its initial profile,
transport, dependencies and release gates; extensions are labelled as deferred.

Provide a shared audio-analysis input for existing border, window, screen,
cursor and animation shaders, including the proposed
[scene transitions](scene-transitions.md). This is a design proposal; no audio
source, configuration key, transport or GLSL symbol below is implemented.

The engine supplies consistent input data and schedules affected outputs.
Authors decide how sound changes their shaders. Audio is independent of effect
kind and of PR2 preset/pool selection: it is not another effect kind or a reason
to choose another pool member on each beat.

## Provider boundary

Use a small versioned analysis-provider contract:

```text
audio source -> external analysis provider -> bounded input snapshots
                                             -> effect input binding
                                             -> GLSL uniforms/helpers
```

The compositor needs the final input binding, coherent frame snapshots, and
damage/scheduling. Audio capture and analysis run in a helper process,
keeping device I/O, FFT work and audio callback threading out of the compositor
event loop. This fits the existing [scope](../../SCOPE.md): reuse a focused data
interface without introducing native plugin loading or a scripting runtime.
Providers are external clients of that interface, not dynamically loaded modules.

Initial adapter coverage must include both system playback and microphone input,
explicitly selected by the user, plus an external or synthetic analysis feed
for interchange and testing. Source bindings distinguish playback monitoring
from microphone acquisition and identify the selected device or an explicitly
chosen follow-default policy. A missing playback source must never silently
fall back to a microphone, or vice versa. Do not infer an application's audio
stream from the focused window or open a microphone because a shader was compiled.
One logical source is bound per effect instance initially; mixing sources can
happen in a provider without making every shader a mixer.

A PipeWire helper is the planned real-source adapter: its official
[audio capture example](https://docs.pipewire.org/1.2/audio-capture_8c-example.html)
shows audio streams and selecting sink monitoring. The GLSL contract should not
expose PipeWire node IDs, sample formats, or backend-specific controls. A
synthetic provider must exercise the same engine input path without PipeWire.

Use a private inherited Unix `SOCK_SEQPACKET` socketpair per demanded source and
direct executable/argv launch. Versioned feature snapshots carry host epoch,
source generation, sequence and observation time. Nonblocking bounded callbacks
coalesce to the latest valid state; no unbounded audio backlog, shell command,
public streaming endpoint or per-band JSON command IPC. The plan specifies
packet/profile limits and separates protocol readiness, device availability,
transport heartbeat and actual measurement freshness.

Reuse `core/process.*` and extract the shared pidfd/event-loop supervision
mechanics needed from `XwaylandSupervisor`; keep its display selection and
X-specific environment policy separate. A1 adds the inherited channel, readiness
handshake and bounded shutdown escalation. Preserve `SIGCHLD = SIG_IGN` and
observe exit through a pidfd rather than depending on waitable child status.
Last demand clears input and closes the channel immediately. The helper stops
acquisition on EOF; bounded EOF/TERM grace periods escalate to KILL if needed,
while the event loop continues without waiting for exit. No demand means no
retry timer. The canonical spec owns retry limits and A1's shutdown deadlines.

## Feature contract

Initial profile data, shared by every provider:

| Feature | Meaning |
| --- | --- |
| Availability | Distinguish a valid silent source from disconnected, stale or unavailable input |
| RMS and peak | Linear amplitude relative to defined digital full scale, with analysis window/channel aggregation specified |
| Envelope | Level with specified attack/release behavior, independent of rendering refresh rate |
| Frequency bands | Fixed bounded band count and documented frequency edges, magnitude/power convention and normalization |
| Source age and generation | Host/inspection metadata only; detect stale input and reset state after source change, without an always-changing shader age uniform |

Onset strength, beat/BPM tracking and waveform data are deferred. A future onset
feature would need a coalescing/decay contract so a transient does not disappear
between compositor frames; it is not part of the baseline transport or helpers.

The proposed `linear16-v1` profile fixes 16 bands, 48 kHz analysis, a 2048-sample
Hann window, an 800-sample hop, power-based channel aggregation, and 10/150 ms
envelope attack/release. The canonical spec defines its normalization, band edges
and golden-vector gate. Auto-normalization and source mixing are deferred; shader
authors may apply their own gain/mapping. Reject NaNs/infinities,
incompatible band layouts, oversized packets, stale generations and out-of-order
updates. A provider failure must not stall a render or leave an old peak held
forever: publish unavailable state, decay to zero over a bounded interval, and
stop requesting frames when stable.

## GLSL interface and frame consistency

Expose optional shared helpers/uniforms, with provisional names such as
`umbriel_audio_available`, `umbriel_audio_level`, `umbriel_audio_peak`, and
`umbriel_audio_band(t)`. The band helper clamps normalized `t` to [0,1] and
interpolates positions 0–15; a separate integer helper gives discrete access.
Pack available/RMS/peak/envelope into one vec4 and bands into vec4[4], consuming
two internal uniform entries with GLSL ES 1.00-compatible lookup. Larger spectrum
or waveform textures are deferred. Check uniform limits independently per stage.
Preserve existing UV, premultiplied-alpha, time and palette semantics.

Time, palette/count and packed audio use five of the legacy table's eight
entries. Scene geometry does not share those remaining entries: scene bundles
use their own typed descriptor/input path, allocated only for active scene
transactions. C1 freezes that layout and per-stage budgets without enlarging
ordinary effect allocations. Reuse the same audio values and binding helpers
across both paths, and test the combined time/palette/audio bindings.

For example, a proposed window fragment could modulate brightness while
preserving alpha (the new audio symbol is illustrative):

```glsl
vec4 window(vec2 uv) {
    vec4 color = umbriel_sample(uv);
    float gain = 0.75 + 0.25 * clamp(umbriel_audio_level, 0.0, 1.0);
    return vec4(color.rgb * gain, color.a);
}
```

At the output's effect cadence, advance its immutable source revision; intervening
client/animation frames reuse it. Latch that revision for each composition.
Display, unfiltered capture, participant draws, shadow/light passes, and all
stages of a transition use that same audio revision; their image feedback remains
separate where PR1 already requires it. A second output can latch a newer revision
at its next frame without making the first output's multipass result inconsistent.

Provider timestamps identify monotonic observation of the samples completing the
analysis window, not packet send time, an asserted ADC timestamp, `umbriel_time`
or animation progress. Protocol READY does not imply device readiness, and a
transport heartbeat does not renew measurement freshness. Frozen
animation-clock tests also pin the shader-visible audio snapshot; real incoming
audio cannot alter deterministic frozen frames. A test hook can inject a new
snapshot explicitly while frozen. Resume selects fresh input and resets or
rebases smoothing rather than replaying an accumulated backlog.

Audio may change transition amplitude, color, refraction or edge detail. It must
not restart transition identity, extend deadlines, select workspaces, alter
logical layout, or stop open/close from reaching exact endpoints. Geometry or
coverage modulation should be multiplied by an envelope that vanishes at those
endpoints. Closing copies of persistent effects freeze their copied audio values
just as they freeze existing uniforms; the active closing transition itself can
continue to receive live audio until its event ends.

## Integration with PR1 and PR2

`EffectRegistry` remains the program owner. Extend linked-program input
reflection and binding metadata to identify consumed audio features. Compilation
alone does not connect a provider. A selected preset declares a source binding;
subscribe only when a usable, unsuppressed consumer needs it. Dependencies are
cached when state changes, never discovered by scanning all presets on a frame.
Pool members can declare compatible input requirements while PR2 continues to
select them by its existing kind and policy.

Persistent frame eligibility currently requires visibility, advancing time and
a linked `umbriel_time` uniform. Audio needs a second explicit reason: a visible
audio consumer has changed input or an envelope still settling. Reuse the
per-output effect scheduling and `effects.max_fps` cap for audio-only frames.
A shader reading audio but not time must react; an unchanged silent input must
not force continuous redraw. A timer for decay exists only while needed.
Upload/sample updates consistently with the output's effect cadence, including
frames scheduled by clients. Clear a pending revision only after successful
submission, and damage the existing effect's drawn bounds.

Finite animations still use `Animatable`, not the persistent ledger. Their
geometry advances at the existing cadence while all stages reuse the output
audio latch, which advances at most at `effects.max_fps` (output cadence when zero).
The input service does not create another
animation owner or keep `settle` busy because music continues playing. A held
carousel can request audio-driven frames through its explicit mode scheduler.
Locking or an inactive session stops helpers/subscriptions and clears latched
state; unlocking resumes with fresh host epochs only for eligible consumers.

PR2 ownership/history/suppression remain unchanged. Mirrored windows and carousel
faces reuse their original selections and source bindings. They may need render
instances, but never new pool holdings or independent device captures. Toggle,
off/reset, runtime reassignment, unmap, disabled outputs and reload reconcile
input demand without drawing-time selection. Keep the underlying source binding
when suppression is reversible; stop its subscription when no active consumer
needs it. Inspection may report bound source, readiness and age, without starting
capture, picking a member or compiling a shader.

Use PR1 capture policy: audio does not override `in_capture`. If a persistent
effect is excluded from screencopy, its audio-responsive pixels are excluded too.
Transient animation capture follows the existing transient policy. Isolated
window capture retains its own scene/input binding and never gains neighbouring
window or desktop pixels. Renderer recovery rebuilds bindings without changing
PR2 assignments or reviving stale audio generations.

An active isolated toplevel capture with `in_capture=true` may be the only
consumer of a hidden view. Give that capture a coherent input latch and demand
without waking unrelated display outputs; one-shot capture never waits for an
audio device. Frozen persistent closing copies do not create source demand.

The no-consumer path must add no provider connection, device capture, spectrum
texture, analysis allocation or timer. Bound source count, features, update rate
and retained snapshots. A connected provider with no eligible visible consumer
must not cause output frames. An installed provider/helper being available is
distinct from actually acquiring audio.

## Acceptance examples and verification

| Example | Evidence |
| --- | --- |
| Border level pulse | Audio changes brightness with no `umbriel_time` reference; silence settles and effect-only frames stop |
| Window band tint | Low and high frequency fixtures produce different shading; alpha and existing backdrop sampling rules remain intact |
| Cursor response | Input changes affect only the visible pointer output; hidden cursor and suppressed selection request no audio frames |
| `window_scene` | Audio changes scene-wide shading and participant motion during opening and closing (for example, wave amplitude in a water preset); deadlines and final geometry stay fixed |
| Workspace carousel | Frequency bands change lighting across live faces using a coherent input revision; navigation remains user-controlled |
| Scene melt | Audio changes melt edge detail while the destination stays intact and source coverage reaches zero on time |
| Source selection and provider interchange | Playback and microphone are independently selectable, no cross-type fallback occurs, and synthetic/real providers publish the same feature profile without changing GLSL |

Begin with a synthetic provider and a persistent border/window preset before
depending on scene-transition work. Test validation, reconnect/generation,
out-of-order updates, silence, overload coalescing, source switching, frozen time,
failed frame submission, suppression, lock, two output refresh rates, capture
exclusion and renderer recovery. Add negative controls that hold audio at zero
or bypass binding to prove pixel assertions depend on the new input path.

G5 transport/cadence evidence from A1/A2 gates connection to real devices in A3.
Device-independent helper scaffolding can proceed earlier. Verify EOF handling,
unresponsive-helper termination and failed exit-watch setup with synthetic
helpers, without changing the compositor's child-reaping policy.

Measure input-to-presentation latency separately from shader cost: acquisition
window, provider cadence, IPC arrival and output frame timing all contribute.
Device-based checks of both playback and microphone inputs validate the required
adapters, including switching devices and loss of the selected device; synthetic
tests prove engine semantics. Selection inspection and no-effect/unused-pool performance
gates from PR2 remain required.

Use the canonical plan's workload matrix as well as its configuration matrix:
idle/silent input, small repeated window damage, video with border lighting,
and active transitions returning to rest. Report helper CPU separately from
compositor CPU/GPU. Frozen shader time does not imply unchanged audio or cached
light emission; each depends on the complete set of inputs it consumes.

The initial reviewed profile is RMS, peak, envelope and 16 bands, with helper-
owned healthy-source analysis and engine-owned bounded failure decay. Onset/BPM,
waveform textures, automatic gain and shader mixing are deferred. The plan gives
proposed numeric values, source-binding TOML, protocol and golden-vector gates;
adapter dependency choice and measured latency/cost are implementation evidence,
not reasons to hard-code an audio visual style.
