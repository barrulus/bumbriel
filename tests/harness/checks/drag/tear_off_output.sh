#!/usr/bin/env bash
# harness: outputs=2
# Browser tab tear-off starts as a data-device drag, then maps another toplevel from the same client with an input
# activation token. The token records the source workspace, but a drop on another output must place the detached
# window on that output instead of beside its source.
set -euo pipefail

readonly BTN_LEFT=272
readonly OUTPUT_LAYOUT_W=2560
readonly OUTPUT_LAYOUT_H=720
readonly CLIENT="${UMBRIEL_DRAG_CLIENT:-./build-debug/tests/drag-client}"
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"
readonly CLIENT_LOG="$UMBRIEL_RUNTIME_DIR/tear-off-client.log"
readonly POINTER_LOG="$UMBRIEL_RUNTIME_DIR/tear-off-pointer.log"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false

[[window_rule]]
match.title = "^tear-off-source$"
default_output = "HEADLESS-1"
EOF
"$UMBRIEL" msg config-reload > /dev/null

output_origin() {
  "$UMBRIEL" outputs |
    awk -v name="$1" '$1 == name { found = 1; next } found && /Position:/ { split($2, p, ","); print p[1], p[2]; exit }'
}

window_json() {
  "$UMBRIEL" windows --json | jq -c --arg title "$1" '.[] | select(.title == $title)'
}

"$CLIENT" tear-off > "$CLIENT_LOG" 2>&1 &

source_window=
for _ in $(seq 60); do
  source_window=$(window_json tear-off-source)
  [[ -n $source_window ]] && grep -q '^ready$' "$CLIENT_LOG" && break
  sleep 0.1
done
if [[ -z $source_window ]] || ! grep -q '^ready$' "$CLIENT_LOG"; then
  echo "tear-off source did not map: $(< "$CLIENT_LOG")"
  exit 1
fi

source_workspace=$(jq -r '.workspace' <<< "$source_window")
if [[ $source_workspace != HEADLESS-1:* ]]; then
  echo "tear-off source mapped on the wrong output: $source_window"
  exit 1
fi

source_x=$(jq -r '.x' <<< "$source_window")
source_y=$(jq -r '.y' <<< "$source_window")
source_w=$(jq -r '.w' <<< "$source_window")
source_h=$(jq -r '.h' <<< "$source_window")
read -r target_x target_y < <(output_origin HEADLESS-2)
if [[ -z ${target_x:-} || -z ${target_y:-} ]]; then
  echo "could not resolve the target output origin"
  exit 1
fi

"$POINTER" "$OUTPUT_LAYOUT_W" "$OUTPUT_LAYOUT_H" \
  move "$((source_x + source_w / 2))" "$((source_y + source_h / 2))" pause 300 \
  press "$BTN_LEFT" pause 300 \
  move "$((target_x + 640))" "$((target_y + 360))" pause 300 \
  release "$BTN_LEFT" > "$POINTER_LOG" 2>&1 || {
  echo "pointer client failed: $(< "$POINTER_LOG")"
  exit 1
}

detached=
for _ in $(seq 60); do
  detached=$(window_json tear-off-window)
  [[ -n $detached ]] && grep -q '^tear-off-mapped$' "$CLIENT_LOG" && break
  sleep 0.1
done
if [[ -z $detached ]] || ! grep -q '^tear-off-mapped$' "$CLIENT_LOG"; then
  echo "detached window did not map: $(< "$CLIENT_LOG")"
  exit 1
fi

workspace=$(jq -r '.workspace' <<< "$detached")
if [[ $workspace != HEADLESS-2:* ]]; then
  echo "detached window returned to its source instead of the drop output: $detached"
  exit 1
fi

echo "pointer-drag tear-off mapped its new toplevel on the drop output"
