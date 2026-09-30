#!/usr/bin/env bash
# A dismissed presentation cannot release input merely because contacts ended:
# the ordinary output buffer must also commit successfully.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"
readonly LOG="$UMBRIEL_RUNTIME_DIR/presentation-commit.log"
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
"$UMBRIEL_SEAT_LOG_CLIENT" presentation-commit > "$LOG" 2>&1 &
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 400
read -r tx ty < <("$UMBRIEL" windows --json | jq -r '.[0] | "\((.x + .w / 2) / 640) \((.y + .h / 2) / 360)"')
"$UMBRIEL" presentation-scene-probe arm
# Establish that the displaced buffer was actually submitted before holding.
grim "$UMBRIEL_RUNTIME_DIR/displaced.png"
"$UMBRIEL" output-commit-hold 'HEADLESS-1 on'
touch_event() { "$UMBRIEL" presentation-touch-probe "$*"; }
touch_event down 1 "$tx" "$ty"
touch_event up 1
# No capture/frame/settle barrier between the two complete sequences. Native
# geometry is still invisible because every attempted replacement commit fails.
touch_event down 2 "$tx" "$ty"
touch_event motion 2 "$tx" "$ty"
touch_event up 2
"$UMBRIEL" presentation-input-probe status --json | jq -e '.restore_pending and (.pending | not)' > /dev/null
[[ $(events "$LOG" touch-down) == 0 && $(events "$LOG" touch-up) == 0 ]]
for _ in $(seq 80); do
  [[ $("$UMBRIEL" effect-frames --json | jq '.outputs[0].rejected_buffer_commits') -gt 0 ]] && break
  sleep 0.025
done
"$UMBRIEL" effect-frames --json | jq -e '.outputs[0].commit_held and .outputs[0].rejected_buffer_commits > 0' > /dev/null
"$UMBRIEL" output-commit-hold 'HEADLESS-1 off'
for _ in $(seq 80); do
  [[ $("$UMBRIEL" presentation-input-probe status --json | jq '.restore_pending') == false ]] && break
  sleep 0.025
done
"$UMBRIEL" presentation-input-probe status --json | jq -e '(.restore_pending | not) and (.pending | not)' > /dev/null
touch_event down 3 "$tx" "$ty"
touch_event motion 3 "$tx" "$ty"
touch_event up 3
await_events "$LOG" 'touch-up id=3' 1
[[ $(events "$LOG" touch-down) == 1 && $(events "$LOG" touch-up) == 1 && $(events "$LOG" touch-motion) == 1 ]]
touch_event destroy
echo "Failed native commits retain dismissal ownership across completed sequences; successful ordinary commit permits one activation"
