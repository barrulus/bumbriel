#!/usr/bin/env bash
# An authored held scene reads TIME through its transaction owner and existing
# output effect clock. It stays idle when frozen and releases cadence on exit.
set -euo pipefail
cat > "$UMBRIEL_RUNTIME_DIR/source-time.vert" <<'VERT'
vec4 transition_vertex(vec2 uv) { return vec4(uv * 2.0 - 1.0, 0.0, 1.0); }
VERT
cat > "$UMBRIEL_RUNTIME_DIR/source-time.glsl" <<'GLSL'
vec4 transition_fragment(vec2 uv, vec2 output_uv) {
  return vec4(0.0, 0.5 + 0.4 * sin(umbriel_time * 6.2831853), 0.0, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 1
[animation]
enabled = true
duration_ms = 100
curve = "linear"
[workspace_presentation]
effect = "scene-time"
[effects]
in_capture = true
max_fps = 8
[effects.preset.scene-time]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "source-time.vert"
shader = "source-time.glsl"
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
for _ in $(seq 100); do
  (( $(grep -c 'config reloaded' "$UMBRIEL_RUNTIME_DIR/compositor.log") >= 2 )) && break
  sleep .02
done
(( $(grep -c 'config reloaded' "$UMBRIEL_RUNTIME_DIR/compositor.log") >= 2 ))
"$UMBRIEL" settle > /dev/null
python3 - <<'PY'
import json
import math
import os
import subprocess
import time

exe = os.environ['UMBRIEL']
def run(*args):
    return subprocess.check_output([exe, *args], text=True, timeout=10)
def data(command):
    return json.loads(run(command, '--json'))
def probe():
    return next(o['workspace_presentation'] for o in data('effects')['owners'] if o['type'] == 'output' and o['name'] == 'HEADLESS-1')
def frame():
    return data('effect-frames')['outputs'][0]
def wait(get, predicate, reason):
    end = time.monotonic() + 4
    while True:
        value = get()
        if predicate(value):
            return value
        assert time.monotonic() < end, (reason, value)
        time.sleep(.01)
def pixel():
    image = os.path.join(os.environ['UMBRIEL_RUNTIME_DIR'], 'source-time.png')
    subprocess.run(['grim', '-s', '1', image], check=True, timeout=10)
    return subprocess.check_output([os.environ['UMBRIEL_PIXEL_PROBE'], image, 'pixel', '150', '150'], text=True).strip()

assert frame()['eligible'] == 0
assert data('effects')['audio_demanded_sources'] == 0
run('clock-freeze')
run('msg', 'workspace-presentation-enter')
run('clock-advance', '1')
run('clock-advance', '3000')
wait(probe, lambda p: p['active'] and p['phase'] == 'held', 'authored scene never held')
assert frame()['eligible'] == 0
run('clock-resume')
wait(frame, lambda f: f['eligible'] == 1, 'authored TIME never became eligible')
samples = []
for _ in range(4):
    samples.append([int(v) for v in pixel().split()])
    time.sleep(.18)
assert all(rgb[0] <= 2 and rgb[2] <= 2 for rgb in samples), samples
assert max(rgb[1] for rgb in samples) - min(rgb[1] for rgb in samples) >= 20, ('source TIME pixels did not advance', samples)
start, before = time.monotonic(), frame()
# Real time: verify time-only source cadence through actual output commits.
time.sleep(1.4)
after = frame()
elapsed = time.monotonic() - start
for key in ('effect_frames', 'buffer_commits'):
    delta = after[key] - before[key]
    assert 2 <= delta <= math.ceil(elapsed * 8) + 2, ('source time cadence', key, delta, elapsed)
assert data('effects')['audio_demanded_sources'] == 0
run('clock-freeze')
run('settle')
wait(frame, lambda f: f['eligible'] == 0, 'frozen source remains eligible')
first = pixel()
baseline = frame()
# Real time: no event or capture may advance a frozen source clock.
time.sleep(.3)
assert frame()['effect_frames'] == baseline['effect_frames']
assert frame()['buffer_commits'] == baseline['buffer_commits'], ('frozen scene commits', baseline, frame())
# A screenshot explicitly requests a buffer; it must preserve the held pixels
# and must not recapture the source or advance the effect clock.
assert pixel() == first, 'frozen hidden time pixels changed'
assert frame()['effect_frames'] == baseline['effect_frames']
run('clock-resume')
wait(frame, lambda f: f['eligible'] == 1 and f['effect_frames'] > baseline['effect_frames'], 'time source did not resume')
run('clock-freeze')
run('msg', 'workspace-presentation-cancel')
run('clock-advance', '1')
run('clock-advance', '3000')
run('settle')
wait(frame, lambda f: f['eligible'] == 0, 'released hidden source still eligible')
baseline = frame()
time.sleep(.3)
assert frame()['buffer_commits'] == baseline['buffer_commits'], 'released time source kept output awake'
print('authored TIME scene obeyed 8 fps, frozen pixels/commits stayed idle, release restored zero eligibility')
PY
