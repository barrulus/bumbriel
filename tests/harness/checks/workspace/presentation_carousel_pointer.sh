#!/usr/bin/env bash
# Mouse elevation changes the authored geometry without recapturing workspaces;
# the same pointer snapshot drives picking and the native endpoint stays exact.
set -Eeuo pipefail
trap 'echo "carousel pointer assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[output.HEADLESS-1]
mode = "640x360"
workspaces = 6
[animation]
duration_ms = 100
curve = "linear"
[animation.windows_in]
enabled = false
[animation.windows_out]
enabled = false
[animation.windows_move]
enabled = false
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[effects]
in_capture = true
[workspace_presentation]
effect = "carousel"
[effects.preset.carousel]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "carousel.vert"
shader = "carousel.frag"
[effects.preset.carousel.parameters]
max_elevation_degrees = 75.0
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF00CC33 "$UMBRIEL_UNMAP_CLIENT" elevation-window 600 300 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
window=$("$UMBRIEL" windows --json | jq -er '.[0].id')
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .workspace_presentation'; }
move() {
  "$UMBRIEL_POINTER_CLIENT" 640 360 move "$1" "$2" > /dev/null
  "$UMBRIEL" settle > /dev/null
}
shot() { grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/$1.png"; }
move 320 180
"$UMBRIEL" clock-freeze
shot native
"$UMBRIEL" msg workspace-presentation-enter
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
state | jq -e '.active and .phase=="held" and .pointer==[0.5,0.5] and (.sources.ids|length)==6' > /dev/null
captures=$(state | jq '.sources.captures')
shot centre
move 320 36
state | jq -e '.pointer[1] > .099 and .pointer[1] < .101' > /dev/null
shot above
! cmp -s "$UMBRIEL_RUNTIME_DIR/centre.png" "$UMBRIEL_RUNTIME_DIR/above.png"
move 320 324
shot below
! cmp -s "$UMBRIEL_RUNTIME_DIR/above.png" "$UMBRIEL_RUNTIME_DIR/below.png"
move 400 324
shot sideways
cmp "$UMBRIEL_RUNTIME_DIR/below.png" "$UMBRIEL_RUNTIME_DIR/sideways.png"
move 320 180
shot centred-again
cmp "$UMBRIEL_RUNTIME_DIR/centre.png" "$UMBRIEL_RUNTIME_DIR/centred-again.png"
state | jq -e --argjson captures "$captures" '.active and .sources.captures==$captures' > /dev/null
# Click a verified window pixel while looking down. Picking must use the tilted
# displayed frame, and pointer motion during exit must not change that endpoint.
move 320 90
shot click
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/click.png" pixel 320 90)
(( r<13 && g>180 && b<77 ))
"$UMBRIEL_POINTER_CLIENT" 640 360 press 272 release 272 > /dev/null
state | jq -e --arg window "$window" '.active and .phase=="exiting" and .click_target==$window' > /dev/null
"$UMBRIEL_POINTER_CLIENT" 640 360 move 320 324 > /dev/null
state | jq -e '.pointer==[0.5,0.25]' > /dev/null
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
state | jq -e '(.active|not) and .memory_bytes==0' > /dev/null
shot restored
cmp "$UMBRIEL_RUNTIME_DIR/native.png" "$UMBRIEL_RUNTIME_DIR/restored.png"
echo 'Pointer elevation redraws six faces without recapturing, preserves picking and returns to exact native pixels'
