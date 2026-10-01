#!/usr/bin/env bash
# C0 input-only probe: real Wayland delivery, without a warped scene renderer.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

readonly LEFT_BUTTON=272
readonly OBSERVER="${UMBRIEL_SEAT_LOG_CLIENT:-./build-debug/tests/seat-log-client}"
readonly LOG_A="$UMBRIEL_RUNTIME_DIR/presentation-probe-a.log"
readonly LOG_B="$UMBRIEL_RUNTIME_DIR/presentation-probe-b.log"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[layout]
mode = "dwindle"

[animation]
duration_ms = 1
curve = "linear"

[input.focus]
follows_mouse = true
EOF
"$UMBRIEL" msg config-reload > /dev/null

"$OBSERVER" presentation-probe-a > "$LOG_A" 2>&1 &
for _ in $(seq 60); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.1
done
"$OBSERVER" presentation-probe-b > "$LOG_B" 2>&1 &
for _ in $(seq 60); do
  [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
  sleep 0.1
done
"$UMBRIEL" settle

windows=$("$UMBRIEL" windows --json)
read -r ax ay < <(jq -r '.[] | select(.title == "presentation-probe-a") | "\(.x + .w / 2 | floor) \(.y + .h / 2 | floor)"' <<< "$windows")
read -r bx by < <(jq -r '.[] | select(.title == "presentation-probe-b") | "\(.x + .w / 2 | floor) \(.y + .h / 2 | floor)"' <<< "$windows")

assert_active() {
  local expected=$1
  for _ in $(seq 60); do
    [[ $("$UMBRIEL" windows --json | jq -r '.[] | select(.active) | .title') == "$expected" ]] && return
    sleep 0.025
  done
  echo "expected focus $expected: $("$UMBRIEL" windows --json)"
  exit 1
}

assert_status() {
  "$UMBRIEL" presentation-input-probe status --json | jq -e "$1" > /dev/null
}

assert_clicks() {
  local expected=$1
  [[ $(events "$LOG_B" "pointer-button code=$LEFT_BUTTON state=pressed") == "$expected" ]]
  [[ $(events "$LOG_B" "pointer-button code=$LEFT_BUTTON state=released") == "$expected" ]]
}

# Keep one physical device connection for every paired event. The initial pause
# lets clients bind wl_pointer after the virtual device advertises capability.
pointer_hold 1280 720 move "$ax" "$ay" pause 300 -- \
  move "$bx" "$by" mark displaced hold \
  press "$LEFT_BUTTON" mark dismissed hold \
  release "$LEFT_BUTTON" mark released hold \
  press "$LEFT_BUTTON" release "$LEFT_BUTTON" mark activated hold \
  press "$LEFT_BUTTON" release "$LEFT_BUTTON" mark control hold
assert_active presentation-probe-a
"$UMBRIEL" presentation-input-probe arm > /dev/null
pointer_step displaced
assert_status '.active and (.pending | not)'
assert_active presentation-probe-a
assert_clicks 0

pointer_step dismissed
assert_status '(.active | not) and .pending'
assert_active presentation-probe-a
assert_clicks 0

# Hover/cursor focus is refreshed as soon as the complete dismissed sequence
# ends; the client gets an enter without needing an extra movement or click.
enters_before=$(events "$LOG_B" pointer-enter)
pointer_step released
assert_status '(.active | not) and (.pending | not)'
assert_active presentation-probe-b
await_events "$LOG_B" pointer-enter "$((enters_before + 1))"
assert_clicks 0

pointer_step activated
await_events "$LOG_B" "pointer-button code=$LEFT_BUTTON state=released" 1
assert_clicks 1

# Negative control: identical complete sequence with the guard disabled must
# reach the client. A fixture that merely loses all pointer input fails here.
"$UMBRIEL" presentation-input-probe cancel > /dev/null
pointer_step control
await_events "$LOG_B" "pointer-button code=$LEFT_BUTTON state=released" 2
assert_clicks 2
pointer_release
assert_status '(.active | not) and (.pending | not)'

# Explicit cancellation also restores hover without requiring a dismissal.
"$UMBRIEL_POINTER_CLIENT" 1280 720 move "$ax" "$ay"
assert_active presentation-probe-a
"$UMBRIEL" presentation-input-probe arm > /dev/null
"$UMBRIEL_POINTER_CLIENT" 1280 720 move "$bx" "$by"
assert_active presentation-probe-a
"$UMBRIEL" presentation-input-probe cancel > /dev/null
assert_active presentation-probe-b

echo "C0 pointer probe consumes dismissal, restores hover, and delivers subsequent clicks exactly once"
