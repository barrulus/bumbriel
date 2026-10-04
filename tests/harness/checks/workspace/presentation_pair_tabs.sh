#!/usr/bin/env bash
# Workspace source overrides must not reveal hidden tabs beneath translucent shown tabs.
set -euo pipefail

cat > "$UMBRIEL_RUNTIME_DIR/tab-pair.glsl" <<'SHADER'
vec4 transition(vec2 uv) {
  return umbriel_progress < 0.5 ? umbriel_sample_from(uv) : umbriel_sample_to(uv);
}
SHADER
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
[animation.windows_move]
enabled = false
[animation.workspaces]
effect = "tab-pair"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[appearance.blur]
enabled = false
[colors]
backdrop = "#000000FF"
[effects.preset.tab-pair]
kind = "animation"
interface = "scene-v1"
scope = "workspace_pair"
shader = "tab-pair.glsl"
[[window_rule]]
match.title = "^pair-tab-"
default_scrolling_column = "pair-tabs"
CONFIG
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  local title=$1 color=$2
  RESIZE_FILL_COLOR="$color" "$UMBRIEL_UNMAP_CLIENT" "$title" 600 400 > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
  for _ in $(seq 100); do
    if "$UMBRIEL" windows --json | jq -e --arg title "$title" 'any(.[]; .title == $title)' > /dev/null; then
      return 0
    fi
    sleep .02
  done
  echo "window did not map: $title"
  return 1
}
state() {
  "$UMBRIEL" effects --json | jq '.owners[] | select(.type == "output" and .name == "HEADLESS-1") | .workspace_transition'
}
ready() {
  for _ in $(seq 100); do
    [[ $(state | jq .source_ready) == true ]] && return 0
    sleep .02
  done
  state
  return 1
}
assert_face() {
  local name=$1 channel=$2 r g b
  grim "$UMBRIEL_RUNTIME_DIR/$name.png"
  read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/$name.png" pixel 320 180)
  if (( r > 5 )) || { [[ $channel == green ]] && (( g < 100 || b > 5 )); } \
      || { [[ $channel == blue ]] && (( b < 100 || g > 5 )); }; then
    echo "$name exposed a hidden red tab or lost the shown $channel tab: $r $g $b"
    return 1
  fi
}

spawn pair-tab-from-hidden 0xFFFF0000
spawn pair-tab-from-shown 0x80008000
"$UMBRIEL" msg column-toggle-tabbed > /dev/null
"$UMBRIEL" settle
assert_face from-native green
"$UMBRIEL" msg workspace-switch:2 > /dev/null
"$UMBRIEL" settle
spawn pair-tab-to-hidden 0xFFFF0000
spawn pair-tab-to-shown 0x80000080
"$UMBRIEL" msg column-toggle-tabbed > /dev/null
"$UMBRIEL" settle
assert_face to-native blue
"$UMBRIEL" windows --json | jq -e '[.[] | select(.tab_hidden)] | length == 2' > /dev/null
"$UMBRIEL" msg workspace-switch:1 > /dev/null
"$UMBRIEL" settle

"$UMBRIEL" clock-freeze
"$UMBRIEL" msg workspace-switch:2 > /dev/null
ready
assert_face from-captured green
"$UMBRIEL" clock-advance 750
ready
assert_face to-captured blue
"$UMBRIEL" clock-advance 1500
"$UMBRIEL" settle
assert_face to-finished blue
state | jq -e '(.active | not) and .memory_bytes == 0' > /dev/null
echo 'Workspace pair preserves hidden tabs in frozen source and live destination'
