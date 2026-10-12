#!/usr/bin/env bash
# A bar-style layer surface with no keyboard interactivity switches to on-demand and opens a grabbing xdg_popup on a
# click. The popup's grab must bring the keyboard to that layer, without dismissing the popup, so Escape reaches the
# menu. wlroots never moves focus for an xdg_popup grab itself.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

readonly OUTPUT_W=1280
readonly OUTPUT_H=720
readonly LEFT_BUTTON=272
readonly ESCAPE=1
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"
readonly OBSERVER="${UMBRIEL_SEAT_LOG_CLIENT:-./build-debug/tests/seat-log-client}"
readonly LAYER_CLIENT="${UMBRIEL_LAYER_CLIENT:-./build-debug/tests/layer-client}"
readonly WINDOW_LOG="$UMBRIEL_RUNTIME_DIR/layer-popup-window.log"
readonly PANEL_LOG="$UMBRIEL_RUNTIME_DIR/layer-popup-panel.log"
readonly KEYBOARD_LOG="$UMBRIEL_RUNTIME_DIR/layer-popup-keyboard.log"

if [[ ! -x $POINTER || ! -x $OBSERVER || ! -x $LAYER_CLIENT ]]; then
  echo "layer popup focus helpers are not available"
  exit 1
fi

refuse_events() {
  local file=$1 event=$2 limit=$3 label=$4
  local seen
  seen=$(events "$file" "$event")
  if ((seen > limit)); then
    echo "$label received $seen '$event', expected at most $limit: $(tr '\n' '|' < "$file")"
    exit 1
  fi
}

# The headless seat has no keyboard until a virtual one appears, and without one no client ever receives a keyboard
# enter. This connection owns that keyboard for the rest of the check.
"$POINTER" "$OUTPUT_W" "$OUTPUT_H" mod none pause 60000 > "$KEYBOARD_LOG" 2>&1 &

"$OBSERVER" layer-popup-window > "$WINDOW_LOG" 2>&1 &
await_events "$WINDOW_LOG" keyboard-enter 1 "the window"

"$LAYER_CLIENT" HEADLESS-1 40 popup-on-click > "$PANEL_LOG" 2>&1 &
await_events "$PANEL_LOG" ready 1 "the bar"
refuse_events "$PANEL_LOG" keyboard-enter 0 "the bar"

# Click the bar to open its menu, then hold until the assertions below are done.
pointer_hold "$OUTPUT_W" "$OUTPUT_H" move $((OUTPUT_W / 2)) 20 click "$LEFT_BUTTON" -- tap "$ESCAPE"
await_events "$PANEL_LOG" popup-mapped 1 "the bar"

# The transition under test: the grabbing popup brings the keyboard to its layer and takes it from the window.
await_events "$PANEL_LOG" keyboard-enter 1 "the bar"
await_events "$WINDOW_LOG" keyboard-leave 1 "the window"
refuse_events "$PANEL_LOG" popup-done 0 "the bar"

pointer_release
await_events "$PANEL_LOG" "keyboard-key code=$ESCAPE state=1" 1 "the bar"
refuse_events "$PANEL_LOG" popup-done 0 "the bar"

echo "a grabbing popup on an on-demand layer surface takes the keyboard and receives Escape"
