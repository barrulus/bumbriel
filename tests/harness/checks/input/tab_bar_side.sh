#!/usr/bin/env bash
# A bar on the left edge stacks its slots down the column and reserves its width beside the tabs, and a click selects
# the slot under it, counted down the bar.
set -euo pipefail

readonly BTN_LEFT=272
readonly OUTPUT_W=1280
readonly OUTPUT_H=720
readonly BORDER=2
readonly GAP=8
readonly BAR=120
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"

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

wait_for_shown() {
  local shown=$1 windows=
  for _ in $(seq 50); do
    windows=$("$UMBRIEL" windows --json)
    if jq -e --arg shown "$shown" '
      map(select(.title == $shown and .tabbed and (.tab_hidden | not) and .active)) | length == 1
    ' <<< "$windows" > /dev/null; then
      return 0
    fi
    sleep 0.1
  done
  echo "expected '$shown' to be the shown tab: $windows"
  return 1
}

# The centre of slot `$1` of `$2` in the bar left of the tabs: it spans the border's outer height and, with tab_gap 0,
# its slots split that evenly.
slot_center() {
  "$UMBRIEL" windows --json | jq -r --argjson slot "$1" --argjson count "$2" --argjson border "$BORDER" \
    --argjson gap "$GAP" --argjson bar "$BAR" '
    map(select(.tabbed and (.tab_hidden | not)))[0]
    | (.y - $border) as $top | (.h + 2 * $border) as $height
    | "\(.x - $border - $gap - ($bar / 2 | floor)) \($top + ($height * (2 * $slot + 1) / (2 * $count) | floor))"
  '
}

cat >> "$UMBRIEL_CONFIG" <<CONFIG

[layout.tabs]
default_display = "tabbed"

[appearance.tab_bar]
position = "left"
height = $BAR
tab_gap = 0

[[window_rule]]
match.title = "^tab-"
default_scrolling_column = "tabs"
CONFIG
"$UMBRIEL" msg config-reload > /dev/null

spawn_client tab-a
wait_for_windows 1
spawn_client tab-b
wait_for_windows 2
spawn_client tab-c
wait_for_windows 3
"$UMBRIEL" settle
wait_for_shown tab-c

# The tabs give up the bar and one gap beside them, and keep the column's full height.
windows=$("$UMBRIEL" windows --json)
if ! jq -e --argjson reserve "$((BAR + GAP))" '
  map(select(.title == "tab-c"))[0] | .h == 700 and .x >= $reserve
' <<< "$windows" > /dev/null; then
  echo "expected the tabs to sit beside a left bar at full height: $windows"
  exit 1
fi

read -r x y <<< "$(slot_center 0 3)"
"$POINTER" "$OUTPUT_W" "$OUTPUT_H" move "$x" "$y" click "$BTN_LEFT"
"$UMBRIEL" settle
wait_for_shown tab-a

read -r x y <<< "$(slot_center 1 3)"
"$POINTER" "$OUTPUT_W" "$OUTPUT_H" move "$x" "$y" click "$BTN_LEFT"
"$UMBRIEL" settle
wait_for_shown tab-b

echo "a left tab bar stacked its slots beside the tabs and mapped clicks down the bar"
