#!/usr/bin/env bash
# A floating protected window exposes live tiles in capture
# across each tiling layout; toggles preserve allocation and revoke window sources.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
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
match.title = "^protected$"
block_capture = false
default_floating = true
default_floating_size_px = { width = 1280, height = 720 }
default_position = { x = 0, y = 0, anchor = "top_left" }
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
set_capture() {
  sed -i "s/^block_capture = .*/block_capture = $1/" "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
}
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/capture.png"
window() { "$UMBRIEL" windows --json | jq -ce --arg title "$1" '.[] | select(.title == $title)'; }
wait_window() {
  for _ in $(seq 100); do
    window "$1" > /dev/null && return
    sleep 0.02
  done
  echo "missing window: $1"; return 1
}
mkfifo "$UMBRIEL_RUNTIME_DIR/left" "$UMBRIEL_RUNTIME_DIR/right"
exec 8<> "$UMBRIEL_RUNTIME_DIR/left"
exec 9<> "$UMBRIEL_RUNTIME_DIR/right"
REPAINT_ON_STDIN=1 FOLLOW_CONFIGURES=1 FILL_COLOR=0xFF00FF00 \
  "$UMBRIEL_UNMAP_CLIENT" left 300 300 <&8 > "$UMBRIEL_RUNTIME_DIR/left.log" 2>&1 &
wait_window left
REPAINT_ON_STDIN=1 FOLLOW_CONFIGURES=1 FILL_COLOR=0xFF0000FF \
  "$UMBRIEL_UNMAP_CLIENT" right 300 300 <&9 > "$UMBRIEL_RUNTIME_DIR/right.log" 2>&1 &
wait_window right
OPAQUE_BUFFER=1 FILL_COLOR=0xFFFF00FF FOLLOW_CONFIGURES=1 \
  "$UMBRIEL_UNMAP_CLIENT" protected 1280 720 > "$UMBRIEL_RUNTIME_DIR/protected.log" 2>&1 &
wait_window protected
id=$(window protected | jq -r .id)
"$UMBRIEL" settle > /dev/null
[[ $(window protected | jq -c '[.x,.y,.w,.h]') == '[0,0,1280,720]' ]]

# Two active sessions share the owned source. Protection must stop both, and a
# new request must receive a valid, stopped session rather than a protocol error.
for n in 1 2; do
  "$UMBRIEL_CAPTURE_CLIENT" --window "$id" > "$UMBRIEL_RUNTIME_DIR/window-$n.log" 2>&1 &
  await_lines "$UMBRIEL_RUNTIME_DIR/window-$n.log" 'session-ready ' 1
done
set_capture true
for n in 1 2; do await_lines "$UMBRIEL_RUNTIME_DIR/window-$n.log" 'session-stopped ' 1; done
"$UMBRIEL_CAPTURE_CLIENT" --window "$id" > "$UMBRIEL_RUNTIME_DIR/denied.log" 2>&1 &
await_lines "$UMBRIEL_RUNTIME_DIR/denied.log" 'session-stopped ' 1
! rg '^frame |protocol error|invalid object' "$UMBRIEL_RUNTIME_DIR/denied.log"
"$UMBRIEL_CAPTURE_CLIENT" --output HEADLESS-1 > "$UMBRIEL_RUNTIME_DIR/output.log" 2>&1 &
await_lines "$UMBRIEL_RUNTIME_DIR/output.log" 'frame ' 1

repaints=0
assert_hidden() {
  grim "$IMAGE"
  local count
  count=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r>0.9&&b>0.9&&g<0.1')
  [[ $count == 0 ]] || { echo "protected pixels leaked: $count"; exit 1; }
}
for layout in scrolling dwindle master; do
  "$UMBRIEL" msg workspace-set-layout "$layout" > /dev/null
  "$UMBRIEL" settle > /dev/null
  assert_hidden
  # A large central rectangle is covered by the local float. Capture must show
  # the two tiled colours there, not a black replacement or a stale image.
  read -r green blue < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'g>0.9&&r<0.1&&b<0.1' 'b>0.9&&r<0.1&&g<0.1' '900x500+150+100')
  ((green > 10000 && blue > 10000)) || { echo "$layout missing underlying tiles: $green $blue"; exit 1; }
  printf R >&8
  printf R >&9
  repaints=$((repaints + 1))
  await_lines "$UMBRIEL_RUNTIME_DIR/left.log" 'repainted ' "$repaints"
  await_lines "$UMBRIEL_RUNTIME_DIR/right.log" 'repainted ' "$repaints"
  "$UMBRIEL" settle > /dev/null
  assert_hidden
  red=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r>0.9&&g<0.1&&b<0.1' '900x500+150+100')
  ((red == green + blue)) || { echo "$layout did not update both covered tiles: $red != $green + $blue"; exit 1; }
  "$UMBRIEL" msg window-move-by 20 20 --window "$id" > /dev/null
  "$UMBRIEL" settle > /dev/null
  assert_hidden
  "$UMBRIEL" msg window-toggle-floating "$id" > /dev/null
  "$UMBRIEL" settle > /dev/null
  before=$(window protected | jq -c '[.x,.y,.w,.h,.floating]')
  assert_hidden
  set_capture false
  "$UMBRIEL" settle > /dev/null
  [[ $(window protected | jq -c '[.x,.y,.w,.h,.floating]') == "$before" ]]
  grim "$IMAGE"
  visible=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r>0.9&&b>0.9&&g<0.1')
  ((visible > 10000)) || { echo "unprotect did not restore window"; exit 1; }
  set_capture true
  "$UMBRIEL" msg window-toggle-floating "$id" > /dev/null
  printf G >&8
  printf B >&9
  repaints=$((repaints + 1))
  await_lines "$UMBRIEL_RUNTIME_DIR/left.log" 'repainted ' "$repaints"
  await_lines "$UMBRIEL_RUNTIME_DIR/right.log" 'repainted ' "$repaints"
done
set_capture false
"$UMBRIEL_CAPTURE_CLIENT" --window "$id" > "$UMBRIEL_RUNTIME_DIR/fresh.log" 2>&1 &
await_lines "$UMBRIEL_RUNTIME_DIR/fresh.log" 'frame ' 1
echo 'invisible floating and tiled exclusion, live underlying updates, movement, and source revocation verified'
