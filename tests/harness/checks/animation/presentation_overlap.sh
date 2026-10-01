#!/usr/bin/env bash
# C0 G4: a real retained close snapshot owns the probe's identity/deadline.
# A later opening cancels replacement, preserves native obligations and latches
# fallback until the lifecycle burst ends (independent of neighbour reflow).
set -euo pipefail
readonly SHOTS="$UMBRIEL_RUNTIME_DIR/presentation-overlap"
mkdir -p "$SHOTS"

cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"

[layout]
mode = "master"

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

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
if [[ ${UMBRIEL_PRESENTATION_FLOATING:-0} == 1 ]]; then
  cat >> "$UMBRIEL_CONFIG" <<'FLOATING'

[[window_rule]]
match.title = "^overlap-"
default_floating = true
default_floating_size_px = { width = 240, height = 140 }
FLOATING
fi
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
  echo "client did not map: $1"
  exit 1
}
record() {
  grim "$SHOTS/$1.png"
  probe_state > "$SHOTS/$1.json"
}
wait_state() {
  for _ in $(seq 80); do
    if probe_state | jq -e "$1" > /dev/null; then
      return
    fi
    sleep 0.025
  done
  probe_state
  exit 1
}

spawn overlap-close 0xFF0000FF
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
spawn overlap-survivor 0xFFFF0000
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
spawn overlap-neighbour 0xFFFF00FF
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
closing_id=$("$UMBRIEL" windows --json | jq -r '.[] | select(.title == "overlap-close") | .id')
"$UMBRIEL" msg "window-close:$closing_id" > /dev/null
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq 'any(.[]; .title == "overlap-close")') == false ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 100
arm > "$SHOTS/armed-close.json"
jq -e '.active and .snapshot > 0 and .reserved_bytes > 0 and .deadline_msec - .start_msec == 800' "$SHOTS/armed-close.json" > /dev/null
record displaced-close
"$UMBRIEL" clock-advance 100
record moving-neighbours

spawn overlap-opener 0xFF00FF00
"$UMBRIEL" clock-advance 1
wait_state '(.active | not) and .overlap and .fallback == "overlapping_lifecycle"'
record overlap-cancelled
jq -e --slurpfile before "$SHOTS/armed-close.json" '.reserved_bytes == 0 and any(.native[]; .identity == $before[0].identity and .snapshot == $before[0].snapshot and .deadline_msec == $before[0].deadline_msec)' "$SHOTS/overlap-cancelled.json" > /dev/null
if arm > /dev/null 2>&1; then
  echo "overlap incorrectly reacquired presentation"
  exit 1
fi

# The original close ends at its own deadline, although the new opening and the
# 2400 ms survivor reflow still run. No retained lease may extend the snapshot.
"$UMBRIEL" clock-advance 601
record original-close-deadline
# Both surviving tiles continue their independent 2400 ms native motion.
if [[ ${UMBRIEL_PRESENTATION_FLOATING:-0} == 1 ]]; then
  jq -e --slurpfile early "$SHOTS/moving-neighbours.json" '
    [.views[] | select(.title == "overlap-survivor" or .title == "overlap-neighbour") |
      . as $late | $early[0].views[] | select(.id == $late.id and .floating and .box == $late.box)] | length == 2
  ' "$SHOTS/original-close-deadline.json" > /dev/null
else
  jq -e --slurpfile early "$SHOTS/moving-neighbours.json" '
    [.views[] | select(.title == "overlap-survivor" or .title == "overlap-neighbour") |
      . as $late | $early[0].views[] | select(.id == $late.id and .box != $late.box)] | length == 2
  ' "$SHOTS/original-close-deadline.json" > /dev/null
fi
jq -e --slurpfile before "$SHOTS/armed-close.json" '.overlap and (.native | length) > 0 and all(.native[]; .snapshot != $before[0].snapshot)' "$SHOTS/original-close-deadline.json" > /dev/null
if arm > /dev/null 2>&1; then
  echo "remaining native opening did not keep overlap fallback latched"
  exit 1
fi
"$UMBRIEL" clock-advance 1000
wait_state '(.overlap | not) and (.native | length) == 0'

# Existing geometry may still be moving; a fresh lifecycle gets a fresh native
# identity and original deadline, with no queued presentation from the burst.
spawn overlap-fresh 0xFFFFFF00
"$UMBRIEL" clock-advance 100
arm > "$SHOTS/armed-fresh.json"
jq -e --slurpfile before "$SHOTS/armed-close.json" '.active and .identity != $before[0].identity and .deadline_msec - .start_msec == 1600' "$SHOTS/armed-fresh.json" > /dev/null
record displaced-fresh
"$UMBRIEL" clock-advance 1600
wait_state '(.active | not) and .reserved_bytes == 0 and (.native | length) == 0'
record fresh-deadline
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle

if [[ -n ${UMBRIEL_PRESENTATION_ARTIFACTS:-} ]]; then
  mkdir -p "$UMBRIEL_PRESENTATION_ARTIFACTS/overlap-${UMBRIEL_PRESENTATION_FLOATING:-0}"
  cp "$SHOTS/"* "$UMBRIEL_PRESENTATION_ARTIFACTS/overlap-${UMBRIEL_PRESENTATION_FLOATING:-0}/"
fi

echo "C0 overlap cancels displaced rendering, preserves close snapshot identity/deadline, and admits only fresh settled native lifecycle events"
