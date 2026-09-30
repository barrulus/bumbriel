# Optional PipeWire helper prototype

`src/audio/pipewire.cpp` implements the inherited stdin `SOCK_SEQPACKET`
protocol and the shared `linear16-v1` analyzer. Meson `audio_helper` and Nix
`enableAudio` are disabled by default. The optional `umbriel-audio` executable
links PipeWire >= 1.0.5 separately from the compositor. PipeWire and the SPA
headers identify their license as MIT, compatible with this project; no FFT
library or additional audio backend is introduced.

A fixed selection resolves only the requested node name or object serial of the
requested `Audio/Sink` (playback monitor) or `Audio/Source` (microphone) class.
Follow-default watches only `default.audio.sink` or `default.audio.source`
metadata for its explicit mode. Each resulting stream is pinned to the resolved
serial, with no reconnect/fallback/remix properties. Node info and raw format
parameters supply channel count and positions; unsupported or unresolved formats
remain unavailable. Native channel changes discard analysis. Loss reports
UNAVAILABLE in the last published generation, allowing the compositor's held
input to fade; only the first complete measurement from a replacement stream
advances the analysis generation. Heartbeat and loss status use the last
successfully sent wire generation, so a newer snapshot discarded under
backpressure cannot prematurely reset the host's fade. Before processing samples
the helper also verifies every incoming link comes from that selected node and that all channels are linked.
Incoming link changes immediately discard queued measurements, including the
pending transport write. Buffers from cycles before the invalidation barrier
cannot establish a replacement generation. Corrupted chunks and metadata are
rejected before analysis.

PipeWire adapts input to interleaved F32 at 48 kHz, preserving 1–8 channels.
The profile validates finite data and bounds the ring. Capture-cycle timestamps
reject zero, future, out-of-order or more than 500 ms old buffers. The wire's
observation timestamp remains the time the helper ingests the validated samples,
not an ADC timestamp. Feature publication coalesces to at most 60 Hz and
unavailable heartbeats continue at 100 ms. Stream failures retry after 500 ms;
server absence or loss retries after 1 s. EOF closes the stream and helper.

Evidence obtained without acquiring audio:

- Strict standalone C++23 compilation against installed PipeWire 1.6.8.
- The pure acquisition regression verifies same-generation loss, pending-window
  discard, rejection of queued old buffers, and generation/sequence reset only
  after a complete replacement measurement.
- `audio-pipewire` checks all playback/microphone × fixed/default combinations
  against an explicitly nonexistent server: READY, repeated UNAVAILABLE, no
  snapshots, and clean EOF exit.
- `tests/audio_pipewire_reconnect.py` runs a private PipeWire server with no
  nodes: initial absence, connection, server loss, reconnection after restart,
  and clean EOF exit. The helper was observed in the private registry on both
  connections. No snapshots were emitted.
- Read-only `pw-dump` of the developer session confirmed its real playback and
  microphone nodes expose two channels and FL/FR through node info/EnumFormat.
  This did not create a stream or acquire samples.

These are scaffolding checks, not A3/G5 acceptance. Actual playback and microphone
samples, fixed/default behavior under the real session manager, removal without
cross-type rerouting, format/channel transitions, latency, CPU, and rendered
consumer scheduling still require the controlled live session. The incoming-link
check and backend properties are precautions; they do not replace those tests.

Primary API reference: [PipeWire property documentation](https://docs.pipewire.org/page_man_pipewire-props_7.html).
