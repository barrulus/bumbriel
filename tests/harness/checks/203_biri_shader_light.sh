#!/usr/bin/env bash
# Bright details illuminate nearby client/backdrop pixels; dim cord does not.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/light.png"
cat > "$UMBRIEL_RUNTIME_DIR/emitter.glsl" <<'GLSL'
vec4 ring_color(vec2 p) {
    float d = ring_distance(p);
    float alpha = step(0.0, d) * (1.0 - step(ring_width, d));
    return vec4(p.x < ring_size.x * 0.5 ? vec3(1,0,0) : vec3(0,0,0.2), alpha);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[layout]
gap = 80
[animation]
enabled = false
[appearance]
border_width = 6
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[appearance.border_shader]
shader = "emitter.glsl"
[appearance.border_shader.light]
enabled = true
spread = 40.0
intensity = 4.0
threshold = 0.5
TOML
"$UMBRIEL" msg config-reload >/dev/null
"$UMBRIEL_UNMAP_CLIENT" illuminated-ring 500 400 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "illuminated-ring")')
  [[ -n $window ]] && (( $(jq -r .y <<< "$window") >= 50 )) && break
  sleep 0.025
done
[[ -n $window ]]
left=$(jq -r .x <<< "$window")
right=$(jq -r '.x + .w' <<< "$window")
middle=$(jq -r '.y + (.h / 2 | floor)' <<< "$window")
sample() {
  magick "$IMAGE" -crop "2x2+$1+$middle" -format '%[fx:round(mean.r*255)] %[fx:round(mean.b*255)]\n' info:
}
grim "$IMAGE"
read -r outer_red outer_blue < <(sample "$((left - 12))")
read -r inner_red inner_blue < <(sample "$((left + 5))")
read -r dim_red dim_blue < <(sample "$((right + 12))")
(( outer_red > 10 && outer_blue < 5 )) || { echo "bright ring did not illuminate backdrop: $outer_red $outer_blue"; exit 1; }
(( inner_red > 10 )) || { echo "bright ring did not illuminate client: $inner_red"; exit 1; }
(( dim_red < 5 && dim_blue < 5 )) || { echo "dim cord emitted light: $dim_red $dim_blue"; exit 1; }
# Invalid shader edits must remove both the procedural ring and its cached spill.
printf '%s\n' 'invalid shader' > "$UMBRIEL_RUNTIME_DIR/emitter.glsl"
for _ in $(seq 60); do
  grim "$IMAGE"
  read -r outer_red outer_blue < <(sample "$((left - 12))")
  (( outer_red < 5 )) && break
  sleep 0.05
done
(( outer_red < 5 )) || { echo "cached illumination survived a failed shader edit"; exit 1; }
echo "post-opacity emission threshold, inward/outward scene lighting, and failed-edit cleanup verified"
