#!/usr/bin/env bash
# Pick the displayed authored pixels, then focus only after a successful native
# restore and the full swallowed sequence. The stage is deliberately not the
# bundled carousel transform, and the fit_all wrapper includes offscreen content.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"
trap 'echo "carousel click assertion at line $LINENO"; "$UMBRIEL" effects --json; "$UMBRIEL" windows --json' ERR
cat > "$UMBRIEL_RUNTIME_DIR/click.vert" <<'GLSL'
vec4 transition_vertex(vec2 uv) {
    vec2 normal = uv * umbriel_output_size;
    vec2 face = vec2(30.0 + 320.0 * float(umbriel_item_ordinal), 60.0)
        + vec2(uv.x * 260.0, uv.y * 190.0 + uv.x * 40.0);
    face.x += 160.0 * umbriel_navigation_position;
    vec2 point = mix(normal, face, umbriel_clamped_progress);
    float w = 1.0 + uv.x * .4 * umbriel_clamped_progress;
    float depth = min(abs(float(umbriel_item_ordinal) - umbriel_navigation_position), 1.0) * .5;
    return vec4((point / umbriel_output_size * 2.0 - 1.0) * w, depth * w, w);
}
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/click.frag" <<'GLSL'
vec4 transition_fragment(vec2 uv, vec2 output_uv) { return umbriel_sample_item(uv); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[output.HEADLESS-1]
mode = "640x360"
workspaces = 2
[animation]
duration_ms = 200
curve = "linear"
[animation.windows_in]
enabled = false
[animation.windows_out]
enabled = false
[animation.windows_move]
enabled = false
[input.focus]
follows_mouse = true
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[layout]
mode = "master"
[workspace_presentation]
effect = "click-scene"
framing = "viewport"
[effects.preset.click-scene]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "click.vert"
shader = "click.frag"
[[window_rule]]
match.title = "^click-hidden"
default_workspace = 2
default_focused = false
CONFIG
if [[ ${UMBRIEL_CAROUSEL_CLICK_FIT_ALL:-0} == 1 ]]; then
  sed -i 's/mode = "master"/mode = "scrolling"/; s/framing = "viewport"/framing = "fit_all"/' "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[layout.scrolling]
default_extent_fraction = 0.75
center_focused = "never"
CONFIG
fi
"$UMBRIEL" msg config-reload > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/target-input"
exec 7<> "$UMBRIEL_RUNTIME_DIR/target-input"
"$UMBRIEL_SEAT_LOG_CLIENT" click-native > "$UMBRIEL_RUNTIME_DIR/native.log" 2>&1 &
SOURCE_UPDATES=1 "$UMBRIEL_SEAT_LOG_CLIENT" click-hidden-target <&7 > "$UMBRIEL_RUNTIME_DIR/target.log" 2>&1 &
target_pid=$!
"$UMBRIEL_SEAT_LOG_CLIENT" click-hidden-other > "$UMBRIEL_RUNTIME_DIR/other.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 3 ]] && break
  sleep .02
done
"$UMBRIEL" settle
id() { "$UMBRIEL" windows --json | jq -r --arg title "$1" '.[] | select(.title==$title) | .id'; }
target=$(id click-hidden-target)
other=$(id click-hidden-other)
native=$(id click-native)
# Make the remembered workspace focus differ from the intended click target.
"$UMBRIEL" msg workspace-switch:2 > /dev/null
"$UMBRIEL" msg "window-focus:$other" > /dev/null
"$UMBRIEL" settle
"$UMBRIEL" msg workspace-switch:1 > /dev/null
"$UMBRIEL" msg "window-focus:$native" > /dev/null
"$UMBRIEL" settle
printf n >&7
"$UMBRIEL" clock-freeze
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .workspace_presentation'; }
advance() { "$UMBRIEL" clock-advance 1 > /dev/null; "$UMBRIEL" clock-advance 5000 > /dev/null; }
"$UMBRIEL" msg workspace-presentation-enter > /dev/null
advance
grim "$UMBRIEL_RUNTIME_DIR/held.png"
state | jq -e '.active and .phase=="held"' > /dev/null
if [[ ${UMBRIEL_CAROUSEL_CLICK_FIT_ALL:-0} == 1 ]]; then
  state | jq -e '.sources.faces[1].content_bounds[2]>640 and .sources.faces[1].content_framing[0]<1' > /dev/null
fi
# Choose a real green visible interior pixel; no production/native inverse
# coordinates or canonical carousel formula are used to locate the click.
read -r left top width height < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/held.png" bbox 'r<.05&&g>.7&&b<.3')
(( width>8 && height>8 ))
x=$((left+width/2)); y=$((top+height/2))
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/held.png" pixel "$x" "$y")
(( r<13 && g>180 && b<77 ))
"$UMBRIEL" output-commit-hold 'HEADLESS-1 on'
if [[ ${UMBRIEL_CAROUSEL_CLICK_COMMITTED:-0} == 1 ]]; then
  # Latest clocks move the green face well away from this pixel, but failed
  # submits cannot change what was displayed or its immutable pick snapshot.
  "$UMBRIEL" msg workspace-presentation-next > /dev/null
  advance
  state | jq -e '.navigation > .99 and .active' > /dev/null
fi
if [[ ${UMBRIEL_CAROUSEL_CLICK_TOUCH:-0} == 1 ]]; then
  "$UMBRIEL" presentation-touch-probe create
  tx=$(awk -v x="$x" 'BEGIN { print x/640 }')
  ty=$(awk -v y="$y" 'BEGIN { print y/360 }')
  "$UMBRIEL" presentation-touch-probe "down 7 $tx $ty"
else
  pointer_hold 640 360 move "$x" "$y" press 272 -- release 272 mark released hold
fi
state | jq -e --arg target "$target" '.active and .phase=="exiting" and .click_target==$target' > /dev/null
if [[ ${UMBRIEL_CAROUSEL_CLICK_REMOVE:-0} == 1 ]]; then
  kill "$target_pid"
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
    sleep .02
  done
fi
advance
"$UMBRIEL" windows --json | jq -e --arg id "$native" '.[] | select(.id==$id) | .active' > /dev/null
[[ $(events "$UMBRIEL_RUNTIME_DIR/target.log" 'pointer-button') == 0 ]]
"$UMBRIEL" output-commit-hold 'HEADLESS-1 off'
for _ in $(seq 100); do
  [[ $(state | jq .active) == false ]] && break
  sleep .02
done
# A real native capture establishes the restore commit while press remains held.
grim "$UMBRIEL_RUNTIME_DIR/restored.png"
"$UMBRIEL" presentation-input-probe status --json | jq -e '.pending and (.restore_pending|not)' > /dev/null
# Workspace activation may restore its remembered focus, but must not deliver
# the initiating button or resolve the projected click before its release.
if [[ ${UMBRIEL_CAROUSEL_CLICK_REMOVE:-0} == 1 ]]; then
  "$UMBRIEL" windows --json | jq -e --arg id "$target" 'all(.[]; .id!=$id)' > /dev/null
  target=$other
  delivery_log="$UMBRIEL_RUNTIME_DIR/other.log"
else
  "$UMBRIEL" windows --json | jq -e --arg id "$target" '.[] | select(.id==$id) | (.active|not)' > /dev/null
  delivery_log="$UMBRIEL_RUNTIME_DIR/target.log"
fi
if [[ ${UMBRIEL_CAROUSEL_CLICK_TOUCH:-0} == 1 ]]; then
  "$UMBRIEL" presentation-touch-probe 'up 7'
else
  pointer_step released
fi
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq -r '.[] | select(.active) | .id') == "$target" ]] && break
  sleep .02
done
"$UMBRIEL" windows --json | jq -e --arg id "$target" '.[] | select(.id==$id) | .active' > /dev/null
[[ $(events "$UMBRIEL_RUNTIME_DIR/target.log" 'pointer-button') == 0 ]]
[[ $(events "$UMBRIEL_RUNTIME_DIR/other.log" 'pointer-button') == 0 ]]
if [[ ${UMBRIEL_CAROUSEL_CLICK_TOUCH:-0} == 1 ]]; then
  [[ $(events "$UMBRIEL_RUNTIME_DIR/target.log" 'touch-down') == 0 ]]
  [[ $(events "$UMBRIEL_RUNTIME_DIR/target.log" 'touch-up') == 0 ]]
  "$UMBRIEL" presentation-touch-probe destroy
else
  pointer_release
fi
"$UMBRIEL" workspaces --json | jq -e '.[1].active' > /dev/null
"$UMBRIEL" settle
# The next ordinary native click is delivered once, after the selected window
# has been revealed by normal workspace focus/scroll policy.
read -r nx ny < <("$UMBRIEL" windows --json | jq -r --arg id "$target" '.[] | select(.id==$id) | "\(.x+.w/2|floor) \(.y+.h/2|floor)"')
"$UMBRIEL_POINTER_CLIENT" 640 360 move "$nx" "$ny" press 272 release 272
await_events "$delivery_log" 'pointer-button code=272 state=released' 1
[[ $(events "$delivery_log" 'pointer-button code=272 state=pressed') == 1 ]]
echo "Actual authored projected pixels selected the workspace and stable window after successful restore and swallowed release"
