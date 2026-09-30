#!/usr/bin/env bash
# Ctrl + wheel/finger scrolling zooms without navigating or recapturing;
# bounds, picking, the native endpoint and ordinary navigation remain intact.
set -Eeuo pipefail
trap 'echo "carousel zoom assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
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
advance() { "$UMBRIEL" clock-advance 1; "$UMBRIEL" clock-advance 3000; }
"$UMBRIEL" msg workspace-presentation-enter
advance
state | jq -e '.active and .zoom==1 and .navigation==0' > /dev/null
captures=$(state | jq '.sources.captures')
shot baseline
area() { "$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/$1.png" count 'r<.05&&g>.7&&b<.3'; }
"$UMBRIEL_POINTER_CLIENT" 640 360 mod control notch -1 notch -1 mod none > /dev/null
"$UMBRIEL" settle
state | jq -e '.zoom>1.25 and .zoom<1.26 and .navigation==0' > /dev/null
shot wheel
(( $(area wheel) > $(area baseline) * 13 / 10 ))
"$UMBRIEL_POINTER_CLIENT" 640 360 mod control notch 1 notch 1 mod none > /dev/null
"$UMBRIEL" settle
shot reset
cmp "$UMBRIEL_RUNTIME_DIR/baseline.png" "$UMBRIEL_RUNTIME_DIR/reset.png"
"$UMBRIEL_POINTER_CLIENT" 640 360 mod control axis vertical -30 axis-stop vertical mod none > /dev/null
"$UMBRIEL" settle
state | jq -e '.zoom>1.25 and .zoom<1.26 and .navigation==0' > /dev/null
shot finger
cmp "$UMBRIEL_RUNTIME_DIR/wheel.png" "$UMBRIEL_RUNTIME_DIR/finger.png"
"$UMBRIEL_POINTER_CLIENT" 640 360 mod control axis horizontal 300 axis-stop horizontal mod none > /dev/null
state | jq -e '.zoom>1.25 and .zoom<1.26 and .navigation==0' > /dev/null
"$UMBRIEL_POINTER_CLIENT" 640 360 mod control axis vertical 10000 axis-stop vertical mod none > /dev/null
"$UMBRIEL" settle
state | jq -e '.zoom==0.25 and .navigation==0' > /dev/null
"$UMBRIEL_POINTER_CLIENT" 640 360 mod control axis vertical -10000 axis-stop vertical mod none > /dev/null
"$UMBRIEL" settle
state | jq -e --argjson captures "$captures" '.zoom==3 and .navigation==0 and .sources.captures==$captures' > /dev/null
# Releasing Ctrl restores ordinary carousel navigation without changing zoom.
"$UMBRIEL_POINTER_CLIENT" 640 360 notch 1 > /dev/null
advance
state | jq -e '.zoom==3 and (.navigation-1|fabs)<0.01' > /dev/null
"$UMBRIEL" msg workspace-presentation-select:1
advance
move 320 90
shot click
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/click.png" pixel 320 90)
(( r<13 && g>180 && b<77 ))
"$UMBRIEL_POINTER_CLIENT" 640 360 press 272 release 272 > /dev/null
state | jq -e --arg window "$window" '.active and .phase=="exiting" and .click_target==$window' > /dev/null
"$UMBRIEL_POINTER_CLIENT" 640 360 mod control notch 1 notch 1 mod none > /dev/null
state | jq -e '.zoom==3' > /dev/null
advance
"$UMBRIEL" settle
state | jq -e '(.active|not) and .memory_bytes==0' > /dev/null
shot restored
cmp "$UMBRIEL_RUNTIME_DIR/native.png" "$UMBRIEL_RUNTIME_DIR/restored.png"
move 320 180
"$UMBRIEL" msg workspace-presentation-enter
advance
state | jq -e '.active and .zoom==1' > /dev/null
shot reentered
cmp "$UMBRIEL_RUNTIME_DIR/baseline.png" "$UMBRIEL_RUNTIME_DIR/reentered.png"
echo 'Ctrl + mouse/touchpad zoom preserves navigation, source captures, tilted picking and native restoration'
