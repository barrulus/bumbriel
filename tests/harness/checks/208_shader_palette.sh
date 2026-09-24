#!/usr/bin/env bash
# An opted-in ring draws the configured palette and follows it across a reload; opting out keeps its own colour.
set -euo pipefail
readonly SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/shaders/barrulus/rings" && pwd)"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/palette.png"
readonly PALETTE="$UMBRIEL_RUNTIME_DIR/palette.toml"
cp "$SOURCE/pulse.glsl" "$UMBRIEL_RUNTIME_DIR/ring.glsl"
cat >> "$UMBRIEL_CONFIG" <<TOML
[include]
files = ["$PALETTE"]
[layout]
gap = 40
[animation]
enabled = false
[appearance]
effects = ["fixture"]
border_width = 6
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[effects.fixture.border.outer]
passes = [{shader = "ring.glsl"}]
padding = 0
palette = true
[render.effects]
in_capture = true
fps = 20
TOML

# Every ramp entry identical makes umbriel_palette_at() constant, so the assert needs no timing.
write_palette() {
  cat > "$PALETTE" <<TOML
[colors]
accent_primary = "$1"
accent_secondary = "$1"
warning = "$1"
error = "$1"
TOML
}

write_palette "#FF00FFFF"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL_UNMAP_CLIENT" palette-ring 700 500 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "palette-ring")')
  [[ -n $window ]] && (( $(jq -r .y <<< "$window") >= 6 )) && break
  sleep 0.025
done
[[ -n $window ]]
x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
y=$(jq -r '.y - 3' <<< "$window")

await_ring() {
  local predicate=$1 label=$2 red green blue
  for _ in $(seq 60); do
    grim "$IMAGE"
    read -r red green blue < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$x" "$y")
    (( $(echo "$red $green $blue" | awk "$predicate") )) && return 0
    sleep 0.05
  done
  echo "$label: got $red $green $blue at $x,$y"
  return 1
}

# The pulse envelope scales every channel, so compare channels against each other rather than absolutely.
await_ring '{print ($1 > 60 && $3 > 60 && $2 < $1 / 2) ? 1 : 0}' "magenta palette never reached"

write_palette "#00FF00FF"
"$UMBRIEL" msg config-reload > /dev/null
await_ring '{print ($2 > 60 && $1 < $2 / 2 && $3 < $2 / 2) ? 1 : 0}' "ring did not follow the palette change"

# Opting out must restore the shader's own colour with the palette still configured.
sed -i 's/^palette = true$/palette = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
await_ring '{print ($3 > 40 && $2 > 25 && $1 < 90) ? 1 : 0}' "opting out did not restore the shader colour"

# A cycling ring must place different ramp entries at different angles, which a constant ramp cannot show.
# Red and blue interpolate through magenta and never through green, so a surviving hue wheel is detectable.
sed -i 's/^palette = false$/palette = true/' "$UMBRIEL_CONFIG"
cat > "$PALETTE" <<'TOML'
[colors]
accent_primary = "#FF0000FF"
accent_secondary = "#FF0000FF"
warning = "#0000FFFF"
error = "#0000FFFF"
TOML
cp "$SOURCE/rainbow-ripple.glsl" "$UMBRIEL_RUNTIME_DIR/ring.glsl"
"$UMBRIEL" msg config-reload > /dev/null
for _ in $(seq 60); do
  grim "$IMAGE"
  read -r reds blues greens < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count \
    'r > 0.5 && g < 0.35 && b < 0.35' \
    'b > 0.5 && r < 0.35 && g < 0.35' \
    'g > 0.5 && r < 0.35 && b < 0.35')
  (( reds > 30 && blues > 30 && greens == 0 )) && break
  sleep 0.05
done
(( reds > 30 && blues > 30 )) \
  || { echo "cycling ring did not spread the ramp: r=$reds b=$blues"; exit 1; }
(( greens == 0 )) \
  || { echo "hue wheel survived the palette: $greens green pixels off a red-blue ramp"; exit 1; }

echo "opted-in ring tracked two palettes, opted out cleanly, and spread a four-entry ramp"
