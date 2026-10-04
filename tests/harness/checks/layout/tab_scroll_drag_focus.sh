#!/usr/bin/env bash
# Panning the strip onto a tab group focuses the tab it shows, as a directional focus move does, not the group's
# first row. The modified middle-button drag settles through the same path as the three-finger swipe, which the
# headless backend cannot send.
set -euo pipefail

readonly OUTPUT_W=1280
readonly OUTPUT_H=720
readonly BTN_MIDDLE=274
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false

# One column fills most of the viewport, so a pan settles on a single column.
[layout.scrolling]
default_extent_fraction = 0.8

[keybinds]
"Mod+MouseMiddle" = "layout-scroll-drag"
EOF
"$UMBRIEL" msg config-reload > /dev/null

pointer() {
  "$POINTER" "$OUTPUT_W" "$OUTPUT_H" "$@"
}

spawn_client() {
  foot --title="$1" sh -c 'sleep 120' > /dev/null 2>&1 &
}

wait_for_windows() {
  local expected=$1 count=
  for _ in $(seq 60); do
    count=$("$UMBRIEL" windows --json | jq 'length')
    [[ $count == "$expected" ]] && return 0
    sleep 0.1
  done
  echo "expected $expected window(s), got $count"
  return 1
}

focused() {
  "$UMBRIEL" windows --json | jq -r '.[] | select(.focused) | .title'
}

wait_for_focus() {
  local expected=$1 actual=
  for _ in $(seq 50); do
    actual=$(focused)
    [[ $actual == "$expected" ]] && return 0
    sleep 0.1
  done
  echo "expected '$expected' to be focused, got '$actual': $("$UMBRIEL" windows --json)"
  return 1
}

# A and B tabbed in one column, B on show; C in a column of its own.
spawn_client tab-a
wait_for_windows 1
spawn_client tab-b
wait_for_windows 2
"$UMBRIEL" msg window-consume-left > /dev/null
"$UMBRIEL" msg column-toggle-tabbed > /dev/null
wait_for_focus tab-b
spawn_client tab-c
wait_for_windows 3
wait_for_focus tab-c
"$UMBRIEL" settle

# Pan back to the group: the drag moves the strip right, revealing the column on the left.
pointer move 300 360 mod logo press "$BTN_MIDDLE" move 350 360 move 1200 360 release "$BTN_MIDDLE" mod none
"$UMBRIEL" settle
wait_for_focus tab-b
if [[ $("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "tab-a") | .tab_hidden') != true ]]; then
  echo "expected tab-a to stay hidden behind tab-b: $("$UMBRIEL" windows --json)"
  exit 1
fi

echo "panning onto a tab group focused the tab it shows"
