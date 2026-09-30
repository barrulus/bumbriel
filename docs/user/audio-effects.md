# Audio inputs for effects

An effect can bind one named audio source. Sources are shared across consumers;
declaring a source or compiling a preset does not start recording. Acquisition
starts when an eligible effect actually uses its audio uniforms and stops when
its last consumer disappears. Missing providers leave audio unavailable while
ordinary rendering continues.

The optional `umbriel-audio` helper uses PipeWire. Build it with Meson's
`-Daudio_helper=enabled`, or the Nix package's `enableAudio = true` override.
The helper requires PipeWire 1.0.5 or later. Playback and microphone are distinct
source modes; they never substitute for one another.

```toml
[effects.audio.sources.desktop]
provider = "pipewire"
mode = "playback"
follow_default = true

[effects.audio.sources.voice]
provider = "pipewire"
mode = "microphone"
target = "the-explicit-node-name"

[effects.preset.music_border]
kind = "border"
shader = "music-border.glsl"
audio = "desktop"
animated = false

[effects]
border = "music_border"
```

Replace the microphone target with the chosen PipeWire node's `node.name`.
Each source must select exactly one fixed `target` or `follow_default = true`.
A fixed target remains unavailable if that device disappears. Following a default
is an explicit choice within the selected source type.

An external provider uses `provider = "external"`, an `executable` path and an
optional `args` array. Relative executable paths resolve beside their declaring
configuration file. Arguments are passed directly, including empty strings;
there is no shell expansion. The provider receives a private Unix seqpacket
channel on standard input and must implement the versioned audio protocol in
`src/audio/protocol.h`. It must negotiate the requested source type and stop
acquisition when the channel closes.

## Shader inputs

The helpers work in every existing effect kind and require no `umbriel_time`
reference. A border's `animated = false` or `speed = 0` freezes its time input;
it does not disable its separately selected audio source.

| GLSL helper | Value |
| --- | --- |
| `umbriel_audio_available()` | 1 for a current measurement, otherwise 0 |
| `umbriel_audio_rms()` | RMS amplitude across channels |
| `umbriel_audio_peak()` | Largest absolute sample |
| `umbriel_audio_level()` or `umbriel_audio_envelope()` | RMS with 10 ms attack and 150 ms release |
| `umbriel_audio_band_at(int index)` | Discrete band, index clamped to 0–15 |
| `umbriel_audio_band(float position)` | Interpolation across bands, position clamped to 0–1 |

All amplitudes are linear values in [0,1], quantized in increments of 1/65535.
There is no automatic gain, beat detector, BPM estimate or waveform input.
Analysis uses 48 kHz audio, a 2048-sample periodic Hann window and an 800-sample
hop. Frequency edges are 20, 40, 80, 120, 180, 270, 400, 600, 900, 1350, 2000,
3000, 4500, 6750, 10000, 15000 and 20000 Hz. Low bands share the FFT's coarse
frequency resolution. Channel power averaging preserves anti-phase stereo.
The analysis window spans about 42.7 ms; this is not an end-to-end latency claim.

For example, this border responds without a running shader clock:

```glsl
vec4 border(vec2 uv) {
    float strength = umbriel_audio_level();
    return vec4(0.1 + 0.9 * strength, 0.2, 1.0 - strength * 0.5, 1.0);
}
```

Audio-only frames follow `effects.max_fps`. Unchanged silence does not request
additional effect frames. Provider loss marks input unavailable and fades held
amplitudes to exact zero over 150 ms. Heartbeats do not refresh measurements;
measurements become stale after 250 ms without a new valid snapshot. Lock and
inactive session clear inputs and stop providers. `umbriel effects --json`
reports source readiness, availability, epoch, generation and sequence without
starting acquisition.

## Verification status

The wire/profile, synthetic helper and GPU packed-input fixtures are automated.
Real playback/microphone routing and removal behavior, capture-only demand,
frame cadence and device latency must pass the gates tracked in
[implementation evidence](../design/effects-progress.md) before release. Source
properties alone do not prove how a session manager behaves on device removal.
