#!/usr/bin/env bash
# Selected animation pairs must govern the new deferred tiled-opening path,
# including their enable flag and duration when the base event is disabled.
set -euo pipefail

cat > "$UMBRIEL_RUNTIME_DIR/pair-open.glsl" <<'GLSL'
vec4 animation(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[animation.windows_in]
enabled = false
[animation.windows_move]
enabled = false
[animation.pair.marker.open]
shader = "pair-open.glsl"
duration_ms = 1600
curve = "linear"
[animation.pair.marker.close]
shader = "pair-open.glsl"
TOML
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" msg 'shader:animation marker' > /dev/null
# Animation time only moves by clock-advance: the opener starts on the frozen instant, so 800 ms lands mid-way through
# the pair's 1600 ms timeline and a further 1600 ms finishes it.
"$UMBRIEL" clock-freeze
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" pair-opener 700 500 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
window=''
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "pair-opener")')
  [[ -n $window ]] && break
  sleep 0.025
done
[[ -n $window ]] || { echo "pair opener never mapped"; exit 1; }
read_pixel() {
  # The first IPC record can precede the admitting arrange. Sample the current
  # client centre, not coordinates captured before tiled placement settled.
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "pair-opener")')
  x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
  y=$(jq -r '.y + (.h / 2 | floor)' <<< "$window")
  grim "$UMBRIEL_RUNTIME_DIR/pair.png"
  magick "$UMBRIEL_RUNTIME_DIR/pair.png" -crop "8x8+$x+$y" +repage \
    -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)] %[fx:round(mean.b*255)]\n' info:
}
"$UMBRIEL" clock-advance 800
read -r red green blue < <(read_pixel)
(( red < 30 && green > 220 && blue < 30 )) || {
  echo "selected pair did not animate the tiled opener: $red $green $blue"
  exit 1
}
"$UMBRIEL" clock-advance 1600
read -r red green blue < <(read_pixel)
(( red < 30 && green < 30 && blue > 220 )) || {
  echo "selected pair did not finish at its own duration: $red $green $blue"
  exit 1
}
echo "selected pair controls tiled opening with the base event disabled"
