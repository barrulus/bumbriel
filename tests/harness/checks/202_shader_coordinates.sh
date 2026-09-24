#!/usr/bin/env bash
# harness: outputs=2
# Persistent decoration coordinates are logical, Y down, and rotate once.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/coordinates.png"
cat > "$UMBRIEL_RUNTIME_DIR/quadrants.glsl" <<'GLSL'
vec4 ring_color(vec2 coords) {
    float distance = ring_distance(coords);
    float alpha = step(0.0, distance) * (1.0 - step(ring_width, distance));
    float brightness = 0.7 + 0.3 * sin(umbriel_time * 3.0);
    return vec4((coords.x < ring_size.x * 0.5 ? vec3(1,0,0) : vec3(0,1,0)) * brightness, alpha);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[output."HEADLESS-1"]
scale = 1.25
transform = "90"
[output."HEADLESS-2"]
scale = 2
[layout]
gap = 40
[animation]
enabled = false
[appearance]
effects = ["fixture"]
border_width = 6
outer_border_width = 0
corner_radius = 24
[appearance.shadow]
enabled = false
[effects.fixture.border.outer]
passes = [{shader = "quadrants.glsl"}]
padding = 48
[colors.border]
focused = "#FFFFFFFF"
[[window_rule]]
match.title = "ring-coordinates"
default_output = "HEADLESS-1"
[render.effects]
in_capture = true
TOML
"$UMBRIEL" msg config-reload >/dev/null
"$UMBRIEL_UNMAP_CLIENT" ring-coordinates 300 300 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "ring-coordinates")')
  [[ -n $window ]] && (( $(jq -r .y <<< "$window") >= 6 )) && break
  sleep 0.025
done
[[ -n $window ]]
sample() {
  magick "$IMAGE" -crop "2x2+$1+$2" -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)]\n' info:
}
grim -s 1 -o HEADLESS-1 "$IMAGE"
read -r width height left top < <(magick "$IMAGE" -colorspace gray -threshold 1% -trim -format '%w %h %X %Y\n' info:)
left=${left#+}
top=${top#+}
x=$((left + width / 4))
y=$((top + 2))
read -r red green < <(sample "$x" "$y")
(( red > 80 && green < 25 )) || { echo "logical left ring did not stay red: $red $green"; exit 1; }
x=$((left + width * 3 / 4))
read -r red green < <(sample "$x" "$y")
(( green > 80 && red < 25 )) || { echo "logical right ring did not stay green: $red $green"; exit 1; }
minimum=255
maximum=0
for _ in $(seq 10); do
  grim -s 1 -o HEADLESS-1 "$IMAGE"
  read -r red green < <(sample "$x" "$y")
  (( green < minimum )) && minimum=$green
  (( green > maximum )) && maximum=$green
  sleep 0.08
done
(( maximum - minimum > 10 )) || { echo "rotated output damage did not update animated pixels"; exit 1; }
grim -s 1 -o HEADLESS-2 "$IMAGE"
colored=$(magick "$IMAGE" -fx '(r > 0.85 || g > 0.85) ? 1 : 0' -format '%[fx:round(mean*w*h)]' info:)
(( colored < 10 )) || { echo "ring escaped onto the second output: $colored"; exit 1; }
echo "persistent logical coordinates survived portrait rotation and fractional scale without output leakage"
