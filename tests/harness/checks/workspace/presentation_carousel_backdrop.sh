#!/usr/bin/env bash
# Exercise the shipped universe preset, its colour/solid/off controls, input
# isolation, native restoration and unchanged memory/capture cost while idle.
set -Eeuo pipefail
trap 'echo "carousel backdrop assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
for file in effect.toml shader.vert shader.frag backdrop.glsl; do
  cp "$UMBRIEL_REPO/examples/effects/scene/carousel/$file" "$UMBRIEL_RUNTIME_DIR/$file"
done
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[include]
files = ["effect.toml"]
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
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF00CC33 "$UMBRIEL_UNMAP_CLIENT" universe-window 600 300 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
window=$("$UMBRIEL" windows --json | jq -er '.[0].id')
"$UMBRIEL_POINTER_CLIENT" 640 360 move 320 180 > /dev/null
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .workspace_presentation'; }
advance() { "$UMBRIEL" clock-advance 1; "$UMBRIEL" clock-advance 3000; }
shot() { grim -s 1 -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/$1.png"; }
enter() { "$UMBRIEL" msg workspace-presentation-enter; advance; state | jq -e '.active and .phase=="held"' > /dev/null; }
dismiss() { "$UMBRIEL" msg workspace-presentation-cancel; advance; "$UMBRIEL" settle; }
shot native
enter
shot universe
memory=$(state | jq '.memory_bytes')
captures=$(state | jq '.sources.captures')
(( $("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/universe.png" count 'r>.4||g>.4||b>.4' '640x40+0+0') > 5 ))
# A static universe is independent of animation time and source acquisition.
"$UMBRIEL" clock-advance 5000
shot held
cmp "$UMBRIEL_RUNTIME_DIR/universe.png" "$UMBRIEL_RUNTIME_DIR/held.png"
state | jq -e --argjson captures "$captures" '.sources.captures==$captures' > /dev/null
# A background touch cannot pick a workspace through the backdrop.
"$UMBRIEL" presentation-touch-probe create
"$UMBRIEL" presentation-touch-probe 'down 7 0.01 0.01'
"$UMBRIEL" presentation-touch-probe 'up 7'
state | jq -e '.active and .phase=="held"' > /dev/null
# A window at the centre still picks with the new background stage enabled.
"$UMBRIEL_POINTER_CLIENT" 640 360 press 272 release 272 > /dev/null
state | jq -e --arg window "$window" '.phase=="exiting" and .click_target==$window' > /dev/null
advance
"$UMBRIEL" settle
shot restored
cmp "$UMBRIEL_RUNTIME_DIR/native.png" "$UMBRIEL_RUNTIME_DIR/restored.png"
# Use unmistakable custom colours so this verifies configuration, not defaults.
sed -i 's/backdrop_style = 2.0/backdrop_style = 1.0/; s/backdrop_color = .*/backdrop_color = [0.2, 0.1, 0.3]/' "$UMBRIEL_RUNTIME_DIR/effect.toml"
"$UMBRIEL" msg config-reload > /dev/null
enter
shot solid
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/solid.png" pixel 10 10)
(( r>=50 && r<=52 && g>=25 && g<=27 && b>=76 && b<=78 ))
dismiss
sed -i 's/backdrop_style = 1.0/backdrop_style = 0.0/' "$UMBRIEL_RUNTIME_DIR/effect.toml"
"$UMBRIEL" msg config-reload > /dev/null
enter
shot off
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/off.png" pixel 10 10)
(( r==0 && g==0 && b==0 ))
state | jq -e --argjson memory "$memory" '.memory_bytes==$memory' > /dev/null
dismiss
state | jq -e '(.active|not) and .memory_bytes==0' > /dev/null
# Optional output for visual review of the real rendered shader.
if [[ -n ${UMBRIEL_BACKDROP_PREVIEW:-} ]]; then
  cp "$UMBRIEL_RUNTIME_DIR/universe.png" "$UMBRIEL_BACKDROP_PREVIEW"
fi
echo 'Shipped universe, solid/custom colours and off modes preserve picking, idle captures and exact native restoration'
