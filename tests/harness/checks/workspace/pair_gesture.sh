#!/usr/bin/env bash
# Inject swipe events through the actual cursor signals. This establishes the
# compositor route; hardware libinput acquisition remains a separate gate.
set -euo pipefail
trap 'echo "pair gesture assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/melt/shader.glsl" "$UMBRIEL_RUNTIME_DIR/melt.glsl"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 3
workspace_axis = "vertical"
[animation]
enabled = true
duration_ms = 100
curve = "linear"
[animation.windows_in]
enabled = false
[animation.windows_move]
enabled = false
[animation.workspaces]
effect = "melt"
[effects.preset.melt]
kind = "animation"
interface = "scene-v1"
scope = "workspace_pair"
shader = "melt.glsl"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[[window_rule]]
match.title = "^pair-red$"
default_workspace = 2
default_focused = false
[[window_rule]]
match.title = "^pair-blue$"
default_workspace = 3
default_focused = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFFFF0000 "$UMBRIEL_UNMAP_CLIENT" pair-red 640 360 > /dev/null 2>&1 &
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" pair-blue 640 360 > /dev/null 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
  sleep .02
done
"$UMBRIEL_POINTER_CLIENT" 640 360 move 320 180
"$UMBRIEL" clock-freeze
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
    return next(o['workspace_transition'] for o in data('effects')['owners'] if o['type'] == 'output')
def native():
    return next(w['index'] for w in data('workspaces') if w['active'])
def advance(ms=3000):
    run('clock-advance', '1')
    run('clock-advance', str(ms))
def inject(command):
    result = json.loads(run('swipe-inject', command, '--json'))
    assert result is True or result.get('ok') is True, result

def wait(get, predicate, reason):
    end = time.monotonic() + 4
    while True:
        value = get()
        if predicate(value):
            return value
        assert time.monotonic() < end, (reason, value)
        time.sleep(.01)
def picture(name):
    path = os.path.join(os.environ['UMBRIEL_RUNTIME_DIR'], name + '.png')
    subprocess.run(['grim', '-s', '1', path], check=True, timeout=10)
    return open(path, 'rb').read()

def origin():
    run('msg', 'workspace-switch:2')
    advance()
    run('settle')
    assert native() == 2

for axis in ('vertical', 'horizontal'):
    config = os.environ['UMBRIEL_CONFIG']
    text = open(config).read().replace('workspace_axis = "vertical"', 'workspace_axis = "' + axis + '"')
    open(config, 'w').write(text)
    run('msg', 'config-reload')
    origin()
    def move(value, ms):
        inject(f'update {value if axis == "horizontal" else 0} {value if axis == "vertical" else 0} {ms}')
        run('clock-advance', '1')
    inject('begin 3 100')
    move(-180, 110)
    first = wait(state, lambda s: s['active'] and s['source_ready'], 'pair swipe did not acquire scene')
    assert first['interactive'] and abs(first['progress'] - .6) < 1e-6, first
    assert first['from'].endswith(':2') and first['to'].endswith(':3') and native() == 2, first
    forward = picture(axis + '-forward')
    move(60, 120)
    reverse = state()
    assert reverse['identity'] == first['identity'] and abs(reverse['progress'] - .4) < 1e-6, reverse
    assert picture(axis + '-reverse') != forward, 'gesture progress did not move authored pixels'
    move(-60, 130)
    assert state()['identity'] == first['identity']
    assert picture(axis + '-retraced') == forward, 'reversal changed pair seed or outgoing snapshot'
    # Cross the origin into the opposite pair, then release before waiting for
    # native restoration. The queued final progress/settle must survive.
    inject(f'update {390 if axis == "horizontal" else 0} {390 if axis == "vertical" else 0} 140')
    crossed = state()
    assert crossed['active'] and crossed['identity'] != first['identity'] and crossed['to'].endswith(':1'), crossed
    inject('end 400')
    advance()
    wait(state, lambda s: not s['active'], 'opposite pair never retired')
    assert native() == 1, ('queued opposite release lost destination', state(), native())
    # Removing the device cancels a held pair back to the original workspace.
    origin()
    inject('begin 3 500')
    move(-210, 510)
    wait(state, lambda s: s['active'] and s['source_ready'], 'second pair missing')
    inject('remove')
    advance()
    wait(state, lambda s: not s['active'], 'lost-device pair never retired')
    assert native() == 2
    # Explicit overview opening cancels the transition; the
    # remainder of that physical swipe cannot recreate the pair underneath it.
    inject('begin 3 700')
    move(-180, 710)
    wait(state, lambda s: s['active'] and s['source_ready'], 'overview interruption pair missing')
    run('msg', 'overview-open')
    advance()
    assert not state()['active']
    move(-90, 720)
    inject('end 1000')
    advance()
    assert not state()['active'] and native() == 2, 'old swipe revived pair under overview'
    run('msg', 'overview-close')
    advance()
    run('settle')
print('both-axis scene swipes preserved reversible pixels/identity, retargeted across origin, retained queued releases, and cancelled lost devices')
PY
