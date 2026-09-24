#!/usr/bin/env bash
# A frozen close snapshot must keep the live ring's palette as well as its shader.
set -euo pipefail
readonly SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/shaders/barrulus/rings" && pwd)"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/close.png"
cp "$SOURCE/pulse.glsl" "$UMBRIEL_RUNTIME_DIR/ring.glsl"
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[layout]
gap = 40
[animation.windows_in]
enabled = false
[animation.windows_move]
enabled = false
[animation.windows_out]
style = "fade"
duration_ms = 1000
curve = "linear"
[appearance]
border_width = 6
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[appearance.border_shader]
shader = "ring.glsl"
animated = false
palette = true
[colors]
accent_primary = "#FF00FFFF"
accent_secondary = "#FF00FFFF"
warning = "#FF00FFFF"
error = "#FF00FFFF"
TOML
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze > /dev/null
"$UMBRIEL_UNMAP_CLIENT" palette-close 700 500 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "palette-close" and .w > 0)')
  [[ -n $window ]] && break
  sleep 0.025
done
[[ -n $window ]]
"$UMBRIEL" clock-advance 1000 > /dev/null
window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "palette-close")')
x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
y=$(jq -r '.y - 3' <<< "$window")
assert_palette() {
  local red green blue
  grim "$IMAGE"
  read -r red green blue < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$x" "$y")
  (( red > 20 && blue > 20 && green < red / 2 )) || {
    echo "$1: ring pixel $red $green $blue at $x,$y"
    return 1
  }
}
assert_palette "live ring palette missing"
"$UMBRIEL" msg window-close > /dev/null
for _ in $(seq 80); do
  if grep -q '^unmapped$' "$UMBRIEL_RUNTIME_DIR/client.log" \
      && ! "$UMBRIEL" windows --json | jq -e 'any(.[]; .title == "palette-close")' > /dev/null; then
    break
  fi
  sleep 0.025
done
grep -q '^unmapped$' "$UMBRIEL_RUNTIME_DIR/client.log"
! "$UMBRIEL" windows --json | jq -e 'any(.[]; .title == "palette-close")' > /dev/null
"$UMBRIEL" clock-advance 100 > /dev/null
assert_palette "close snapshot lost the ring palette"
echo "closing ring keeps its palette during the fade"
