#!/usr/bin/env bash
# A window preset's own palette key: published on opt-in, tracks a reload, absent when opted out.
set -euo pipefail
readonly SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/shaders/barrulus/window" && pwd)"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/window.png"
readonly PALETTE="$UMBRIEL_RUNTIME_DIR/palette.toml"

# A probe rather than a shipped effect: every shipped one tints existing content, so its
# output depends on what a client drew and cannot isolate the uniform.
cat > "$UMBRIEL_RUNTIME_DIR/probe.glsl" <<'GLSL'
vec4 postprocess(vec3 c) {
    if (umbriel_palette_count <= 0) return vec4(0.0, 0.0, 1.0, 1.0);
    return vec4(umbriel_palette_at(0.0).rgb, 1.0);
}
GLSL
# Window scope with no global preset configured is the whole point: a window effect has to opt
# in on its own key. Reading a palette published for the global scope would not be visible here.
cat >> "$UMBRIEL_CONFIG" <<TOML
[include]
files = ["$PALETTE"]
[animation]
enabled = false
[appearance]
effects = ["window.probe"]
[render.effects]
# Postprocess effects are excluded from captures unless this is set, so grim would
# otherwise photograph the unfiltered scene and see nothing this check asserts.
in_capture = true
redraw = "continuous"
[effects."window.probe".content]
palette = true
passes = [{ shader = "probe.glsl" }]
TOML

set_palette() {
  cat > "$PALETTE" <<TOML
[colors]
accent_primary = "$1"
accent_secondary = "$1"
warning = "$1"
error = "$1"
TOML
  "$UMBRIEL" msg config-reload > /dev/null
}

set_palette "#00FF00FF"
"$UMBRIEL_UNMAP_CLIENT" palette-window 700 500 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "palette-window")')
  [[ -n $window ]] && (( $(jq -r .w <<< "$window") > 0 )) && break
  sleep 0.025
done
[[ -n $window ]]
x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
y=$(jq -r '.y + (.h / 2 | floor)' <<< "$window")
"$UMBRIEL" settle > /dev/null

# The probe covers the window's content box, so a pixel inside it is the published colour.
await_window() {
  local label=$1 predicate=$2 r g b
  for _ in $(seq 60); do
    grim "$IMAGE"
    read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$x" "$y")
    (( $(echo "$r $g $b" | awk "$predicate") )) && return 0
    sleep 0.05
  done
  echo "$label: pixel $r $g $b at $x,$y"
  return 1
}

await_window "green palette never reached the window effect" '{print ($2 > 200 && $1 < 40 && $3 < 40) ? 1 : 0}'

set_palette "#FF0000FF"
await_window "palette change did not reach the window effect" '{print ($1 > 200 && $2 < 40 && $3 < 40) ? 1 : 0}'

# Opting out leaves the count at zero, which the probe reports as blue.
sed -i 's/^palette = true$/palette = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
await_window "a window effect that opted out still got a palette" '{print ($3 > 200 && $1 < 40 && $2 < 40) ? 1 : 0}'

# A shipped effect must still compile and run with the palette published.
cp "$SOURCE/rainbow-radial.glsl" "$UMBRIEL_RUNTIME_DIR/probe.glsl"
sed -i 's/^palette = false$/palette = true/' "$UMBRIEL_CONFIG"
set_palette "#00FF00FF"
for _ in $(seq 60); do
  grim "$IMAGE"
  read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" max)
  (( g > 200 )) && break
  sleep 0.05
done
(( g > 200 )) || { echo "shipped effect did not run with the palette published: max $r $g $b"; exit 1; }

echo "window palette published on its own key, tracked a reload, cleared on opt-out, and a shipped effect ran"
