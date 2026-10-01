#!/usr/bin/env bash
# Exercise the configured authored runtime, including a never-active client.
set -euo pipefail
trap 'echo "carousel assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 2
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
[effects.preset.carousel.parameters]
max_elevation_degrees = 35.0
[[window_rule]]
match.title = "^carousel-source"
default_workspace = 2
default_focused = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/held-input"
exec 7<> "$UMBRIEL_RUNTIME_DIR/held-input"
HOLD_RESIZE=1 LOG_OUTPUTS=1 "$UMBRIEL_SEAT_LOG_CLIENT" carousel-source-held <&7 > "$UMBRIEL_RUNTIME_DIR/held.log" 2>&1 &
held=$!
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
"$UMBRIEL_UNMAP_CLIENT" carousel-source-peer 600 320 > "$UMBRIEL_RUNTIME_DIR/peer.log" 2>&1 &
peer=$!
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
  sleep .02
done
"$UMBRIEL" clock-freeze
grim "$UMBRIEL_RUNTIME_DIR/native.png"
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .workspace_presentation'; }
"$UMBRIEL" msg workspace-presentation-enter
for _ in $(seq 200); do
  [[ $(state | jq '.active') == false ]] && break
  sleep .02
done
state | jq -e '(.active | not) and .memory_bytes==0 and .fallback=="source_unavailable"' > /dev/null
"$UMBRIEL" workspaces --json | jq -e '.[0].active and (.[1].active | not)' > /dev/null
[[ $(grep -c '^surface-output-enter' "$UMBRIEL_RUNTIME_DIR/held.log" || true) == 0 ]]
grim "$UMBRIEL_RUNTIME_DIR/fallback.png"
cmp "$UMBRIEL_RUNTIME_DIR/native.png" "$UMBRIEL_RUNTIME_DIR/fallback.png"
kill "$held" "$peer"
echo "Unacknowledged hidden source preparation expires without changing native membership or publishing a partial scene"
