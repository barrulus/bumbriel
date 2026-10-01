#!/usr/bin/env bash
# Exercise the configured authored runtime, including a never-active client.
set -euo pipefail
trap 'echo "carousel assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 3
[animation]
enabled = true
duration_ms = 100
curve = "linear"
[animation.windows_in]
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
match.title = "^carousel-source$"
default_workspace = 2
default_focused = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/carousel-input"
exec 7<> "$UMBRIEL_RUNTIME_DIR/carousel-input"
SOURCE_UPDATES=1 LOG_OUTPUTS=1 "$UMBRIEL_SEAT_LOG_CLIENT" carousel-source <&7 > "$UMBRIEL_RUNTIME_DIR/carousel-client.log" 2>&1 &
client=$!
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/native-workspaces.json"
grim "$UMBRIEL_RUNTIME_DIR/native.png"
state() {
  "$UMBRIEL" effects --json | jq '.owners[] | select(.type == "output" and .name == "HEADLESS-1") | .workspace_presentation'
}
"$UMBRIEL" msg workspace-presentation-enter
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
state | jq -e '.active and .phase == "held" and .memory_bytes > 0' > /dev/null
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/held-workspaces.json"
cmp "$UMBRIEL_RUNTIME_DIR/native-workspaces.json" "$UMBRIEL_RUNTIME_DIR/held-workspaces.json"
"$UMBRIEL" msg workspace-presentation-select:2
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
grim "$UMBRIEL_RUNTIME_DIR/face.png"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/face.png" pixel 320 180)
(( b > 170 && r < 80 && g > 90 )) || { echo "authored hidden face not blue: $r $g $b"; exit 1; }
[[ $(grep -c '^surface-output-enter' "$UMBRIEL_RUNTIME_DIR/carousel-client.log" || true) == 0 ]]
# Held mode permits settle and inspection cannot allocate a new candidate.
"$UMBRIEL" settle
state > "$UMBRIEL_RUNTIME_DIR/held-state.json"
state > "$UMBRIEL_RUNTIME_DIR/inspected-state.json"
cmp "$UMBRIEL_RUNTIME_DIR/held-state.json" "$UMBRIEL_RUNTIME_DIR/inspected-state.json"
"$UMBRIEL" msg workspace-presentation-cancel
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
state | jq -e '(.active | not) and .memory_bytes == 0' > /dev/null
grim "$UMBRIEL_RUNTIME_DIR/cancelled.png"
cmp "$UMBRIEL_RUNTIME_DIR/native.png" "$UMBRIEL_RUNTIME_DIR/cancelled.png"
"$UMBRIEL" msg workspace-presentation-enter
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" msg workspace-presentation-select:2
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" msg workspace-presentation-accept
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
state | jq -e '(.active | not) and .memory_bytes == 0' > /dev/null
"$UMBRIEL" workspaces --json | jq -e '.[1].active' > /dev/null
grim "$UMBRIEL_RUNTIME_DIR/accepted.png"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/accepted.png" pixel 320 180)
(( b > 170 && r < 80 && g > 90 ))
kill "$client"
echo "Configured authored carousel displays hidden live sources, preserves membership, settles idle, cancels and commits"
