#!/usr/bin/env bash
# A hidden time-only source uses its original effect owner and the presentation
# output clock, with no audio provider and no extra native selection.
set -euo pipefail
cat > "$UMBRIEL_RUNTIME_DIR/source-time.glsl" <<'GLSL'
vec4 window(vec2 uv) {
  return vec4(0.0, 0.5 + 0.4 * sin(umbriel_time * 6.2831853), 0.0, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
workspaces = 3
[animation]
enabled = false
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[effects]
in_capture = true
max_fps = 8
[effects.preset.hidden-time]
kind = "window"
shader = "source-time.glsl"
[[window_rule]]
match.title = "^hidden-time$"
default_workspace = 2
default_focused = false
default_floating = true
default_position = { x = 100, y = 100, anchor = "top_left" }
window_effect = "hidden-time"
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
for _ in $(seq 100); do
  (( $(grep -c 'config reloaded' "$UMBRIEL_RUNTIME_DIR/compositor.log") >= 2 )) && break
  sleep .02
done
(( $(grep -c 'config reloaded' "$UMBRIEL_RUNTIME_DIR/compositor.log") >= 2 ))
FILL_COLOR=0xFF000000 "$UMBRIEL_UNMAP_CLIENT" hidden-time 300 200 > "$UMBRIEL_RUNTIME_DIR/time-client.log" 2>&1 &
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .025
done
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
def probe(action='status'):
    return json.loads(run('presentation-workspace-probe', action, '--json'))
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

hidden = data('windows')[0]
assert frame()['eligible'] == 0, ('hidden native time owner kept output awake', frame())
assert data('effects')['audio_demanded_sources'] == 0
probe('open ' + hidden['workspace'])
wait(frame, lambda f: f['eligible'] == 1, 'source time owner never became eligible')
wait(probe, lambda p: p['active'] and p['committed_revision'] > 0, 'source never committed')
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
baseline, captured = frame(), probe()
# Real time: no event or capture may advance a frozen source clock.
time.sleep(.3)
assert frame()['effect_frames'] == baseline['effect_frames']
assert frame()['buffer_commits'] == baseline['buffer_commits'], ('frozen source commits', baseline, frame(), captured, probe())
assert probe()['captures'] == captured['captures']
# A screenshot explicitly requests a buffer; it must preserve the held pixels
# and must not recapture the source or advance the effect clock.
assert pixel() == first, 'frozen hidden time pixels changed'
assert frame()['effect_frames'] == baseline['effect_frames']
assert probe()['captures'] == captured['captures']
run('clock-resume')
wait(frame, lambda f: f['eligible'] == 1 and f['effect_frames'] > baseline['effect_frames'], 'time source did not resume')
probe('cancel')
run('settle')
wait(frame, lambda f: f['eligible'] == 0, 'released hidden source still eligible')
baseline = frame()
time.sleep(.3)
assert frame()['buffer_commits'] == baseline['buffer_commits'], 'released time source kept output awake'
print('hidden time-only source obeyed 8 fps, frozen pixels/commits stayed idle, release restored zero eligibility')
PY
