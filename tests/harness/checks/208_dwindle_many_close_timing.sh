#!/usr/bin/env bash
# Closing the root leaf of a five-window dwindle tree moves several differently sized survivors. Their visible geometry
# must keep following windows_move for its whole clock instead of pinning at the first overshoot of a non-linear curve.
set -euo pipefail

readonly SHOTS="$UMBRIEL_RUNTIME_DIR/dwindle-many-close-timing"
readonly OUT_MS=${DWINDLE_OUT_MS:-1370}
readonly MOVE_MS=${DWINDLE_MOVE_MS:-1030}
readonly MOVE_CURVE=${DWINDLE_MOVE_CURVE:-snappy}
mkdir -p "$SHOTS"

cat > "$UMBRIEL_RUNTIME_DIR/dwindle-transparent-close.glsl" <<'GLSL'
vec4 animation(vec2 uv) {
    return vec4(0.0);
}
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/dwindle-move-marker.glsl" <<'GLSL'
vec4 animation(vec2 uv) {
    vec4 source = umbriel_sample(uv);
    return vec4(source.rgb * 0.78 + vec3(0.22), source.a);
}
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

[animation.windows_in]
enabled = false

[animation.windows_out]
enabled = true
duration_ms = $OUT_MS
curve = "linear"
shader = "dwindle-transparent-close.glsl"

[animation.windows_move]
enabled = true
duration_ms = $MOVE_MS
curve = "$MOVE_CURVE"
shader = "dwindle-move-marker.glsl"

[animation.workspaces]
enabled = false
EOF
"$UMBRIEL" msg config-reload > /dev/null

now_ms() {
  local stamp seconds fraction
  read -r stamp _ < /proc/uptime
  seconds=${stamp%%.*}
  fraction=${stamp#*.}000
  fraction=${fraction:0:3}
  printf '%d\n' "$((10#$seconds * 1000 + 10#$fraction))"
}

spawn() {
  local title=$1 color=$2
  FILL_COLOR="$color" "$UMBRIEL_UNMAP_CLIENT" "$title" 1280 720 \
    > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
  for _ in $(seq 100); do
    if "$UMBRIEL" windows --json | jq -e --arg title "$title" 'any(.[]; .title == $title)' > /dev/null; then
      return 0
    fi
    sleep 0.025
  done
  echo "timed out waiting for $title"
  return 1
}

window_id() {
  "$UMBRIEL" windows --json | jq -r --arg title "$1" '.[] | select(.title == $title) | .id'
}

colour_bounds() {
  local colour=$1 file=$2 expression
  case $colour in
    red) expression='r > 0.75 && g < 0.48 && b < 0.48' ;;
    green) expression='g > 0.75 && r < 0.48 && b < 0.48' ;;
    blue) expression='b > 0.75 && r < 0.48 && g < 0.48' ;;
    yellow) expression='r > 0.75 && g > 0.75 && b < 0.48' ;;
  esac
  magick "$file" -alpha off -fx "($expression) ? 1 : 0" -bordercolor black -border 1 -trim \
    -format '%[fx:page.x-1] %[fx:page.y-1] %w %h\n' info: 2> /dev/null
}

move_marker_pixels() {
  magick "$1" -alpha off -fx 'r > 0.9 && g > 0.12 && g < 0.4 && b > 0.12 && b < 0.4 ? 1 : 0' \
    -format '%[fx:round(mean*w*h)]\n' info:
}

spawn dwindle-close-root 0xFFFFFFFF
spawn dwindle-survivor-red 0xFFFF0000
spawn dwindle-survivor-green 0xFF00FF00
spawn dwindle-survivor-blue 0xFF0000FF
spawn dwindle-survivor-yellow 0xFFFFFF00
sleep 1.4

grim "$SHOTS/before.png"
readonly CLOSE_ID=$(window_id dwindle-close-root)
if [[ -z $CLOSE_ID ]]; then
  echo "five-window dwindle setup did not expose the root leaf"
  exit 1
fi

request_ms=$(now_ms)
"$UMBRIEL" msg "window-close:$CLOSE_ID" > /dev/null
frame_count=0
deadline_ms=$((request_ms + OUT_MS + 350))
declare -a sample_times=()
while :; do
  before_ms=$(now_ms)
  ((before_ms > deadline_ms)) && break
  grim "$SHOTS/frame-$frame_count.png"
  after_ms=$(now_ms)
  sample_times[$frame_count]=$(((before_ms + after_ms) / 2))
  frame_count=$((frame_count + 1))
  sleep 0.035
done

if ! grep -q '^unmapped$' "$UMBRIEL_RUNTIME_DIR/dwindle-close-root.log"; then
  echo "root dwindle leaf did not unmap"
  exit 1
fi

readonly colours=(red green blue yellow)
declare -A box_x=() box_y=() box_w=() box_h=()
declare -a marker_pixels=()
max_gap_ms=0
for colour in "${colours[@]}"; do
  read -r box_x["$colour,before"] box_y["$colour,before"] \
    box_w["$colour,before"] box_h["$colour,before"] <<< "$(colour_bounds "$colour" "$SHOTS/before.png")"
done
for ((i = 0; i < frame_count; i++)); do
  if ((i > 0)); then
    gap_ms=$((sample_times[i] - sample_times[i - 1]))
    ((gap_ms > max_gap_ms)) && max_gap_ms=$gap_ms
  fi
  for colour in "${colours[@]}"; do
    read -r box_x["$colour,$i"] box_y["$colour,$i"] box_w["$colour,$i"] box_h["$colour,$i"] \
      <<< "$(colour_bounds "$colour" "$SHOTS/frame-$i.png")"
  done
  marker_pixels[$i]=$(move_marker_pixels "$SHOTS/frame-$i.png")
done
if ((max_gap_ms > 150)); then
  echo "screenshot cadence was too sparse for dwindle timing assertions: maximum gap ${max_gap_ms} ms"
  exit 1
fi

marker_first=-1
marker_last=-1
for ((i = 0; i < frame_count; i++)); do
  if ((marker_pixels[i] > 100)); then
    ((marker_first < 0)) && marker_first=$i
    marker_last=$i
  fi
done
if ((marker_first < 0 || marker_last < marker_first)); then
  echo "windows_move shader marker never appeared on the red survivor"
  exit 1
fi

timing_tolerance=$((2 * max_gap_ms + 120))
marker_start_ms=$((sample_times[marker_first] - request_ms))
marker_end_ms=$((sample_times[marker_last] - request_ms))
expected_end_ms=$MOVE_MS
if ((marker_start_ms > timing_tolerance)); then
  echo "windows_move marker began at ${marker_start_ms} ms, expected it to start immediately within ${timing_tolerance} ms"
  exit 1
fi
if ((marker_end_ms < expected_end_ms - timing_tolerance || marker_end_ms > expected_end_ms + timing_tolerance)); then
  echo "windows_move marker ended at ${marker_end_ms} ms, expected ${expected_end_ms} ms within ${timing_tolerance} ms"
  exit 1
fi

abs() {
  local value=$1
  ((value < 0)) && value=$((-value))
  printf '%d\n' "$value"
}

moving_survivors=0
early_survivors=0
last_frame=$((frame_count - 1))
for colour in "${colours[@]}"; do
  initial="${box_x[$colour,before]} ${box_y[$colour,before]} ${box_w[$colour,before]} ${box_h[$colour,before]}"
  final="${box_x[$colour,$last_frame]} ${box_y[$colour,$last_frame]} ${box_w[$colour,$last_frame]} ${box_h[$colour,$last_frame]}"
  delta=$((
    $(abs $((box_x[$colour,$last_frame] - box_x[$colour,before])))
    + $(abs $((box_y[$colour,$last_frame] - box_y[$colour,before])))
    + $(abs $((box_w[$colour,$last_frame] - box_w[$colour,before])))
    + $(abs $((box_h[$colour,$last_frame] - box_h[$colour,before])))
  ))
  ((delta < 20)) && continue
  moving_survivors=$((moving_survivors + 1))

  first_change=-1
  last_change=-1
  previous=$initial
  for ((i = 0; i < frame_count; i++)); do
    current="${box_x[$colour,$i]} ${box_y[$colour,$i]} ${box_w[$colour,$i]} ${box_h[$colour,$i]}"
    if [[ $current != "$initial" && $first_change -lt 0 ]]; then
      first_change=$i
    fi
    if [[ $current != "$previous" ]]; then
      last_change=$i
    fi
    previous=$current
  done
  if ((first_change < 0 || last_change < first_change)); then
    echo "$colour survivor changed target by $delta pixels but showed no intermediate geometry"
    exit 1
  fi
  first_elapsed=$((sample_times[first_change] - request_ms))
  last_elapsed=$((sample_times[last_change] - request_ms))
  printf '%s survivor: delta=%d px, visible geometry=%d..%d ms, %s -> %s\n' \
    "$colour" "$delta" "$first_elapsed" "$last_elapsed" "$initial" "$final"
  if ((first_elapsed > timing_tolerance)); then
    echo "$colour survivor began at ${first_elapsed} ms, expected it to start immediately within ${timing_tolerance} ms"
    exit 1
  fi
  # Allow rounding to remove the final few pixels, but not the final quarter of the configured motion. In particular,
  # the default snappy curve crosses 1 near its midpoint and later returns; pinning at that first crossing is observable.
  if ((last_elapsed < expected_end_ms - MOVE_MS / 4)); then
    early_survivors=$((early_survivors + 1))
  fi
done

if ((moving_survivors < 2)); then
  echo "closing the root leaf moved only $moving_survivors measurable dwindle survivor(s)"
  exit 1
fi
if ((early_survivors > 0)); then
  echo "$early_survivors of $moving_survivors dwindle survivors stopped before the final quarter of windows_move, while its shader remained active through ${marker_end_ms} ms"
  exit 1
fi

printf 'five-window dwindle close: %d survivors followed %s for %d ms starting immediately\n' \
  "$moving_survivors" "$MOVE_CURVE" "$MOVE_MS"
