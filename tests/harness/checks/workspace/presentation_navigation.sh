#!/usr/bin/env bash
# Real pointer axis events exercise carousel navigation without activating the
# native workspace. Finger loss cancels and wrapping is independent of native output policy.
set -euo pipefail
trap 'echo "navigation assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/nav.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/nav.frag"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 3
cyclic_workspaces = false
[animation]
enabled = true
duration_ms = 100
curve = "linear"
[animation.overview]
workspace_curve = "spring:1,120"
[workspace_presentation]
effect = "nav"
[effects.preset.nav]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "nav.vert"
shader = "nav.frag"
[effects.preset.nav.parameters]
max_elevation_degrees = 35.0
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
"$UMBRIEL_POINTER_CLIENT" 640 360 move 320 180
"$UMBRIEL" clock-freeze
"$UMBRIEL" msg workspace-presentation-enter
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type == "output") | .workspace_presentation'; }
nav_is() { state | jq -e --argjson n "$1" '(.navigation - ((.navigation / 3)|floor) * 3 - $n) | fabs < 0.01' > /dev/null; }
advance() { "$UMBRIEL" clock-advance 1 > /dev/null; "$UMBRIEL" clock-advance 3000 > /dev/null; }
native_is_one() { "$UMBRIEL" workspaces --json | jq -e '.[0].active and (.[1].active|not) and (.[2].active|not)' > /dev/null; }
"$UMBRIEL_POINTER_CLIENT" 640 360 notch 1
advance
nav_is 1
native_is_one
"$UMBRIEL_POINTER_CLIENT" 640 360 notch-horizontal 1
advance
nav_is 2
"$UMBRIEL_POINTER_CLIENT" 640 360 notch 1
advance
nav_is 0
for axis in vertical horizontal; do
  "$UMBRIEL_POINTER_CLIENT" 640 360 axis "$axis" 300 pause 400 axis-stop "$axis" &
  pointer=$!
  moved=false
  for _ in $(seq 80); do
    if state | jq -e '(.navigation - ((.navigation / 3)|floor) * 3) as $n | $n > 0.5 and $n < 0.7' > /dev/null; then moved=true; break; fi
    sleep .005
  done
  [[ $moved == true ]] || { echo "carousel $axis did not follow finger travel"; state; exit 1; }
  native_is_one
  wait "$pointer"
  advance
  nav_is 1
  "$UMBRIEL" msg workspace-presentation-select:1 > /dev/null
  advance
  # Device destruction without a stop event cancels back to its selected face.
  "$UMBRIEL_POINTER_CLIENT" 640 360 axis "$axis" 300
  advance
  nav_is 0
  native_is_one
  # Three-finger events enter the same navigation path with swipe travel units.
  "$UMBRIEL" swipe-inject "begin 3 100" > /dev/null
  if [[ $axis == horizontal ]]; then
    "$UMBRIEL" swipe-inject "update -180 0 110" > /dev/null
  else
    "$UMBRIEL" swipe-inject "update 0 -180 110" > /dev/null
  fi
  "$UMBRIEL" clock-advance 1 > /dev/null
  state | jq -e '(.navigation - ((.navigation / 3)|floor) * 3) as $n | $n > 0.59 and $n < 0.61' > /dev/null
  native_is_one
  "$UMBRIEL" swipe-inject "end 400" > /dev/null
  advance
  nav_is 1
  "$UMBRIEL" msg workspace-presentation-select:1 > /dev/null
  advance
  nav_is 0
done
# The native overview gesture cannot open a competing mode while held.
"$UMBRIEL" swipe-inject "begin 4 500" > /dev/null
"$UMBRIEL" swipe-inject "update 0 -300 510" > /dev/null
"$UMBRIEL" swipe-inject "end 800" > /dev/null
advance
state | jq -e '.active' > /dev/null
nav_is 0
"$UMBRIEL" msg workspace-presentation-cancel
advance
"$UMBRIEL" settle > /dev/null
state | jq -e '(.active|not)' > /dev/null
native_is_one
# Keyboard actions also cross both seams with native wrapping disabled.
"$UMBRIEL" msg workspace-presentation-enter > /dev/null
advance
"$UMBRIEL" msg workspace-presentation-previous > /dev/null
advance
nav_is 2
state | jq -e '(.navigation + 1) | fabs < 0.01' > /dev/null
"$UMBRIEL" msg workspace-presentation-next > /dev/null
advance
nav_is 0
state | jq -e '.navigation | fabs < 0.01' > /dev/null
"$UMBRIEL_POINTER_CLIENT" 640 360 notch -1
advance
nav_is 2
"$UMBRIEL_POINTER_CLIENT" 640 360 notch 1
advance
nav_is 0
# A reverse swipe crosses directly from the first face to the last.
"$UMBRIEL" swipe-inject "begin 3 900" > /dev/null
"$UMBRIEL" swipe-inject "update 180 0 910" > /dev/null
"$UMBRIEL" clock-advance 1 > /dev/null
state | jq -e '.navigation > -0.61 and .navigation < -0.59' > /dev/null
"$UMBRIEL" swipe-inject "end 1200" > /dev/null
advance
nav_is 2
state | jq -e '(.navigation + 1) | fabs < 0.01' > /dev/null
native_is_one
"$UMBRIEL" msg workspace-presentation-cancel > /dev/null
advance
"$UMBRIEL" settle > /dev/null
echo "carousel wheel/finger navigation followed input, retained native membership, wrapped cyclically, and cancelled lost devices"
