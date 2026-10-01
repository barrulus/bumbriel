#!/usr/bin/env bash
# Start captures after the frozen outgoing client's destruction.
set -euo pipefail
trap 'echo "pair capture assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/melt/shader.glsl" "$UMBRIEL_RUNTIME_DIR/melt.glsl"
cat > "$UMBRIEL_RUNTIME_DIR/tint.glsl" <<'GLSL'
vec4 window(vec2 uv) { vec4 c = umbriel_sample(uv); return vec4(c.a, 0.0, 0.0, c.a); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[output.HEADLESS-1]
mode = "640x360"
workspaces = 2
[animation]
enabled = true
duration_ms = 1000
curve = "linear"
[animation.windows_in]
enabled = false
[animation.windows_out]
enabled = false
[animation.windows_move]
enabled = false
[animation.workspaces]
effect = "melt"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[effects]
window = "tint"
in_capture = false
[effects.preset.tint]
kind = "window"
shader = "tint.glsl"
[effects.preset.melt]
kind = "animation"
interface = "scene-v1"
scope = "workspace_pair"
shader = "melt.glsl"
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" frozen-client 600 320 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
client=$!
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
"$UMBRIEL" msg workspace-switch:2
for _ in $(seq 100); do
  [[ $("$UMBRIEL" effects --json | jq '[.owners[] | select(.type == "output") | .workspace_transition.source_ready][0]') == true ]] && break
  sleep .02
done
kill "$client"
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 0 ]] && break
  sleep .02
done
grim "$UMBRIEL_RUNTIME_DIR/unfiltered.png"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/unfiltered.png" pixel 320 180)
(( b > 240 && r < 10 && g < 10 )) || { echo "unfiltered frozen source: $r $g $b"; exit 1; }
# Switching capture policy selects the other retained role, without the gone
# client or its shaders being available to re-render that outgoing scene.
sed -i 's/in_capture = false/in_capture = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
grim "$UMBRIEL_RUNTIME_DIR/display.png"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/display.png" pixel 320 180)
(( r > 240 && g < 10 && b < 10 )) || { echo "display frozen source: $r $g $b"; exit 1; }
"$UMBRIEL" clock-advance 1500
"$UMBRIEL" settle
"$UMBRIEL" effects --json | jq -e '[.owners[] | select(.type == "output") | .workspace_transition | (.active | not) and .memory_bytes == 0] | all' > /dev/null
echo 'Post-freeze captures preserve both outgoing roles after the original client is destroyed'
