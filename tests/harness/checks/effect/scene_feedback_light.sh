#!/usr/bin/env bash
# A new virtual occurrence of a focused border starts independent feedback.
# Its halo and retry history must match literal native/carousel references.
set -euo pipefail
helper=$(realpath "$(dirname "$UMBRIEL_UNMAP_CLIENT")/audio-synthetic")
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cat > "$UMBRIEL_RUNTIME_DIR/history-light.glsl" <<'GLSL'
vec4 border(vec2 uv) {
  vec4 p = umbriel_sample_previous(uv);
  float token = umbriel_audio_rms();
  bool seeded = abs(p.b - 0.137) < 0.01;
  float old = seeded ? p.r : 0.0;
  float step = !seeded || abs(p.g - token) > 0.01 ? 0.1 : 0.0;
  return vec4(min(old + step, 0.9), token, 0.137, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<CONFIG
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
[appearance]
border_width = 4
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[workspace_presentation]
effect = "carousel"
[effects]
border = "history-light"
in_capture = true
[effects.preset.history-light]
kind = "border"
shader = "history-light.glsl"
audio = "fixture"
animated = false
[effects.preset.history-light.light]
spread = 40
intensity = 4
threshold = 0
[effects.preset.carousel]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "carousel.vert"
shader = "carousel.frag"
[effects.audio.sources.fixture]
provider = "external"
mode = "playback"
target = "explicit-cold-feedback-light"
executable = "$helper"
args = ["--external-test", "--silence"]
[[window_rule]]
match.title = "^cold-history-light$"
default_floating = true
default_position = { x = 200, y = 120, anchor = "top_left" }
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF000000 "$UMBRIEL_UNMAP_CLIENT" cold-history-light 240 120 > "$UMBRIEL_RUNTIME_DIR/hidden-client.log" 2>&1 &
python3 - <<'PY'
import json
import os
import subprocess
import time

exe = os.environ['UMBRIEL']
root = os.environ['UMBRIEL_RUNTIME_DIR']
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
    run('audio-inject', json.dumps(dict(source='fixture', rms=value, peak=value, envelope=value, bands=[value]*16)))
    if submitted:
        wait(latch, lambda a: not a['pending'] and abs(a['presented_rms']-value)<1e-6, 'input not presented')
def picture(name):
    path = os.path.join(root, name+'.png')
    subprocess.run(['grim', '-s', '1', '-o', 'HEADLESS-1', path], check=True, timeout=10)
    return path
def pixels(path):
    return [[int(c) for c in subprocess.check_output([os.environ['UMBRIEL_PIXEL_PROBE'], path, 'pixel', str(x), str(y)], text=True).split()] for x,y in points]
def enter():
    run('msg','workspace-presentation-enter')
    run('clock-advance','1')
    run('clock-advance','3000')
    wait(state,lambda s:s['active'] and s['phase']=='held','cold lit sources not admitted')
    run('msg','workspace-presentation-select:1')
    run('clock-advance','1')
    run('clock-advance','3000')
    run('settle')
def dismiss():
    run('msg','workspace-presentation-cancel')
    run('clock-advance','1')
    run('clock-advance','3000')
    run('settle')
    wait(state,lambda s:not s['active'] and s['memory_bytes']==0,'retained light/history resources')

windows=wait(lambda:data('windows'),lambda w:len(w)==1,'hidden client did not map')
window=windows[0]
run('settle')
run('clock-freeze')
native_workspaces=data('workspaces')
# Held selected canonical face is front-facing at scale .68. Sample its ring
# and the native halo well outside the undeformed client rectangle.
cx=window['x']+window['w']/2
points=[(round(320+.68*(cx-320)),round(180+.68*(window['y']-d-180))) for d in (2,24)]
wait(lambda:data('effects')['audio'],lambda a:a and a[0]['available'],'helper unavailable')
inject(.2)
inject(.6)
native_picture=picture('native-three-steps')
enter()
assert data('workspaces')==native_workspaces, 'presentation activated another workspace'
actual={.1:pixels(picture('cold-first'))}
assert actual[.1][1][1]>5, ('cold source did not synthesize a halo',points,actual)
inject(.8)
actual[.2]=pixels(picture('cold-second'))
run('settle')
before=frame()
time.sleep(.15)
assert frame()['buffer_commits']==before['buffer_commits'],'unchanged feedback woke itself'
run('output-commit-hold','HEADLESS-1 on')
inject(.1,False)
wait(frame,lambda f:f['rejected_buffer_commits']>before['rejected_buffer_commits'],'no failed candidate')
inject(.9,False)
time.sleep(.15)
assert abs(latch()['latched_rms']-.1)<1e-6,'retry replaced held feedback input'
run('output-commit-hold','HEADLESS-1 off')
wait(latch,lambda a:not a['pending'] and abs(a['presented_rms']-.9)<1e-6,'queued input not presented')
actual[.4]=pixels(picture('cold-retry'))
assert data('workspaces')==native_workspaces,'retry changed native workspace selection'
dismiss()
# Independent literal emission oracle includes exactly the same native
# threshold/pyramid/screen blend and authored carousel geometry.
for red,green in ((.1,.6),(.2,.8),(.4,.9)):
    with open(os.path.join(root,'history-light.glsl'),'w') as shader:
        shader.write(f'vec4 border(vec2 uv) {{ return vec4({red}, {green}, 0.137, 1.0); }}\n')
    run('msg','config-reload')
    run('settle')
    if red == .1:
        with open(os.path.join(root,'history-light.glsl'),'w') as shader:
            shader.write('vec4 border(vec2 uv) { return vec4(0.3,0.6,0.137,1.0); }\n')
        run('msg','config-reload')
        run('settle')
        native_reference=picture('native-literal')
        native_points=[(round(cx),round(window['y']-d)) for d in (2,24)]
        def native_pixels(path):
            return [[int(c) for c in subprocess.check_output([os.environ['UMBRIEL_PIXEL_PROBE'],path,'pixel',str(x),str(y)],text=True).split()] for x,y in native_points]
        assert all(abs(a-b)<=2 for p,q in zip(native_pixels(native_picture),native_pixels(native_reference)) for a,b in zip(p,q)), ('native feedback reference',native_pixels(native_picture),native_pixels(native_reference))
        with open(os.path.join(root,'history-light.glsl'),'w') as shader:
            shader.write(f'vec4 border(vec2 uv) {{ return vec4({red}, {green}, 0.137, 1.0); }}\n')
        run('msg','config-reload')
        run('settle')
    enter()
    reference=pixels(picture('literal-'+str(red)))
    assert all(abs(a-b)<=2 for p,q in zip(actual[red],reference) for a,b in zip(p,q)), ('feedback halo disagrees with literal history',red,points,actual[red],reference)
    dismiss()
print('Cold virtual feedback border, actual halo, held idle, failed-submit history and literal emission references verified')
PY
