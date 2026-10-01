#!/usr/bin/env bash
# Unequal 1/4/2/0 populations and an off-viewport scrolling row must retain
# equal face canvases, without client configure/position changes from framing.
set -euo pipefail
trap 'echo "carousel framing assertion at line $LINENO"; "$UMBRIEL" effects --json; "$UMBRIEL" windows --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 4
[animation]
duration_ms = 100
curve = "linear"
[animation.windows_in]
enabled = false
[animation.windows_move]
enabled = false
[layout]
mode = "scrolling"
[layout.scrolling]
default_extent_fraction = 0.75
center_focused = "never"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[workspace_presentation]
effect = "carousel"
framing = "viewport"
[effects.preset.carousel]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "carousel.vert"
shader = "carousel.frag"
[[window_rule]]
match.title = "^face-one"
default_workspace = 1
default_focused = false
[[window_rule]]
match.title = "^face-four"
default_workspace = 2
default_focused = false
[[window_rule]]
match.title = "^face-two"
default_workspace = 3
default_focused = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
spawned=0
spawn() {
  FILL_COLOR="$2" "$UMBRIEL_UNMAP_CLIENT" "$1" 600 320 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  spawned=$((spawned+1))
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == "$spawned" ]] && return
    sleep .02
  done
  exit 1
}
spawn face-one-a 0xFF660066
spawn face-four-a 0xFFFF0000
spawn face-four-b 0xFF00FF00
spawn face-four-c 0xFF0000FF
spawn face-four-d 0xFFFFFF00
spawn face-two-a 0xFF00FFFF
spawn face-two-b 0xFFFF00FF
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .workspace_presentation'; }
advance() { "$UMBRIEL" clock-advance 1; "$UMBRIEL" clock-advance 3000; }
for framing in viewport fit_all; do
  sed -i "s/^framing = .*/framing = \"$framing\"/" "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  "$UMBRIEL" msg workspace-presentation-enter
  advance
  "$UMBRIEL" msg workspace-presentation-select:2
  advance
  state | jq -e '.active and .phase=="held" and (.sources.faces | length)==4 and all(.sources.faces[]; .viewport==[0,0,640,360])' > /dev/null
  "$UMBRIEL" windows --json | jq '[.[] | {id,x,y,width,height,workspace}] | sort_by(.id)' > "$UMBRIEL_RUNTIME_DIR/$framing-geometry.json"
  state > "$UMBRIEL_RUNTIME_DIR/$framing-state.json"
  grim "$UMBRIEL_RUNTIME_DIR/$framing.png"
  if [[ $framing == fit_all ]]; then
    state | jq -e '.sources.faces[1] | .content_bounds[2]>640 and .extent[2]>640 and .content_framing[0]<1' > /dev/null
    # All four columns now contribute pixels within the same selected face.
    read -r red green blue yellow < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/$framing.png" count 'r>.8&&g<.1&&b<.1' 'r<.1&&g>.8&&b<.1' 'r<.1&&g<.1&&b>.8' 'r>.8&&g>.8&&b<.1')
    (( red>50 && green>50 && blue>50 && yellow>50 )) || { echo "fit_all omitted columns: $red $green $blue $yellow"; exit 1; }
  fi
  "$UMBRIEL" msg workspace-presentation-cancel
  advance
  "$UMBRIEL" settle
  state | jq -e '(.active | not) and .memory_bytes==0' > /dev/null
done
# At intermediate fit_all frames, solid windows must move as one image.
# Blending a viewport capture with a differently framed capture leaves large
# translucent duplicates instead of just a few filtered boundary pixels.
"$UMBRIEL" msg workspace-switch:2
advance
"$UMBRIEL" settle
"$UMBRIEL" msg workspace-presentation-enter
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 50
state | jq -e '.progress > 0.4 and .progress < 0.6' > /dev/null
grim "$UMBRIEL_RUNTIME_DIR/mid-entry.png"
intermediate_pixels() {
  "$UMBRIEL_PIXEL_PROBE" "$1" count '(r>.05&&r<.95)||(g>.05&&g<.95)||(b>.05&&b<.95)'
}
blended=$(intermediate_pixels "$UMBRIEL_RUNTIME_DIR/mid-entry.png")
(( blended < 3000 )) || { echo "entry has translucent duplicate windows: $blended pixels"; exit 1; }
advance
"$UMBRIEL" msg workspace-presentation-accept
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 50
grim "$UMBRIEL_RUNTIME_DIR/mid-exit.png"
blended=$(intermediate_pixels "$UMBRIEL_RUNTIME_DIR/mid-exit.png")
(( blended < 3000 )) || { echo "exit has translucent duplicate windows: $blended pixels"; exit 1; }
advance
"$UMBRIEL" settle
cmp "$UMBRIEL_RUNTIME_DIR/viewport-geometry.json" "$UMBRIEL_RUNTIME_DIR/fit_all-geometry.json"
if cmp -s "$UMBRIEL_RUNTIME_DIR/viewport.png" "$UMBRIEL_RUNTIME_DIR/fit_all.png"; then
  echo "viewport and fit_all failed to distinguish the off-viewport row"
  exit 1
fi
echo "Authored carousel retained equal faces for 1/4/2/0 windows, exposed all scrolling columns with fit_all, and preserved native geometry"
