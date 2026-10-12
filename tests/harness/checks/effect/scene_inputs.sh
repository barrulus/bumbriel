#!/usr/bin/env bash
# The configured authored workspace runtime consumes the same output audio latch
# as native effects, without requiring a TIME uniform or a real audio device.
set -euo pipefail
cat > "$UMBRIEL_RUNTIME_DIR/input.vert" <<'GLSL'
vec4 transition_vertex(vec2 uv) { return vec4(uv * 2.0 - 1.0, 0.0, 1.0); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/input.frag" <<'GLSL'
vec4 transition_fragment(vec2 uv, vec2 output_uv) {
  return vec4(0.0, umbriel_audio.x, 0.0, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<EOF_CONFIG

[output.HEADLESS-1]
mode = "640x360"
workspaces = 1
[animation]
enabled = true
duration_ms = 100
curve = "linear"
[workspace_presentation]
effect = "input"
[effects]
in_capture = true
max_fps = 8
[effects.preset.input]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "input.vert"
shader = "input.frag"
EOF_CONFIG
if [[ ${UMBRIEL_SCENE_SCREEN_ONCE:-0} == 1 ]]; then
  cat > "$UMBRIEL_RUNTIME_DIR/screen-once.glsl" <<'GLSL'
vec4 screen(vec2 uv) {
  vec4 c = umbriel_sample(uv);
  return vec4(c.r + 0.125, c.g * 0.5, c.b, 1.0);
}
GLSL
  sed -i '/^\[effects\]$/a screen = "screen_once"' "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[effects.preset.screen_once]
kind = "screen"
shader = "screen-once.glsl"
CONFIG
fi
"$UMBRIEL" msg config-reload > /dev/null
for _ in $(seq 100); do
  (( $(grep -c 'config reloaded' "$UMBRIEL_RUNTIME_DIR/compositor.log") >= 2 )) && break
  sleep .02
done
(( $(grep -c 'config reloaded' "$UMBRIEL_RUNTIME_DIR/compositor.log") >= 2 ))
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" isolated-scene-input 240 160 > "$UMBRIEL_RUNTIME_DIR/isolated-client.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
"$UMBRIEL" settle > /dev/null
python3 - <<'PY'
import json
import os
import subprocess
import time
import sys
sys.path.insert(0, os.path.join(os.environ['UMBRIEL_REPO'], 'tests', 'harness'))
from audio_feed import AudioFeed
feed = AudioFeed()

exe = os.environ['UMBRIEL']
def run(*args):
    return subprocess.check_output([exe, *args], text=True, timeout=10)
def data(command):
    return json.loads(run(command, '--json'))
def state():
    return next(o['workspace_presentation'] for o in data('effects')['owners'] if o['type'] == 'output' and o['name'] == 'HEADLESS-1')
def frame():
    return data('effect-frames')['outputs'][0]
def wait(get, predicate, reason):
    end = time.monotonic() + 5
    while True:
        value = get()
        if predicate(value):
            return value
        assert time.monotonic() < end, (reason, value)
        time.sleep(.01)
def inject(value):
    feed.set(value)
def pixel(value):
    image = os.path.join(os.environ['UMBRIEL_RUNTIME_DIR'], 'scene-input.png')
    subprocess.run(['grim', '-s', '1', image], check=True, timeout=10)
    rgb = [int(v) for v in subprocess.check_output([os.environ['UMBRIEL_PIXEL_PROBE'], image, 'pixel', '320', '180'], text=True).split()]
    screen = os.environ.get('UMBRIEL_SCENE_SCREEN_ONCE') == '1'
    expected = [32 if screen else 0, round(value * 255 * (.5 if screen else 1)), 0]
    assert all(abs(actual - want) <= 3 for actual, want in zip(rgb, expected)), ('authored frame audio and single screen pass', value, rgb, expected)

run('clock-freeze')
run('msg', 'workspace-presentation-enter')
run('clock-advance', '1')
run('clock-advance', '3000')
wait(state, lambda s: s['active'] and s['phase'] == 'held', 'authored scene never held')
for value in (.8, 0.0):
    inject(value)
    time.sleep(.2)
    pixel(value)
window = data('windows')[0]
image = os.path.join(os.environ['UMBRIEL_RUNTIME_DIR'], 'isolated-scene-input.png')
subprocess.run(['grim', '-T', str(window['id']), image], check=True, timeout=10)
rgb = list(map(int, subprocess.check_output([os.environ['UMBRIEL_PIXEL_PROBE'], image, 'pixel', str(round(window['w'] / 2)), str(round(window['h'] / 2))], text=True).split()))
assert rgb[0] <= 2 and rgb[1] <= 2 and rgb[2] >= 250, ('isolated toplevel received desktop presentation or screen pass', rgb)
run('settle')
before = frame()
# Real time: silent, frozen, no-TIME authored mode must remain idle.
time.sleep(.3)
assert frame()['buffer_commits'] == before['buffer_commits'], ('silent held mode kept committing', before, frame())
assert frame()['effect_frames'] == before['effect_frames']
run('output-commit-hold', 'HEADLESS-1 on')
inject(.25)
wait(frame, lambda f: f['rejected_buffer_commits'] > 0, 'authored candidate never reached failed submission')
inject(.75)
time.sleep(.15)
run('output-commit-hold', 'HEADLESS-1 off')
time.sleep(.2)
pixel(.75)
run('msg', 'workspace-presentation-cancel')
run('clock-advance', '1')
run('clock-advance', '3000')
run('settle')
wait(state, lambda s: not s['active'], 'authored scene did not retire')
assert frame()['eligible'] == 0
print('authored no-TIME scene latched inputs, preserved isolated toplevel capture and one screen pass, stayed idle, retried failed candidates and released scene ownership')
feed.close()
PY
