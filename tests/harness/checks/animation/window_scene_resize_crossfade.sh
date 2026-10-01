#!/usr/bin/env bash
# Replacing only windows_in must retain the independently running old-size
# client-buffer fade. An identity scene makes that opacity directly measurable.
set -euo pipefail
trap 'echo "resize crossfade assertion at line $LINENO"; "$UMBRIEL" effects --json | jq ".owners[] | select(.type==\"output\") | .window_presentation"; "$UMBRIEL" windows --json' ERR
cat > "$UMBRIEL_RUNTIME_DIR/identity.vert" <<'GLSL'
vec4 transition_vertex(vec2 uv) {
  vec2 p = umbriel_capture_extent.xy + uv * umbriel_capture_extent.zw;
  return vec4(p / umbriel_output_size * 2.0 - 1.0, 0.0, 1.0);
}
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/identity.frag" <<'GLSL'
vec4 transition_fragment(vec2 uv, vec2 output_uv) { return umbriel_sample_item(uv); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[output.HEADLESS-1]
mode = "640x360"
[input.focus]
follows_mouse = false
[layout]
mode = "master"
[layout.master]
new_becomes_master = true
[animation]
enabled = false
[animation.windows_in]
effect = "identity-scene"
duration_ms = 2000
curve = "linear"
[animation.windows_move]
duration_ms = 2000
curve = "linear"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[effects.preset.identity-scene]
kind = "animation"
interface = "scene-v1"
scope = "window_scene"
vertex_shader = "identity.vert"
shader = "identity.frag"
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" resize-neighbour 600 320 > "$UMBRIEL_RUNTIME_DIR/neighbour.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
"$UMBRIEL" settle
sed -i 's/^enabled = false$/enabled = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
FILL_COLOR=0xFFFF0000 RESIZE_FILL_COLOR=0xFF00FF00 "$UMBRIEL_UNMAP_CLIENT" resize-target 600 320 > "$UMBRIEL_RUNTIME_DIR/target.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq '[.[] | select(.title=="resize-target" and .w!=600)] | length') == 1 ]] && break
  sleep .02
done
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output") | .window_presentation'; }
"$UMBRIEL" clock-advance 1
first_width=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title=="resize-target") | .w')
"$UMBRIEL" msg window-set-primary-extent:0.7 > /dev/null
for _ in $(seq 100); do
  width=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title=="resize-target") | .w')
  [[ $width != "$first_width" ]] && break
  sleep .02
done
[[ $width != "$first_width" ]]
# An isolated capture proves the client actually attached its new green buffer;
# IPC layout geometry alone could still precede that acknowledgement/commit.
id=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title=="resize-target") | .id')
for _ in $(seq 100); do
  grim -T "$id" "$UMBRIEL_RUNTIME_DIR/client.png"
  read -r red green blue < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/client.png" pixel 100 150)
  (( red<3 && green>250 && blue<3 )) && break
  sleep .02
done
(( red<3 && green>250 && blue<3 ))
"$UMBRIEL" clock-advance 500
grim "$UMBRIEL_RUNTIME_DIR/quarter.png"
state | jq -e '.active and (.deadline_msec-.start_msec)==2000' > /dev/null
read -r red1 green1 blue1 < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/quarter.png" pixel 100 150)
"$UMBRIEL" clock-advance 500
grim "$UMBRIEL_RUNTIME_DIR/half.png"
read -r red2 green2 blue2 < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/half.png" pixel 100 150)
if ! (( red1>red2 && green2>green1 && green1>20 && red2>20 && blue1<3 && blue2<3 )); then
  echo "independent resize fade was lost: quarter=$red1,$green1,$blue1 half=$red2,$green2,$blue2"
  state
  exit 1
fi
"$UMBRIEL" clock-advance 1100
grim "$UMBRIEL_RUNTIME_DIR/endpoint.png"
read -r red green blue < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/endpoint.png" pixel 100 150)
(( red<3 && green>250 && blue<3 ))
state | jq -e '(.active|not) and .memory_bytes==0' > /dev/null
echo "Opening lifecycle bypass preserves the old-size client's independent red-to-green resize crossfade"
