#!/usr/bin/env bash
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/window-effect.png"
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation]
enabled = false
[appearance]
border_width = 6
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[shaders]
in_capture = true
[[window_rule]]
match.title = "shaded-window"
shader = "invert"
TOML
"$UMBRIEL" msg config-reload >/dev/null
FILL_COLOR=0xFF204080 "$UMBRIEL_UNMAP_CLIENT" shaded-window 500 400 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "shaded-window")')
  [[ -n $window ]] && (( $(jq -r .y <<< "$window") >= 6 )) && break
  sleep 0.025
done
[[ -n $window ]]
x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
y=$(jq -r '.y + (.h / 2 | floor)' <<< "$window")
sample() {
  grim "$IMAGE"
  magick "$IMAGE" -crop "2x2+$x+$y" -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)] %[fx:round(mean.b*255)]\n' info:
}
read -r red green blue < <(sample)
(( red > 215 && red < 230 && green > 185 && green < 200 && blue > 120 && blue < 135 )) || {
  echo "persistent window invert missing: $red $green $blue"; exit 1;
}
read -r red green blue < <(timeout 10 "$(dirname "$UMBRIEL")/tests/toplevel-capture-client" shaded-window)
(( red > 215 && green > 185 && blue > 120 && blue < 135 )) || {
  echo "isolated included capture wrong: $red $green $blue"; exit 1;
}
sed -i 's/in_capture = true/in_capture = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload >/dev/null
read -r red green blue < <(sample)
(( red > 25 && red < 40 && green > 55 && green < 75 && blue > 120 && blue < 135 )) || {
  echo "capture did not recover unfiltered window pixels: $red $green $blue"; exit 1;
}
read -r red green blue < <(timeout 10 "$(dirname "$UMBRIEL")/tests/toplevel-capture-client" shaded-window)
(( red > 25 && red < 40 && green > 55 && green < 75 && blue > 120 && blue < 135 )) || {
  echo "isolated excluded capture wrong: $red $green $blue"; exit 1;
}
sed -i 's/in_capture = false/in_capture = true/; s/shader = "invert"/shader = "off"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload >/dev/null
read -r red green blue < <(sample)
(( red > 25 && red < 40 && green > 55 && green < 75 && blue > 120 && blue < 135 )) || {
  echo "disabled effect did not restore ordinary window pixels: $red $green $blue"; exit 1;
}
echo "persistent window shader, protocol capture exclusion, and disable recovery verified"
