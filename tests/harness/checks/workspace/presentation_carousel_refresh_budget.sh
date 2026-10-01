#!/usr/bin/env bash
# A populated 1080p carousel must leave room for live refreshes before
# admitting its first frame. Distinct display/capture roles force downscaling.
set -Eeuo pipefail
trap 'echo "carousel assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cat > "$UMBRIEL_RUNTIME_DIR/border.glsl" <<'GLSL'
vec4 border(vec2 uv) { return vec4(0.1, 0.3, 0.5, 1.0); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "1920x1080"
workspaces = 9
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
[effects]
border = "paired-border"
in_capture = false
[effects.preset.paired-border]
kind = "border"
shader = "border.glsl"
padding = 48
[colors]
backdrop = "#000000FF"
[workspace_presentation]
effect = "carousel"
framing = "fit_all"
[effects.preset.carousel]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "carousel.vert"
shader = "carousel.frag"
[effects.preset.carousel.parameters]
max_elevation_degrees = 35.0
[[window_rule]]
match.title = "^carousel-source$"
default_workspace = 2
default_focused = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/carousel-input"
exec 7<> "$UMBRIEL_RUNTIME_DIR/carousel-input"
SOURCE_UPDATES=1 LOG_OUTPUTS=1 "$UMBRIEL_SEAT_LOG_CLIENT" carousel-source <&7 > "$UMBRIEL_RUNTIME_DIR/carousel-client.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
for i in $(seq 1 9); do
  "$UMBRIEL" msg workspace-switch:$i
  "$UMBRIEL_UNMAP_CLIENT" "face-$i" 936 1018 > "$UMBRIEL_RUNTIME_DIR/face-$i.log" 2>&1 &
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == $((i+1)) ]] && break
    sleep .02
  done
  [[ $("$UMBRIEL" windows --json | jq length) == $((i+1)) ]]
done
"$UMBRIEL" msg workspace-switch:1
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/native-workspaces.json"
state() {
  "$UMBRIEL" effects --json | jq '.owners[] | select(.type == "output" and .name == "HEADLESS-1") | .workspace_presentation'
}
"$UMBRIEL" msg workspace-presentation-enter
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
assert_held() {
  state | jq -e --slurpfile native "$UMBRIEL_RUNTIME_DIR/native-workspaces.json" '
    .active and .phase == "held" and .memory_bytes > 0 and .memory_bytes <= 268435456 and
    (.sources.ids | length) == 9 and .sources.ids == [$native[0][] | .id] and
    any(.sources.faces[]; .width < 1920) and .sources.landing_width == 1920 and .sources.landing_height == 1080
  ' > /dev/null
}
assert_held
captures=$(state | jq '.sources.captures')
printf n >&7
for _ in $(seq 100); do
  [[ $(state | jq --argjson previous "$captures" '.sources.captures > $previous') == true ]] && break
  sleep .02
done
state | jq -e --argjson previous "$captures" '.sources.captures > $previous' > /dev/null
assert_held
"$UMBRIEL" msg workspace-presentation-select:9
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
assert_held
state | jq -e '(.navigation - ((.navigation / 9)|floor) * 9) == 8' > /dev/null
"$UMBRIEL" msg workspace-presentation-cancel
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
state | jq -e '(.active | not) and .memory_bytes == 0' > /dev/null
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/after.json"
cmp "$UMBRIEL_RUNTIME_DIR/native-workspaces.json" "$UMBRIEL_RUNTIME_DIR/after.json"
echo 'Nine populated 1080p faces downscale before acquisition, refresh live, navigate and release their budget'
