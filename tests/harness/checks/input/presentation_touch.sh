#!/usr/bin/env bash
# C0 G4: injected wlr_touch device, real Cursor routing and wl_touch delivery.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"
readonly LOG="$UMBRIEL_RUNTIME_DIR/presentation-touch.log"
readonly SHOTS="$UMBRIEL_RUNTIME_DIR/presentation-touch"
mkdir -p "$SHOTS"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"

[animation.windows_in]
duration_ms = 1600
curve = "linear"
style = "fade"

[animation.windows_move]
enabled = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
"$UMBRIEL" presentation-touch-probe create
"$UMBRIEL_SEAT_LOG_CLIENT" presentation-touch > "$LOG" 2>&1 &
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 400
# Client roundtrips/map precede its first injected touch.
read -r tx ty < <("$UMBRIEL" windows --json | jq -r '.[0] | "\((.x + .w / 2) / 640) \((.y + .h / 2) / 360)"')
record() {
  grim "$SHOTS/$1.png"
  "$UMBRIEL" presentation-scene-probe status --json > "$SHOTS/$1.json"
  "$UMBRIEL" presentation-input-probe status --json > "$SHOTS/$1-input.json"
}
touch_event() { "$UMBRIEL" presentation-touch-probe "$*"; }
assert_count() { [[ $(events "$LOG" "$1") == "$2" ]]; }
assert_pending() { "$UMBRIEL" presentation-input-probe status --json | jq -e "$1" > /dev/null; }
"$UMBRIEL" presentation-scene-probe arm
record displaced
touch_event down 11 "$tx" "$ty"
record dismissed
jq -e '(.active | not) and .fallback == "input_dismissal" and .reserved_bytes == 0' "$SHOTS/dismissed.json" > /dev/null
assert_pending '.pending'
touch_event motion 11 "$tx" "$ty"
touch_event down 12 "$tx" "$ty"
touch_event up 11
assert_pending '.pending'
touch_event motion 12 "$tx" "$ty"
touch_event up 12
assert_pending '(.pending | not)'
assert_count touch-down 0
assert_count touch-up 0
assert_count touch-motion 0
record released
# The following sequence reaches the client exactly once; it is also the
# negative control for accidental loss of every injected touch event.
touch_event down 21 "$tx" "$ty"
touch_event motion 21 "$tx" "$ty"
touch_event up 21
await_events "$LOG" 'touch-up id=21' 1
assert_count touch-down 1
assert_count touch-up 1
assert_count touch-motion 1
record activated
# Cancellation and removal clear whole device sequences, including contacts
# which arrived after dismissal. No consumed contact leaks to a wl_touch client.
"$UMBRIEL" presentation-input-probe arm
touch_event down 31 "$tx" "$ty"
touch_event down 32 "$tx" "$ty"
touch_event cancel 31
assert_pending '(.pending | not)'
assert_count touch-cancel 0
"$UMBRIEL" presentation-input-probe arm
touch_event down 41 "$tx" "$ty"
touch_event destroy
assert_pending '(.pending | not)'
assert_count touch-down 1
assert_count touch-up 1
cp "$LOG" "$SHOTS/client.log"
if [[ -n ${UMBRIEL_PRESENTATION_ARTIFACTS:-} ]]; then
  mkdir -p "$UMBRIEL_PRESENTATION_ARTIFACTS/touch"
  cp "$SHOTS/"* "$UMBRIEL_PRESENTATION_ARTIFACTS/touch/"
fi
echo "C0 displaced touch dismissal consumes the whole contact sequence, then delivers one native activation; cancel and removal clear ownership"
