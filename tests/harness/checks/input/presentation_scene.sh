#!/usr/bin/env bash
# C0 G4: real output-local displacement and real client delivery, borrowing the
# opener's native clock. Artifacts record each step of the dismissal sequence.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

readonly LEFT_BUTTON=272
readonly OBSERVER="${UMBRIEL_SEAT_LOG_CLIENT:-./build-debug/tests/seat-log-client}"
readonly LOG_A="$UMBRIEL_RUNTIME_DIR/presentation-scene-a.log"
readonly LOG_B="$UMBRIEL_RUNTIME_DIR/presentation-scene-b.log"
readonly SHOTS="$UMBRIEL_RUNTIME_DIR/presentation-scene"
mkdir -p "$SHOTS"

cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"

[layout]
mode = "dwindle"

[colors]
backdrop = "#112233FF"

[animation.windows_in]
duration_ms = 1600
curve = "linear"
style = "fade"

[animation.windows_move]
enabled = false

[input.focus]
follows_mouse = true
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze

spawn_observer() {
  LOG_OUTPUTS=1 "$OBSERVER" "$1" > "$2" 2>&1 &
  for _ in $(seq 80); do
    [[ $("$UMBRIEL" windows --json | jq length) == "$3" ]] && return
    sleep 0.025
  done
  echo "observer did not map: $1"
  exit 1
}
spawn_observer presentation-scene-a "$LOG_A" 1
"$UMBRIEL" clock-advance 2000
"$UMBRIEL" settle
spawn_observer presentation-scene-b "$LOG_B" 2
"$UMBRIEL" clock-advance 400

windows=$("$UMBRIEL" windows --json)
read -r ax ay < <(jq -r '.[] | select(.title == "presentation-scene-a") | "\(.x + .w / 2 | floor) \(.y + .h / 2 | floor)"' <<< "$windows")
read -r bx by < <(jq -r '.[] | select(.title == "presentation-scene-b") | "\(.x + .w / 2 | floor) \(.y + .h / 2 | floor)"' <<< "$windows")

assert_active() {
  for _ in $(seq 80); do
    [[ $("$UMBRIEL" windows --json | jq -r '.[] | select(.active) | .title') == "$1" ]] && return
    sleep 0.025
  done
  echo "wrong focus, wanted $1: $("$UMBRIEL" windows --json)"
  exit 1
}
assert_clicks() {
  [[ $(events "$LOG_B" "pointer-button code=$LEFT_BUTTON state=pressed") == "$1" ]]
  [[ $(events "$LOG_B" "pointer-button code=$LEFT_BUTTON state=released") == "$1" ]]
}
record() {
  grim "$SHOTS/$1.png"
  "$UMBRIEL" presentation-scene-probe status --json > "$SHOTS/$1.json"
  "$UMBRIEL" windows --json > "$SHOTS/$1-windows.json"
}

pointer_hold 640 360 move "$ax" "$ay" pause 300 -- \
  move "$bx" "$by" mark displaced hold \
  press "$LEFT_BUTTON" mark dismissed hold \
  release "$LEFT_BUTTON" mark released hold \
  press "$LEFT_BUTTON" release "$LEFT_BUTTON" mark activated hold \
  press "$LEFT_BUTTON" release "$LEFT_BUTTON" mark control hold
assert_active presentation-scene-a
await_events "$LOG_A" surface-output-enter 1
await_events "$LOG_B" surface-output-enter 1
record native
enters_a=$(events "$LOG_A" surface-output-enter)
enters_b=$(events "$LOG_B" surface-output-enter)
leaves_a=$(events "$LOG_A" surface-output-leave)
leaves_b=$(events "$LOG_B" surface-output-leave)

"$UMBRIEL" presentation-scene-probe arm --json > "$SHOTS/armed.json"
jq -e '.active and .reserved_bytes > 0 and (.native | length) == 1 and .identity == .native[0].identity and .deadline_msec == .native[0].deadline_msec' "$SHOTS/armed.json" > /dev/null
pointer_step displaced
record displaced
assert_active presentation-scene-a
assert_clicks 0
# The backing replaces a known opaque native backdrop corner. This must be an
# actual displaced render, not a guard-only fixture losing pointer events.
native_black=$("$UMBRIEL_PIXEL_PROBE" "$SHOTS/native.png" count 'r < 0.01 && g < 0.01 && b < 0.01' 8x8+2+2)
displaced_black=$("$UMBRIEL_PIXEL_PROBE" "$SHOTS/displaced.png" count 'r < 0.01 && g < 0.01 && b < 0.01' 8x8+2+2)
[[ $native_black == 0 && $displaced_black == 64 ]]

pointer_step dismissed
record dismissed
jq -e '(.active | not) and .fallback == "input_dismissal" and .reserved_bytes == 0' "$SHOTS/dismissed.json" > /dev/null
"$UMBRIEL" presentation-input-probe status --json | jq -e '.pending' > /dev/null
assert_active presentation-scene-a
assert_clicks 0
cmp "$SHOTS/native.png" "$SHOTS/dismissed.png"

enters_before=$(events "$LOG_B" pointer-enter)
pointer_step released
record released
assert_active presentation-scene-b
await_events "$LOG_B" pointer-enter "$((enters_before + 1))"
assert_clicks 0
pointer_step activated
record activated
await_events "$LOG_B" "pointer-button code=$LEFT_BUTTON state=released" 1
assert_clicks 1
pointer_step control
await_events "$LOG_B" "pointer-button code=$LEFT_BUTTON state=released" 2
assert_clicks 2
pointer_release

# Occlusion and renderer replacement cannot change client output membership.
[[ $(events "$LOG_A" surface-output-enter) == "$enters_a" ]]
[[ $(events "$LOG_B" surface-output-enter) == "$enters_b" ]]
[[ $(events "$LOG_A" surface-output-leave) == "$leaves_a" ]]
[[ $(events "$LOG_B" surface-output-leave) == "$leaves_b" ]]
# Dismissal must leave the original native lifecycle identity and deadline intact.
jq -e --slurpfile armed "$SHOTS/armed.json" '.native[0].identity == $armed[0].identity and .native[0].deadline_msec == $armed[0].deadline_msec' "$SHOTS/activated.json" > /dev/null
"$UMBRIEL" clock-advance 1600
"$UMBRIEL" settle

if [[ -n ${UMBRIEL_PRESENTATION_ARTIFACTS:-} ]]; then
  mkdir -p "$UMBRIEL_PRESENTATION_ARTIFACTS/input"
  cp "$SHOTS/"* "$UMBRIEL_PRESENTATION_ARTIFACTS/input/"
fi

echo "C0 displaced presentation consumed the first complete click, restored hover, preserved native membership/clock, and delivered later clicks once"
