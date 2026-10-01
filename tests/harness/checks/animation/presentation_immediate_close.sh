#!/usr/bin/env bash
# Third-window admission followed by immediate exit must preserve the new
# native close snapshot, then reject presentation through another opening.
set -euo pipefail
readonly SHOTS="$UMBRIEL_RUNTIME_DIR/presentation-immediate-close"
mkdir -p "$SHOTS"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"

[layout]
mode = "master"

[animation.windows_in]
duration_ms = 1600
curve = "linear"
style = "fade"

[animation.windows_out]
duration_ms = 800
curve = "linear"
style = "fade"

[animation.windows_move]
duration_ms = 2400
curve = "linear"
CONFIG
if [[ ${UMBRIEL_WINDOW_SCENE_PRODUCTION:-0} == 1 ]]; then
  cat >> "$UMBRIEL_CONFIG" <<CONFIG

[include]
files = ["$UMBRIEL_REPO/examples/effects/scene/water/effect.toml"]
CONFIG
  sed -i '/^\[animation.windows_in\]$/a effect = "water"' "$UMBRIEL_CONFIG"
  sed -i '/^\[animation.windows_out\]$/a effect = "water"' "$UMBRIEL_CONFIG"
fi
production_state() {
  "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .window_presentation | .reserved_bytes=.memory_bytes'
}
probe_state() {
  if [[ ${UMBRIEL_WINDOW_SCENE_PRODUCTION:-0} == 1 ]]; then
    production_state
  else
    "$UMBRIEL" presentation-scene-probe status --json
  fi
}
arm() {
  if [[ ${UMBRIEL_WINDOW_SCENE_PRODUCTION:-0} == 1 ]]; then
    probe_state | jq -e 'select(.active)'
  else
    "$UMBRIEL" presentation-scene-probe arm --json
  fi
}
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
spawn() {
  FILL_COLOR="$2" "$UMBRIEL_UNMAP_CLIENT" "$1" 600 320 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  for _ in $(seq 80); do
    [[ $("$UMBRIEL" windows --json | jq --arg title "$1" 'any(.[]; .title == $title)') == true ]] && return
    sleep 0.025
  done
  exit 1
}
record() {
  grim "$SHOTS/$1.png"
  probe_state > "$SHOTS/$1.json"
}
spawn immediate-neighbour-a 0xFFFF0000
"$UMBRIEL" clock-advance 3000
spawn immediate-neighbour-b 0xFFFF00FF
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
spawn immediate-trigger 0xFF0000FF
"$UMBRIEL" clock-advance 100
arm
record displaced-opening
id=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "immediate-trigger") | .id')
"$UMBRIEL" msg "window-close:$id" > /dev/null
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq 'any(.[]; .title == "immediate-trigger")') == false ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 1
record immediate-exit
jq -e --slurpfile before "$SHOTS/displaced-opening.json" '
  (.active | not) and .overlap and .reserved_bytes == 0 and .fallback == "overlapping_lifecycle" and
  (.native | length) == 1 and .native[0].snapshot > 0 and .native[0].identity != $before[0].identity and
  (.native[0].deadline_msec - .native[0].start_msec == 800)
' "$SHOTS/immediate-exit.json" > /dev/null
spawn immediate-next 0xFF00FF00
"$UMBRIEL" clock-advance 100
record next-opening
jq -e --slurpfile close "$SHOTS/immediate-exit.json" '
  (.active | not) and .overlap and
  any(.native[]; .snapshot == $close[0].native[0].snapshot and .identity == $close[0].native[0].identity and .deadline_msec == $close[0].native[0].deadline_msec)
' "$SHOTS/next-opening.json" > /dev/null
if arm > /dev/null 2>&1; then
  echo "immediate-close burst incorrectly reacquired presentation"
  exit 1
fi
"$UMBRIEL" clock-advance 700
record original-close-deadline
jq -e --slurpfile close "$SHOTS/immediate-exit.json" '
  .overlap and (.native | length) == 1 and all(.native[]; .snapshot != $close[0].native[0].snapshot)
' "$SHOTS/original-close-deadline.json" > /dev/null
"$UMBRIEL" clock-advance 1700
"$UMBRIEL" settle
record settled
jq -e '(.active | not) and (.overlap | not) and (.native | length) == 0 and .reserved_bytes == 0' "$SHOTS/settled.json" > /dev/null
if [[ -n ${UMBRIEL_PRESENTATION_ARTIFACTS:-} ]]; then
  mkdir -p "$UMBRIEL_PRESENTATION_ARTIFACTS/immediate-close"
  cp "$SHOTS/"* "$UMBRIEL_PRESENTATION_ARTIFACTS/immediate-close/"
fi
echo "Third-window immediate exit preserved native close snapshot and deadline through another opening without queued replay"
