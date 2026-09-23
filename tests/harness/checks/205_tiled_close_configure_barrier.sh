#!/usr/bin/env bash
# A survivor receives its target configure and begins visible movement immediately while the independent windows_out
# clock runs. A later stale-size client keeps the compositor endpoint instead of falling back to its old buffer.
set -euo pipefail

readonly OUT_MS=1000
readonly MOVE_MS=600
readonly SHOTS="$UMBRIEL_RUNTIME_DIR/tiled-close-configure"
mkdir -p "$SHOTS"

# The snapshot holds its captured box above the vacancy for its whole lifecycle, so mark only its bottom band. That
# band stays clear of the survivor until the movement has finished, keeping the survivor's geometry observable while
# the close clock is still running.
cat > "$UMBRIEL_RUNTIME_DIR/configure-close.glsl" <<'GLSL'
vec4 animation(vec2 uv) { return uv.y > 0.9 ? vec4(0.0, 0.5, 0.0, 0.5) : vec4(0.0); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<EOF

[colors]
backdrop = "#000000FF"

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

[layout]
mode = "dwindle"
gap = 0

[layout.master]
default_width_fraction = 0.5

[animation.windows_in]
enabled = false

[animation.windows_out]
enabled = true
duration_ms = $OUT_MS
curve = "linear"
shader = "configure-close.glsl"

[animation.windows_move]
enabled = true
duration_ms = $MOVE_MS
curve = "linear"
EOF
"$UMBRIEL" msg workspace-set-layout:master > /dev/null

spawn() {
  local title=$1 log=$2 color=$3
  FILL_COLOR="$color" LOG_CONFIGURES=1 "$UMBRIEL_UNMAP_CLIENT" "$title" 1280 720 > "$log" 2>&1 &
  for _ in $(seq 100); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "$title" '.[] | select(.title == $title)')
    [[ -n $window ]] && return 0
    sleep 0.025
  done
  echo "timed out waiting for $title"
  return 1
}

wait_for_log() {
  local log=$1 pattern=$2 message=$3
  for _ in $(seq 100); do
    grep -q "$pattern" "$log" && return 0
    sleep 0.025
  done
  echo "$message: $(tail -n 20 "$log")"
  return 1
}

wait_for_log_since() {
  local log=$1 mark=$2 pattern=$3 message=$4
  for _ in $(seq 100); do
    if tail -n "+$((mark + 1))" "$log" | grep -q "$pattern"; then
      return 0
    fi
    sleep 0.025
  done
  echo "$message: $(tail -n "+$((mark + 1))" "$log")"
  return 1
}

# The marker band may sit over the moving survivor, so identify it by its green channel alone.
green_pixels() {
  magick "$1" -alpha off -fx '(g > 0.12 && b < 0.08) ? 1 : 0' \
    -format '%[fx:round(mean*w*h)]\n' info:
}

red_bounds() {
  magick "$1" -alpha off -fx '(r > 0.2 && g < 0.08 && b < 0.08) ? 1 : 0' \
    -bordercolor black -border 1 -trim -format '%X %Y %w %h\n' info: 2> /dev/null
}

pattern_bounds() {
  magick "$1" -alpha off -fx '((b > 0.2 || g > 0.2) && r < 0.08) ? 1 : 0' \
    -bordercolor black -border 1 -trim -format '%X %Y %w %h\n' info: 2> /dev/null
}

bounds_match() {
  local tolerance=$1 ax=$2 ay=$3 aw=$4 ah=$5 bx=$6 by=$7 bw=$8 bh=$9
  ((ax >= bx - tolerance && ax <= bx + tolerance \
    && ay >= by - tolerance && ay <= by + tolerance \
    && aw >= bw - tolerance && aw <= bw + tolerance \
    && ah >= bh - tolerance && ah <= bh + tolerance))
}

verify_layout() {
  local mode=$1
  local survivor_log="$UMBRIEL_RUNTIME_DIR/close-configure-$mode-survivor.log"
  local third_log="$UMBRIEL_RUNTIME_DIR/close-configure-$mode-third.log"

  spawn "close-configure-$mode-first" "$UMBRIEL_RUNTIME_DIR/close-configure-$mode-first.log" 0xFF000080
  sleep 0.7
  spawn "close-configure-$mode-survivor" "$survivor_log" 0xFFFF0000
  sleep 0.7
  spawn "close-configure-$mode-third" "$third_log" 0xFF808000
  sleep 0.7

  wait_for_log "$survivor_log" 'configured-size=640x360' "$mode survivor never received its three-window size"
  local mark closing_id
  mark=$(wc -l < "$survivor_log")
  closing_id=$(jq -r .id <<< "$window")
  grim "$SHOTS/$mode-before.png"
  "$UMBRIEL" msg "window-close:$closing_id" > /dev/null
  wait_for_log "$third_log" '^unmapped$' "$mode third window did not unmap"
  wait_for_log_since "$survivor_log" "$mark" 'configured-size=640x720' \
    "$mode survivor did not receive its target configure"

  sleep 0.2
  grim "$SHOTS/$mode-held.png"

  # windows_move runs for MOVE_MS from the close; windows_out keeps running past it on its own clock.
  sleep 0.15
  grim "$SHOTS/$mode-moving.png"
  sleep 0.6
  grim "$SHOTS/$mode-final.png"

  local before_x before_y before_w before_h held_x held_y held_w held_h
  local moving_x moving_y moving_w moving_h final_x final_y final_w final_h
  read -r before_x before_y before_w before_h <<< "$(red_bounds "$SHOTS/$mode-before.png")"
  read -r held_x held_y held_w held_h <<< "$(red_bounds "$SHOTS/$mode-held.png")"
  read -r moving_x moving_y moving_w moving_h <<< "$(red_bounds "$SHOTS/$mode-moving.png")"
  read -r final_x final_y final_w final_h <<< "$(red_bounds "$SHOTS/$mode-final.png")"
  if ((held_h <= before_h + 20 || held_h >= 700)); then
    echo "$mode survivor had not started windows_move by the first sample: before=$before_h held=$held_h"
    exit 1
  fi
  if ((moving_h <= held_h + 20 || moving_h >= 700)); then
    echo "$mode survivor did not visibly resize during windows_move: held=$held_h moving=$moving_h"
    exit 1
  fi
  if ! bounds_match 3 "$final_x" "$final_y" "$final_w" "$final_h" 640 0 640 720; then
    echo "$mode survivor missed its final geometry: got=$final_x $final_y $final_w $final_h"
    exit 1
  fi

  local close_pixels
  close_pixels=$(green_pixels "$SHOTS/$mode-moving.png")
  if ((close_pixels < 500)); then
    echo "$mode survivor began visible movement only after windows_out ended: pixels=$close_pixels"
    exit 1
  fi
}

verify_layout dwindle
sleep 0.7
"$UMBRIEL" msg workspace-switch:2 > /dev/null
sleep 0.4
"$UMBRIEL" msg workspace-set-layout:master > /dev/null
sleep 0.1
verify_layout master

# A client may acknowledge the configure but keep rendering its old size. The compositor-owned endpoint must remain
# authoritative after windows_move, rather than snapping back to that stale buffer while waiting for a later commit.
"$UMBRIEL" msg workspace-switch:3 > /dev/null
sleep 0.2
HOLD_SIZE=1 "$UMBRIEL_FRACTIONAL_CLIENT" close-configure-stale-survivor 640 720 \
  > "$UMBRIEL_RUNTIME_DIR/close-configure-stale-survivor.log" 2>&1 &
for _ in $(seq 100); do
  window=$(
    "$UMBRIEL" windows --json \
      | jq -c '.[] | select(.title == "close-configure-stale-survivor")'
  )
  [[ -n $window ]] && break
  sleep 0.025
done
if [[ -z $window ]]; then
  echo "timed out waiting for stale-size survivor"
  exit 1
fi
sleep 0.7
spawn close-configure-stale-third "$UMBRIEL_RUNTIME_DIR/close-configure-stale-third.log" 0xFF000000
sleep 0.7
closing_id=$(jq -r .id <<< "$window")
"$UMBRIEL" msg "window-close:$closing_id" > /dev/null
wait_for_log "$UMBRIEL_RUNTIME_DIR/close-configure-stale-third.log" '^unmapped$' \
  "stale-size closer did not unmap"
sleep 1.2
grim "$SHOTS/stale-final.png"
read -r final_x final_y final_w final_h <<< "$(pattern_bounds "$SHOTS/stale-final.png")"
if ! bounds_match 3 "$final_x" "$final_y" "$final_w" "$final_h" 0 0 1280 720; then
  echo "completed windows_move snapped back to a stale client buffer: got=$final_x $final_y $final_w $final_h"
  exit 1
fi

# A later geometry-stable arrange must not discard that endpoint ownership while the client still refuses the
# configured size. Force at least one layout replacement while keeping a lone tiled view fullscreen in both modes.
"$UMBRIEL" msg workspace-set-layout:master > /dev/null
sleep 0.05
"$UMBRIEL" msg workspace-set-layout:dwindle > /dev/null
sleep 0.2
grim "$SHOTS/stale-after-arrange.png"
read -r final_x final_y final_w final_h <<< "$(pattern_bounds "$SHOTS/stale-after-arrange.png")"
if ! bounds_match 3 "$final_x" "$final_y" "$final_w" "$final_h" 0 0 1280 720; then
  echo "stable arrange discarded the stale-client endpoint hold: got=$final_x $final_y $final_w $final_h"
  exit 1
fi

echo "dwindle and master started moving at once, kept the close clock running, and retained stale-client endpoints"
