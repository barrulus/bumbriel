#!/usr/bin/env bash
# Floating is a workspace layout: protect an ordinary layout-managed window,
# reveal two updating floats beneath it, and preserve protection on layout changes.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[layout]
mode = "floating"
[animation]
enabled = false
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.title_bar]
mode = "off"
[appearance.shadow]
enabled = false
[appearance.blur]
enabled = false
[[window_rule]]
match.title = "^left$"
default_floating_size_px = { width = 400, height = 400 }
default_position = { x = 100, y = 100, anchor = "top_left" }
[[window_rule]]
match.title = "^right$"
default_floating_size_px = { width = 400, height = 400 }
default_position = { x = 650, y = 100, anchor = "top_left" }
[[window_rule]]
match.title = "^protected$"
block_capture = false
default_floating_size_px = { width = 1280, height = 720 }
default_position = { x = 0, y = 0, anchor = "top_left" }
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
set_capture() {
  sed -i "s/^block_capture = .*/block_capture = $1/" "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
}
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/floating-capture.png"
window() { "$UMBRIEL" windows --json | jq -ce --arg title "$1" '.[] | select(.title == $title)'; }
wait_window() {
  for _ in $(seq 100); do
    window "$1" > /dev/null && return
    sleep 0.02
  done
  echo "missing window: $1"; return 1
}
geometry() { "$UMBRIEL" windows --json | jq -c 'sort_by(.id) | map([.id,.x,.y,.w,.h,.floating])'; }
assert_hidden() {
  grim "$IMAGE"
  [[ $("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r>0.9&&b>0.9&&g<0.1') == 0 ]] || {
    echo 'protected pixels appeared in capture'; exit 1;
  }
}
mkfifo "$UMBRIEL_RUNTIME_DIR/left" "$UMBRIEL_RUNTIME_DIR/right"
exec 8<> "$UMBRIEL_RUNTIME_DIR/left"
exec 9<> "$UMBRIEL_RUNTIME_DIR/right"
REPAINT_ON_STDIN=1 FOLLOW_CONFIGURES=1 FILL_COLOR=0xFF00FF00 \
  "$UMBRIEL_UNMAP_CLIENT" left 400 400 <&8 > "$UMBRIEL_RUNTIME_DIR/left.log" 2>&1 &
wait_window left
REPAINT_ON_STDIN=1 FOLLOW_CONFIGURES=1 FILL_COLOR=0xFF0000FF \
  "$UMBRIEL_UNMAP_CLIENT" right 400 400 <&9 > "$UMBRIEL_RUNTIME_DIR/right.log" 2>&1 &
wait_window right
OPAQUE_BUFFER=1 FOLLOW_CONFIGURES=1 FILL_COLOR=0xFFFF00FF \
  "$UMBRIEL_UNMAP_CLIENT" protected 1280 720 > "$UMBRIEL_RUNTIME_DIR/protected.log" 2>&1 &
wait_window protected
id=$(window protected | jq -r .id)
"$UMBRIEL" settle > /dev/null
[[ $("$UMBRIEL" workspaces --json | jq -r '.[] | select(.active) | .layout') == floating ]]
"$UMBRIEL" windows --json | jq -e 'length == 3 and all(.floating)' > /dev/null
[[ $(window protected | jq -c '[.x,.y,.w,.h]') == '[0,0,1280,720]' ]]
grim "$IMAGE"
[[ $("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r>0.9&&b>0.9&&g<0.1') == 921600 ]]
before=$(geometry)
set_capture true
"$UMBRIEL_CAPTURE_CLIENT" --output HEADLESS-1 > "$UMBRIEL_RUNTIME_DIR/output.log" 2>&1 &
await_lines "$UMBRIEL_RUNTIME_DIR/output.log" 'frame ' 1
"$UMBRIEL" settle > /dev/null
[[ $(geometry) == "$before" ]]
assert_hidden
read -r green blue < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'g>0.9&&r<0.1&&b<0.1' 'b>0.9&&r<0.1&&g<0.1')
((green > 10000 && blue > 10000)) || { echo "missing underlying floats: $green $blue"; exit 1; }
printf R >&8
printf R >&9
await_lines "$UMBRIEL_RUNTIME_DIR/left.log" 'repainted ' 1
await_lines "$UMBRIEL_RUNTIME_DIR/right.log" 'repainted ' 1
"$UMBRIEL" settle > /dev/null
assert_hidden
red=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r>0.9&&g<0.1&&b<0.1')
((red == green + blue)) || { echo "underlying floats stopped updating: $red != $green + $blue"; exit 1; }
"$UMBRIEL" msg "window-move-by:20/20/$id" > /dev/null
"$UMBRIEL" settle > /dev/null
assert_hidden
before=$(geometry)
set_capture false
"$UMBRIEL" settle > /dev/null
[[ $(geometry) == "$before" ]]
grim "$IMAGE"
visible=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r>0.9&&b>0.9&&g<0.1')
((visible > 10000)) || { echo 'unprotect did not restore floating window'; exit 1; }
set_capture true
# These windows float because of layout policy, not explicit floating intent.
# Changing the workspace layout must preserve exclusion even as they become tiles.
for layout in scrolling floating; do
  "$UMBRIEL" msg "workspace-set-layout:$layout" > /dev/null
  "$UMBRIEL" settle > /dev/null
  expected_floating=false
  if [[ $layout == floating ]]; then expected_floating=true; fi
  [[ $(window protected | jq -r .floating) == "$expected_floating" ]]
  assert_hidden
done
echo 'floating layout exclusion, covered client updates, movement, unchanged geometry, and layout transitions verified'
