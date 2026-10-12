#!/usr/bin/env bash
# A master layout whose stack emptied when the drag lifted its only window still accepts a drop into the stack on the
# stack's side, while the master's side keeps offering its rows. The overview drives the drag without a modifier key.
set -euo pipefail

readonly BTN_LEFT=272
readonly OUTPUT_W=1280
readonly OUTPUT_H=720
readonly OVERVIEW_ZOOM=0.5
readonly OVERVIEW_X=320
readonly OVERVIEW_Y=180
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"

spawn_client() {
  foot --title="master-stack-$1" sh -c 'sleep 120' > /dev/null 2>&1 &
}

wait_for_count() {
  for _ in $(seq 60); do
    [[ $("$UMBRIEL" windows --json | jq 'length') -eq $1 ]] && return 0
    sleep 0.25
  done
  echo "timed out waiting for $1 window(s)"
  return 1
}

# Drags the stack window in the overview and drops it at world coordinates ($1, $2). The detour over the master's top
# left corner makes every drop a real drag, however close it lands to the start.
drag_stack_to() {
  local windows start_x start_y
  windows=$("$UMBRIEL" windows --json)
  start_x=$(jq -r --argjson origin "$OVERVIEW_X" --argjson zoom "$OVERVIEW_ZOOM" \
    '.[] | select(.title == "master-stack-b") | ($origin + ((.x + .w / 2) * $zoom) | round)' <<< "$windows")
  start_y=$(jq -r --argjson origin "$OVERVIEW_Y" --argjson zoom "$OVERVIEW_ZOOM" \
    '.[] | select(.title == "master-stack-b") | ($origin + ((.y + .h / 2) * $zoom) | round)' <<< "$windows")
  "$UMBRIEL" msg overview-open > /dev/null
  "$UMBRIEL" settle
  "$POINTER" "$OUTPUT_W" "$OUTPUT_H" move "$start_x" "$start_y" press "$BTN_LEFT" \
    move $((OVERVIEW_X + 50)) $((OVERVIEW_Y + 50)) move $((OVERVIEW_X + $1 / 2)) $((OVERVIEW_Y + $2 / 2)) \
    release "$BTN_LEFT"
  "$UMBRIEL" settle
  "$UMBRIEL" msg overview-close > /dev/null
  "$UMBRIEL" settle
}

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[layout]
mode = "master"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn_client a
wait_for_count 1
spawn_client b
wait_for_count 2
"$UMBRIEL" settle

# Content is 1260x700 at (10, 10): master a is 686 wide, stack b starts at x 708 and is 562 wide.
side_by_side='
  first(.[] | select(.title == "master-stack-a")) as $a |
  first(.[] | select(.title == "master-stack-b")) as $b |
  $a.x == 10 and $a.w == 686 and $a.h == 700 and $b.x == 708 and $b.w == 562 and $b.h == 700
'
windows=$("$UMBRIEL" windows --json)
if ! jq -e "$side_by_side" <<< "$windows" > /dev/null; then
  echo "expected a master beside a stack before dragging: $windows"
  exit 1
fi

# Lifting b leaves a alone across the whole width; dropping inside a's area on the stack side reopens the stack.
drag_stack_to 1000 360
windows=$("$UMBRIEL" windows --json)
if ! jq -e "$side_by_side" <<< "$windows" > /dev/null; then
  echo "a drop on the stack side did not return b to the stack: $windows"
  exit 1
fi

# The master side still targets the master's rows: near its bottom edge, b lands below a.
drag_stack_to 300 680
windows=$("$UMBRIEL" windows --json)
if ! jq -e '
  first(.[] | select(.title == "master-stack-a")) as $a |
  first(.[] | select(.title == "master-stack-b")) as $b |
  $a.x == 10 and $b.x == 10 and $a.w == 1260 and $b.w == 1260 and $a.y < $b.y
' <<< "$windows" > /dev/null; then
  echo "a drop near the master's bottom edge did not stack b below a: $windows"
  exit 1
fi

echo "an emptied stack accepted a drop on its side while the master side kept its rows"
