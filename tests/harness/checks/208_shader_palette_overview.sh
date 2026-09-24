#!/usr/bin/env bash
# Overview has separate ring nodes: publish, reload and clear their palettes too.
set -euo pipefail
readonly SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/shaders/barrulus/rings" && pwd)"
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/overview.png"
readonly PALETTE="$UMBRIEL_RUNTIME_DIR/palette.toml"
cp "$SOURCE/pulse.glsl" "$UMBRIEL_RUNTIME_DIR/ring.glsl"
cat >> "$UMBRIEL_CONFIG" <<TOML
[include]
files = ["$PALETTE"]
[animation]
enabled = false
[appearance]
effects = ["fixture"]
border_width = 8
outer_border_width = 0
[appearance.shadow]
enabled = false
[effects.fixture.border.outer]
passes = [{shader = "ring.glsl"}]
animated = false
palette = true
[render.effects]
in_capture = true
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
set_palette "#FF00FFFF"
FILL_COLOR=0xFF202020 "$UMBRIEL_UNMAP_CLIENT" palette-overview 700 500 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  "$UMBRIEL" windows --json | jq -e 'any(.[]; .title == "palette-overview" and .w > 0)' > /dev/null && break
  sleep 0.025
done
"$UMBRIEL" settle > /dev/null

await_color() {
  local predicate=$1 label=$2 count
  for _ in $(seq 60); do
    grim "$IMAGE"
    count=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count "$predicate")
    (( count > 30 )) && return 0
    sleep 0.025
  done
  echo "$label: only $count matching pixels"
  return 1
}
await_color 'r > 0.3 && b > 0.3 && g < 0.1' "desktop ring palette missing"
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" settle > /dev/null
await_color 'r > 0.3 && b > 0.3 && g < 0.1' "overview lost the ring palette"
set_palette "#FF0000FF"
# Config reload closes overview; reopen it before inspecting the new card nodes.
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" settle > /dev/null
await_color 'r > 0.3 && g < 0.1 && b < 0.1' "overview retained the old ring palette"
sed -i 's/^palette = true$/palette = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" settle > /dev/null
await_color 'b > 0.3 && g > 0.3 && r < 0.2' "overview did not clear the ring palette"
echo "overview ring palette survives entry, follows reload and clears on opt-out"
