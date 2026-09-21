#!/usr/bin/env bash
# Persistent ring pixels must continue changing with every transition disabled.
set -euo pipefail
readonly SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/shaders/biri/rings" && pwd)"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/ring.png"
cp "$SOURCE/pulse.glsl" "$UMBRIEL_RUNTIME_DIR/ring.glsl"
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[layout]
gap = 40
[animation]
enabled = false
[appearance]
border_width = 6
outer_border_width = 0
corner_radius = 0
shader_fps = 20
[appearance.shadow]
enabled = false
[appearance.border_shader]
shader = "ring.glsl"
padding = 24
[colors.border]
focused = "#FF0000FF"
unfocused = "#00FF00FF"
TOML
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL_UNMAP_CLIENT" persistent-ring 700 500 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "persistent-ring")')
  [[ -n $window ]] && (( $(jq -r .y <<< "$window") >= 6 )) && break
  sleep 0.025
done
[[ -n $window ]]
x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
y=$(jq -r '.y - 3' <<< "$window")
inside=$(jq -r '.y + 12' <<< "$window")
read_ring() {
  grim "$IMAGE"
  magick "$IMAGE" -crop "1x1+$x+$y" -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)] %[fx:round(mean.b*255)]\n' info:
}
minimum=255
maximum=0
for _ in $(seq 12); do
  read -r red green blue < <(read_ring)
  if (( blue < 35 || green < 25 || red > 90 )); then
    echo "persistent cyan ring absent: $red $green $blue at $x,$y"
    exit 1
  fi
  (( blue < minimum )) && minimum=$blue
  (( blue > maximum )) && maximum=$blue
  sleep 0.09
done
(( maximum - minimum > 20 )) || { echo "ring did not animate after transition completion"; exit 1; }
# Ordinary content must remain visible through the hole.
blue=$(magick "$IMAGE" -crop "1x1+$x+$inside" -format '%[fx:round(mean.b*255)]' info:)
(( blue > 100 ))
# A failed edit must fall back to the original six-pixel border, and recover
# automatically on the next valid source edit.
printf '%s\n' 'deliberately invalid GLSL' > "$UMBRIEL_RUNTIME_DIR/ring.glsl"
for _ in $(seq 60); do
  read -r red green blue < <(read_ring)
  (( red > 220 && green < 20 && blue < 20 )) && break
  sleep 0.05
done
(( red > 220 && green < 20 && blue < 20 )) || { echo "invalid ring did not restore native styling"; exit 1; }
cp "$SOURCE/pulse.glsl" "$UMBRIEL_RUNTIME_DIR/ring.glsl"
for _ in $(seq 60); do
  read -r red green blue < <(read_ring)
  (( blue > 40 && green > 25 && red < 90 )) && break
  sleep 0.05
done
(( blue > 40 && green > 25 && red < 90 )) || { echo "valid edit did not recover the ring"; exit 1; }
"$UMBRIEL" msg overview-open >/dev/null
for _ in $(seq 40); do
  grim "$IMAGE"
  cyan=$(magick "$IMAGE" -fx '(b > 0.3 && g > 0.2 && r < 0.25) ? 1 : 0' -format '%[fx:round(mean*w*h)]' info:)
  (( cyan > 20 )) && break
  sleep 0.05
done
(( cyan > 20 )) || { echo "overview did not retain the procedural ring"; exit 1; }
echo "persistent animated hollow ring, reload recovery, and overview presentation verified"
