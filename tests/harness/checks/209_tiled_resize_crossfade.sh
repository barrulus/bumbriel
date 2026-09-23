#!/usr/bin/env bash
# A tiled neighbour that redraws at its new size mid-reflow crossfades from its outgoing frame on the windows_move
# clock instead of switching in one frame. The survivor paints red at its first size and green at every later one, so
# a blended pixel exists only while both frames are composed.
set -euo pipefail

readonly IMAGE="$UMBRIEL_RUNTIME_DIR/tiled-resize-crossfade.png"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[colors]
backdrop = "#000000FF"

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

[layout]
mode = "master"

[animation.windows_in]
enabled = false

[animation.windows_out]
enabled = false

[animation.dim_unfocused]
enabled = false

[animation.windows_move]
enabled = true
duration_ms = 1000
curve = "linear"
EOF
"$UMBRIEL" msg config-reload > /dev/null

spawn() {
  local title=$1
  shift
  env "$@" "$UMBRIEL_UNMAP_CLIENT" "$title" 1200 700 > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "$title" '.[] | select(.title == $title)')
    [[ -n $window ]] && return 0
    sleep 0.025
  done
  echo "timed out waiting for $title"
  return 1
}

# The survivor keeps the left part of the output in both layouts, so this point shows it throughout the reflow.
survivor_rgb() {
  grim "$IMAGE"
  magick "$IMAGE" -alpha off -crop '8x8+146+356' +repage \
    -format '%[fx:round(255*mean.r)] %[fx:round(255*mean.g)] %[fx:round(255*mean.b)]\n' info:
}

spawn resize-crossfade-survivor FILL_COLOR=0xFFFF0000 RESIZE_FILL_COLOR=0xFF00FF00
sleep 1.2
read -r red green blue < <(survivor_rgb)
if ! ((red > 220 && green < 30 && blue < 30)); then
  echo "setup did not settle the survivor on its first red frame: $red $green $blue"
  exit 1
fi

spawn resize-crossfade-opener FILL_COLOR=0xFF0000FF
sleep 0.35
read -r mid_red mid_green mid_blue < <(survivor_rgb)
sleep 1.2
read -r end_red end_green end_blue < <(survivor_rgb)

if ! ((mid_red >= 40 && mid_red <= 215 && mid_green >= 40 && mid_green <= 215 && mid_blue < 30)); then
  echo "survivor did not crossfade from its outgoing frame mid-reflow: $mid_red $mid_green $mid_blue"
  exit 1
fi
if ! ((end_red < 30 && end_green > 220 && end_blue < 30)); then
  echo "survivor did not finish on its redrawn frame: $end_red $end_green $end_blue"
  exit 1
fi

echo "the resized survivor crossfaded red -> green ($mid_red $mid_green) and settled on its redrawn frame"
