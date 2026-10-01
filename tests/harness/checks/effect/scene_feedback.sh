#!/usr/bin/env bash
# Virtual workspace occurrences own histories independently of native owners;
# successful final submission, not capture/retry, advances those histories.
set -euo pipefail
helper=$(realpath "$(dirname "$UMBRIEL_UNMAP_CLIENT")/audio-synthetic")
cp "$UMBRIEL_REPO/examples/effects/scene/melt/shader.glsl" "$UMBRIEL_RUNTIME_DIR/melt.glsl"
cat > "$UMBRIEL_RUNTIME_DIR/history.glsl" <<'GLSL'
vec4 window(vec2 uv) {
  vec4 p = umbriel_sample_previous(uv);
  float token = umbriel_audio_rms();
  float step = abs(p.g - token) > 0.01 ? 0.1 : 0.0;
  return vec4(p.r + step, token, umbriel_sample(uv).b, 1.0);
}
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/faces.vert" <<'GLSL'
vec4 transition_vertex(vec2 uv) {
  return vec4(uv.x - 1.0 + float(umbriel_item_ordinal), uv.y * 2.0 - 1.0, 0.0, 1.0);
}
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/faces.frag" <<'GLSL'
vec4 transition_fragment(vec2 uv, vec2 output_uv) { return umbriel_sample_item(uv); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<EOF
[output.HEADLESS-1]
mode = "640x360"
workspaces = 2
[animation]
enabled = true
duration_ms = 100
curve = "linear"
[animation.windows_in]
enabled = false
[animation.windows_out]
enabled = false
[animation.windows_move]
enabled = false
[animation.workspaces]
effect = "melt"
duration_ms = 1000
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[workspace_presentation]
effect = "faces"
[effects]
window = "history"
in_capture = true
[effects.preset.history]
kind = "window"
shader = "history.glsl"
audio = "fixture"
[effects.preset.faces]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "faces.vert"
shader = "faces.frag"
[effects.preset.melt]
kind = "animation"
interface = "scene-v1"
scope = "workspace_pair"
shader = "melt.glsl"
[effects.audio.sources.fixture]
provider = "external"
mode = "playback"
target = "explicit-history-fixture"
executable = "$helper"
args = ["--external-test", "--silence"]
[[window_rule]]
match.title = "^history-hidden$"
default_workspace = 2
default_focused = false
EOF
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" history-native 600 320 > "$UMBRIEL_RUNTIME_DIR/native-client.log" 2>&1 &
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" history-hidden 600 320 > "$UMBRIEL_RUNTIME_DIR/hidden-client.log" 2>&1 &
python3 - <<'PY'
import json
import os
import subprocess
import time

exe = os.environ['UMBRIEL']
def run(*args):
    return subprocess.check_output([exe, *args], text=True, timeout=10)
def data(command):
    return json.loads(run(command, '--json'))
def state():
    return next(o['workspace_presentation'] for o in data('effects')['owners'] if o['type'] == 'output' and o['name'] == 'HEADLESS-1')
def frame():
    return data('effect-frames')['outputs'][0]
def latch():
    return next(a for a in frame()['audio'] if a['source'] == 'fixture')
def wait(get, predicate, reason):
    end = time.monotonic() + 5
    while True:
        value = get()
        if predicate(value):
            return value
        assert time.monotonic() < end, (reason, value)
        time.sleep(.01)
def inject(value, submitted=True):
    run('audio-inject', json.dumps(dict(source='fixture', rms=value, peak=value, envelope=value, bands=[value] * 16)))
    if submitted:
        wait(latch, lambda a: not a['pending'] and abs(a['presented_rms'] - value) < 1e-6, 'audio not submitted')
def pixels(xs, red, green):
    image = os.path.join(os.environ['UMBRIEL_RUNTIME_DIR'], 'scene-history.png')
    subprocess.run(['grim', '-s', '1', '-o', 'HEADLESS-1', image], check=True, timeout=10)
    for x in xs:
        rgb = list(map(int, subprocess.check_output([os.environ['UMBRIEL_PIXEL_PROBE'], image, 'pixel', str(x), '180'], text=True).split()))
        assert abs(rgb[0] - round(red * 255)) <= 5 and abs(rgb[1] - round(green * 255)) <= 3 and rgb[2] >= 250, ('history pixel', x, red, green, rgb)

wait(lambda: data('windows'), lambda w: len(w) == 2, 'clients did not map')
wait(lambda: data('effects')['audio'], lambda a: a and a[0]['available'], 'helper not available')
run('settle')
run('clock-freeze')
inject(0)
for value in (.2, .4, .6):
    inject(value)
pixels([320], .3, .6)
run('msg', 'workspace-presentation-enter')
run('clock-advance', '1')
run('clock-advance', '3000')
wait(state, lambda s: s['active'] and s['phase'] == 'held', 'feedback faces were not admitted')
# Both faces start from their own source, including the hidden native owner.
# The already accumulated native history must not seed either occurrence.
pixels([160, 480], .1, .6)
inject(.8)
pixels([160, 480], .2, .8)
run('settle')
before = frame()
time.sleep(.15)
assert frame()['buffer_commits'] == before['buffer_commits'], 'unchanged feedback presentation woke itself'
run('output-commit-hold', 'HEADLESS-1 on')
inject(.1, False)
wait(frame, lambda f: f['rejected_buffer_commits'] > before['rejected_buffer_commits'], 'no failed candidate')
inject(.9, False)
time.sleep(.15)
assert abs(latch()['latched_rms'] - .1) < 1e-6, 'retry discarded the held source snapshot'
run('output-commit-hold', 'HEADLESS-1 off')
wait(latch, lambda a: not a['pending'] and abs(a['presented_rms'] - .9) < 1e-6, 'queued input not presented')
pixels([160, 480], .4, .9)
run('msg', 'workspace-presentation-cancel')
run('clock-advance', '1')
run('clock-advance', '3000')
run('settle')
wait(state, lambda s: not s['active'] and s['memory_bytes'] == 0, 'history resources retained after cancel')
pixels([320], .4, .9)
def pair():
    return next(o['workspace_transition'] for o in data('effects')['owners'] if o['type'] == 'output' and o['name'] == 'HEADLESS-1')
run('msg', 'workspace-switch:2')
wait(pair, lambda s: s['active'] and s['source_ready'], 'feedback melt was not admitted')
# A frozen outgoing endpoint replays its committed native history, unlike a
# newly created live occurrence. Client teardown and later input cannot alter it.
pixels([320], .4, .9)
native = next(w for w in data('windows') if w['title'] == 'history-native')
run('msg', 'window-close:' + str(native['id']))
wait(lambda: data('windows'), lambda ws: all(w['title'] != 'history-native' for w in ws), 'outgoing client did not unmap')
inject(.5)
pixels([320], .4, .9)
run('clock-advance', '1500')
run('settle')
wait(pair, lambda s: not s['active'] and s['memory_bytes'] == 0, 'frozen history resources retained')
print('Independent virtual feedback, failed-submit promotion, native isolation, exact frozen melt and resource release verified')
PY
