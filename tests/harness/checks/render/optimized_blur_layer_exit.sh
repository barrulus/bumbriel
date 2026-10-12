#!/usr/bin/env bash
# An animated background layer must leave the shared blur cache as it fades out. Once its close snapshot is gone,
# translucent windows must blur the backdrop instead of retaining the wallpaper captured during unmap.
set -euo pipefail

readonly WALLPAPER_LOG="$UMBRIEL_RUNTIME_DIR/optimized-blur-wallpaper.log"
readonly WINDOW_LOG="$UMBRIEL_RUNTIME_DIR/optimized-blur-window.log"
readonly BEFORE="$UMBRIEL_RUNTIME_DIR/optimized-blur-before-exit.png"
readonly AFTER="$UMBRIEL_RUNTIME_DIR/optimized-blur-after-exit.png"
readonly REMAPPED="$UMBRIEL_RUNTIME_DIR/optimized-blur-after-remap.png"
readonly TITLE=optimized-blur-layer-exit

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[colors]
backdrop = "#00FF00FF"

[appearance]
border_width = 0
corner_radius = 0

[appearance.blur]
enabled = true
optimized = true
passes = 2
radius = 4
noise = 0.0
brightness = 1.0
contrast = 1.0
saturation = 1.0

[animation.layers]
enabled = false
duration_ms = 250
curve = "linear"

[[window_rule]]
match.title = "^optimized-blur-layer-exit$"
blur = true
blur_optimized = true
EOF
"$UMBRIEL" msg config-reload > /dev/null

wait_for_line() {
  local log=$1 line=$2
  for _ in $(seq 100); do
    grep -q "^$line\$" "$log" && return 0
    sleep 0.02
  done
  echo "$log never printed $line: $(< "$log")"
  return 1
}

sample_window() {
  local image=$1 windows x y w h
  windows=$("$UMBRIEL" windows --json)
  read -r x y w h < <(
    jq -r --arg title "$TITLE" '.[] | select(.title == $title) | "\(.x) \(.y) \(.w) \(.h)"' <<< "$windows"
  )
  grim "$image"
  "$UMBRIEL_PIXEL_PROBE" "$image" mean "40x40+$((x + w / 2 - 20))+$((y + h / 2 - 20))"
}

# Populate the optimized cache with a stable wallpaper, then enable the close animation under test.
"$UMBRIEL_LAYER_CLIENT" HEADLESS-1 0 > "$WALLPAPER_LOG" 2>&1 &
wallpaper_pid=$!
wait_for_line "$WALLPAPER_LOG" ready
sed -i '/^\[animation.layers\]$/,/^\[/s/^enabled = false$/enabled = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null

env TRANSLUCENT_CONTENT=1 "$UMBRIEL_SUBSURFACE_CLIENT" "$TITLE" 900 600 > "$WINDOW_LOG" 2>&1 &
wait_for_line "$WINDOW_LOG" mapped
"$UMBRIEL" settle
read -r before_r before_g before_b < <(sample_window "$BEFORE")
if ((before_r < 150 || before_g > 90 || before_b < 180)); then
  echo "optimized blur did not contain the wallpaper before exit: $before_r $before_g $before_b"
  exit 1
fi

kill "$wallpaper_pid"
wait "$wallpaper_pid" 2>/dev/null || true
"$UMBRIEL" settle
read -r after_r after_g after_b < <(sample_window "$AFTER")

red_green=$((after_r - after_g)); red_green=$((red_green < 0 ? -red_green : red_green))
green_blue=$((after_g - after_b)); green_blue=$((green_blue < 0 ? -green_blue : green_blue))
transition=$((before_r - after_r)); transition=$((transition < 0 ? -transition : transition))
delta=$((before_g - after_g)); delta=$((delta < 0 ? -delta : delta)); transition=$((transition + delta))
delta=$((before_b - after_b)); delta=$((delta < 0 ? -delta : delta)); transition=$((transition + delta))
if ((red_green > 15 || green_blue > 15 || transition < 100)); then
  echo "optimized blur retained the wallpaper after its animated layer exited"
  echo "  before: $before_r $before_g $before_b"
  echo "  after:  $after_r $after_g $after_b"
  exit 1
fi

# Mapping another wallpaper with layer animations already enabled must refresh the cache through the opening fade too.
"$UMBRIEL_LAYER_CLIENT" HEADLESS-1 0 > "$WALLPAPER_LOG" 2>&1 &
wait_for_line "$WALLPAPER_LOG" ready
"$UMBRIEL" settle
read -r remapped_r remapped_g remapped_b < <(sample_window "$REMAPPED")
if ((remapped_r < 150 || remapped_g > 90 || remapped_b < 180)); then
  echo "optimized blur did not follow the remapped wallpaper through its opening animation"
  echo "  backdrop:  $after_r $after_g $after_b"
  echo "  remapped:  $remapped_r $remapped_g $remapped_b"
  exit 1
fi

echo "optimized blur follows animated background layers through map, exit, and snapshot teardown"
