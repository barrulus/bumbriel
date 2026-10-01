#!/usr/bin/env bash
# Configured window_scene borrows the native lifecycle clock while two layout
# neighbours keep their longer motion, then returns to native presentation.
set -euo pipefail
trap 'echo "window scene assertion at line $LINENO"; "$UMBRIEL" effects --json; "$UMBRIEL" windows --json' ERR
preset=${UMBRIEL_WINDOW_SCENE_PRESET:-water}
cp "$UMBRIEL_REPO/examples/effects/scene/$preset/"*.glsl "$UMBRIEL_RUNTIME_DIR/"
cp "$UMBRIEL_REPO/examples/effects/scene/$preset/shader.vert" "$UMBRIEL_RUNTIME_DIR/water.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/$preset/shader.frag" "$UMBRIEL_RUNTIME_DIR/water.frag"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
[layout]
mode = "master"
[layout.master]
new_becomes_master = true
[animation]
enabled = false
[effects.preset.water]
kind = "animation"
interface = "scene-v1"
scope = "window_scene"
common_shader = "common.glsl"
vertex_shader = "water.vert"
shader = "water.frag"
composite_shader = "composite.glsl"
[animation.windows_in]
effect = "water"
duration_ms = 1000
curve = "linear"
[animation.windows_out]
effect = "water"
duration_ms = 800
curve = "linear"
[animation.windows_move]
duration_ms = 2400
curve = "linear"
CONFIG
if [[ ${UMBRIEL_WINDOW_SCENE_CAPTURE:-0} == 1 ]]; then
  cat > "$UMBRIEL_RUNTIME_DIR/once.glsl" <<'GLSL'
vec4 screen(vec2 uv) {
  vec4 c = umbriel_sample(uv);
  return vec4(c.r * 0.5 + c.a * 0.125, c.g, c.b, c.a);
}
GLSL
  cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[effects]
in_capture = true
screen = "off"
[effects.preset.once]
kind = "screen"
shader = "once.glsl"
CONFIG
fi
if [[ ${UMBRIEL_WINDOW_SCENE_TRANSFORM:-0} == 1 ]]; then
  sed -i '/^mode = "640x360"$/a scale = 1.25\ntransform = "90"' "$UMBRIEL_CONFIG"
fi
if [[ ${UMBRIEL_WINDOW_SCENE_1080P:-0} == 1 ]]; then
  sed -i 's/mode = "640x360"/mode = "1920x1080"/' "$UMBRIEL_CONFIG"
fi
if [[ ${UMBRIEL_WINDOW_SCENE_LIT:-0} == 1 ]]; then
  cat >> "$UMBRIEL_CONFIG" <<CONFIG

[include]
files = ["$UMBRIEL_REPO/examples/effects/border/pulse/effect.toml"]
[effects]
border = "pulse"
CONFIG
fi
if [[ ${UMBRIEL_WINDOW_SCENE_FLOATING:-0} == 1 ]]; then
  cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[[window_rule]]
match.title = "^scene-"
default_floating = true
default_floating_size_px = { width = 260, height = 180 }
CONFIG
fi
"$UMBRIEL" msg config-reload > /dev/null
spawn() {
  if [[ ${UMBRIEL_WINDOW_SCENE_RECOVERY:-0} == 1 ]]; then
    FILL_COLOR="$2" RESIZE_FILL_COLOR="$2" "$UMBRIEL_UNMAP_CLIENT" "$1" 600 320 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  else
    FILL_COLOR="$2" "$UMBRIEL_UNMAP_CLIENT" "$1" 600 320 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  fi
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq --arg title "$1" 'any(.[]; .title==$title)') == true ]] && return
    sleep .02
  done
  exit 1
}
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .window_presentation'; }
spawn scene-neighbour-a 0xFFFF0000
spawn scene-neighbour-b 0xFFFF00FF
"$UMBRIEL" settle
sed -i 's/^enabled = false$/enabled = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
spawn scene-target 0xFF0000FF
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 250
grim "$UMBRIEL_RUNTIME_DIR/opening.png"
state > "$UMBRIEL_RUNTIME_DIR/opening.json"
jq -e '.active and (.pending | not) and .frames>0 and .memory_bytes>0 and (.deadline_msec-.start_msec)==1000' "$UMBRIEL_RUNTIME_DIR/opening.json" > /dev/null
if [[ ${UMBRIEL_WINDOW_SCENE_FLOATING:-0} == 0 ]]; then
  jq -e '[.items[] | select(.kind==1 and .source_box != .destination_box and .motion_progress>0 and .motion_progress<1)] | length>=2' "$UMBRIEL_RUNTIME_DIR/opening.json" > /dev/null
fi
if [[ ${UMBRIEL_WINDOW_SCENE_CAPTURE:-0} == 1 ]]; then
  id=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title=="scene-neighbour-a") | .id')
  grim -T "$id" "$UMBRIEL_RUNTIME_DIR/isolated.png"
  read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/isolated.png" pixel 300 160)
  (( r>250 && g<3 && b<3 ))
  sed -i 's/screen = "off"/screen = "once"/' "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  grim "$UMBRIEL_RUNTIME_DIR/screen-once.png"
  state | jq -e '.active' > /dev/null
  # The first config application can arrange native participants. Compare both
  # capture roles after that barrier, while the scene clock remains frozen.
  state > "$UMBRIEL_RUNTIME_DIR/screen-pose.json"
  sed -i 's/in_capture = true/in_capture = false/' "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  grim "$UMBRIEL_RUNTIME_DIR/screen-unfiltered.png"
  state | jq -e --slurpfile before "$UMBRIEL_RUNTIME_DIR/screen-pose.json" '.active and .views==$before[0].views' > /dev/null
  python3 - <<'PYSCREEN'
import os, subprocess
root = os.environ['UMBRIEL_RUNTIME_DIR']
probe = os.environ['UMBRIEL_PIXEL_PROBE']
def pixel(name, x, y):
    return list(map(int, subprocess.check_output([probe, root+'/'+name, 'pixel', str(x), str(y)], text=True).split()))
for y in (45, 135, 225, 315):
    for x in (80, 240, 400, 560):
        before = pixel('screen-unfiltered.png', x, y)
        after = pixel('screen-once.png', x, y)
        wanted = [round(before[0]*.5 + 255*.125), before[1], before[2]]
        assert all(abs(a-b)<=3 for a,b in zip(after,wanted)), ('screen stage not exactly once', x,y,before,after,wanted)
PYSCREEN
  grim -T "$id" "$UMBRIEL_RUNTIME_DIR/isolated-with-screen.png"
  read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/isolated-with-screen.png" pixel 300 160)
  (( r>250 && g<3 && b<3 ))
  sed -i 's/screen = "once"/screen = "off"/' "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
fi
if [[ ${UMBRIEL_WINDOW_SCENE_RECOVERY:-0} == 1 ]]; then
  "$UMBRIEL" renderer-recover > /dev/null
  for _ in $(seq 100); do
    [[ $(state | jq '.active') == false ]] && break
    sleep .02
  done
  state | jq -e '(.active|not) and .memory_bytes==0 and .fallback=="renderer_lost"' > /dev/null
  # Recovery invalidates old client textures. Exercise real fresh attaches from
  # every client before requiring the next transaction to acquire its sources.
  sed -i 's/mode = "640x360"/mode = "642x362"/' "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  grim "$UMBRIEL_RUNTIME_DIR/recovered-native.png"
fi
"$UMBRIEL" clock-advance 750
grim "$UMBRIEL_RUNTIME_DIR/open-endpoint.png"
state | jq -e '(.active | not) and .memory_bytes==0' > /dev/null
state > "$UMBRIEL_RUNTIME_DIR/native-deadline.json"
"$UMBRIEL" clock-advance 100
grim "$UMBRIEL_RUNTIME_DIR/native-after-deadline.png"
state > "$UMBRIEL_RUNTIME_DIR/native-after-deadline.json"
if [[ ${UMBRIEL_WINDOW_SCENE_FLOATING:-0} == 0 && ${UMBRIEL_WINDOW_SCENE_RECOVERY:-0} == 0 ]]; then
  jq -e --slurpfile before "$UMBRIEL_RUNTIME_DIR/native-deadline.json" '
    [.views[] | select(.title=="scene-neighbour-a" or .title=="scene-neighbour-b") |
      . as $later | $before[0].views[] | select(.id==$later.id and .box!=$later.box)] | length==2
  ' "$UMBRIEL_RUNTIME_DIR/native-after-deadline.json" > /dev/null
fi
"$UMBRIEL" clock-advance 2000
"$UMBRIEL" settle
id=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title=="scene-target") | .id')
"$UMBRIEL" msg "window-close:$id" > /dev/null
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
  sleep .02
done
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 200
grim "$UMBRIEL_RUNTIME_DIR/closing.png"
state > "$UMBRIEL_RUNTIME_DIR/closing.json"
jq -e '.active and .snapshot>0 and .memory_bytes>0 and (.deadline_msec-.start_msec)==800' "$UMBRIEL_RUNTIME_DIR/closing.json" > /dev/null
if [[ ${UMBRIEL_WINDOW_SCENE_LIT:-0} == 1 ]]; then
  jq -e '.target_token as $target | all(.items[]; .kind!=4 or .token!=$target) and any(.items[]; .kind==4 and .token!=$target)' "$UMBRIEL_RUNTIME_DIR/closing.json" > /dev/null
fi
"$UMBRIEL" clock-advance 600
grim "$UMBRIEL_RUNTIME_DIR/close-endpoint.png"
state | jq -e '(.active | not) and .memory_bytes==0' > /dev/null
if [[ -n ${UMBRIEL_PRESENTATION_ARTIFACTS:-} ]]; then
  directory="$UMBRIEL_PRESENTATION_ARTIFACTS/window-$preset-${UMBRIEL_WINDOW_SCENE_FLOATING:-0}-${UMBRIEL_WINDOW_SCENE_LIT:-0}"
  mkdir -p "$directory"
  cp "$UMBRIEL_RUNTIME_DIR/"*.png "$directory/"
  cp "$UMBRIEL_RUNTIME_DIR/opening.json" "$UMBRIEL_RUNTIME_DIR/closing.json" "$directory/"
fi
echo "Configured window scene rendered open and retained close with two moving neighbours, ordinary shadows, and original independent deadlines"
