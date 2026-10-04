#!/usr/bin/env bash
# The tab bar is the compositor's: a click selects the tab under it, the wheel steps through the tabs, a middle click
# closes a tab when tabs are set to, and dragging a tab out of the bar opens it in its own column. New columns take
# the configured display, so these windows open tabbed without an action.
set -euo pipefail

readonly BTN_LEFT=272
readonly BTN_MIDDLE=274
readonly OUTPUT_W=1280
readonly OUTPUT_H=720
readonly BORDER=2
readonly GAP=8
readonly BAR=24
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"

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

# The centre of tab slot `$1` among `$2` equal slots: the bar spans the border's outer width and ends one gap above
# the tabs' border. tab_gap is 0 here, so the slots split the bar evenly.
slot_center() {
  local index=$1 count=$2 windows
  windows=$("$UMBRIEL" windows --json)
  jq -r --argjson index "$index" --argjson count "$count" --argjson border "$BORDER" --argjson gap "$GAP" \
    --argjson bar "$BAR" '
    map(select(.tabbed and (.tab_hidden | not)))[0]
    | (.x - $border) as $left | (.w + 2 * $border) as $width
    | "\($left + ($width * (2 * $index + 1) / (2 * $count) | floor)) \(.y - $border - $gap - ($bar / 2 | floor))"
  ' <<< "$windows"
}

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[layout.tabs]
default_display = "tabbed"
middle_click_closes = true

[appearance.tab_bar]
tab_gap = 0

[[window_rule]]
match.title = "^tab-"
default_scrolling_column = "tabs"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn_client tab-a
wait_for_windows 1
spawn_client tab-b
wait_for_windows 2
spawn_client tab-c
wait_for_windows 3
"$UMBRIEL" settle
wait_for_shown tab-c

read -r x y <<< "$(slot_center 0 3)"
pointer move "$x" "$y" click "$BTN_LEFT"
"$UMBRIEL" settle
wait_for_shown tab-a

pointer move "$x" "$y" notch 1
"$UMBRIEL" settle
wait_for_shown tab-b

read -r x y <<< "$(slot_center 2 3)"
pointer move "$x" "$y" click "$BTN_MIDDLE"
wait_for_windows 2
"$UMBRIEL" settle
if [[ $("$UMBRIEL" windows --json | jq '[.[] | select(.title == "tab-c")] | length') != 0 ]]; then
  echo "expected a middle click on its slot to close tab-c: $("$UMBRIEL" windows --json)"
  exit 1
fi

# Drag tab-b, now the second of two, out past the right end of the strip.
read -r x y <<< "$(slot_center 1 2)"
pointer move "$x" "$y" press "$BTN_LEFT" move "$((x + 40))" "$((y + 200))" move 1270 360 release "$BTN_LEFT"
"$UMBRIEL" settle
windows=$("$UMBRIEL" windows --json)
if ! jq -e '
  [.[] | select(.title == "tab-a")][0] as $a
  | [.[] | select(.title == "tab-b")][0] as $b
  | $a.x != $b.x and ($a.tab_hidden | not) and ($b.tab_hidden | not)
' <<< "$windows" > /dev/null; then
  echo "expected dragging tab-b out of the bar to open it in its own column: $windows"
  exit 1
fi

# Build adjacent groups in the first column, then drop the detached tab onto the lower group's leading slot.
spawn_client tab-d
wait_for_windows 3
spawn_client tab-e
wait_for_windows 4
"$UMBRIEL" msg window-move-down > /dev/null
"$UMBRIEL" msg column-toggle-tabbed > /dev/null
"$UMBRIEL" msg column-focus-last > /dev/null
"$UMBRIEL" settle
read -r sx sy <<< "$("$UMBRIEL" windows --json | jq -r '
  .[] | select(.title == "tab-b")
  | "\(.x + (.w / 2 | floor)) \(.y - 2 - 8 - 12)"
')"
source "$UMBRIEL_HARNESS_LIB"
"$UMBRIEL" clock-freeze
pointer_hold "$OUTPUT_W" "$OUTPUT_H" move "$sx" "$sy" press "$BTN_LEFT" \
  move "$((sx + 40))" "$((sy + 200))" -- release "$BTN_LEFT"
"$UMBRIEL" clock-advance 5000
read -r tx ty <<< "$("$UMBRIEL" windows --json | jq -r '
  .[] | select(.title == "tab-e")
  | "\(.x + (.w / 4 | floor)) \(.y - 2 - 8 - 12)"
')"
pointer move "$tx" "$ty"
pointer_release
"$UMBRIEL" clock-advance 5000
windows=$("$UMBRIEL" windows --json)
if ! jq -e '
  [.[] | select(.title == "tab-b")][0] as $dropped
  | [.[] | select(.title == "tab-e")][0] as $lower
  | [.[] | select(.title == "tab-a")][0] as $upper
  | $dropped.tabbed and $dropped.tab_index == 0 and $dropped.active
    and $lower.tab_index == 1 and $lower.tab_hidden
    and [$dropped.x, $dropped.y, $dropped.w, $dropped.h] == [$lower.x, $lower.y, $lower.w, $lower.h]
    and $dropped.y != $upper.y
' <<< "$windows" > /dev/null; then
  echo "a drop on the lower group's leading slot joined the wrong group: $windows"
  exit 1
fi

echo "the tab bar selected, scrolled, closed, and released tabs under the pointer"
