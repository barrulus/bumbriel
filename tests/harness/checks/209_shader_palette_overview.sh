#!/usr/bin/env bash
# Window filters and inward border overlays each own a palette on overview cards.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/overview.png"
readonly SETTINGS="$UMBRIEL_RUNTIME_DIR/effect.toml"
cat > "$UMBRIEL_RUNTIME_DIR/probe.glsl" <<'GLSL'
vec4 postprocess(vec3 c) {
    if (umbriel_palette_count <= 0) return vec4(0.0, 0.0, 1.0, 1.0);
    return vec4(umbriel_palette_at(0.0).rgb, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<TOML
[include]
files = ["$SETTINGS"]
[animation]
enabled = false
[appearance]
border_width = 6
outer_border_width = 0
[appearance.shadow]
enabled = false
TOML

set_effect() {
  local target=$1 palette=$2 color=$3
  local scope=content
  if [[ $target != window ]]; then scope=border.inner; fi
  cat > "$SETTINGS" <<TOML
[colors]
accent_primary = "$color"
accent_secondary = "$color"
warning = "$color"
error = "$color"
[appearance]
effects = ["probe"]
[render.effects]
in_capture = true
redraw = "continuous"
[effects.probe.$scope]
palette = $palette
passes = [{ shader = "probe.glsl" }]
TOML
  "$UMBRIEL" msg config-reload > /dev/null
}
open_overview() {
  # Reloading colours or shaders closes overview; never sample the desktop instead.
  "$UMBRIEL" msg overview-open > /dev/null
  "$UMBRIEL" settle > /dev/null
}
await_color() {
  local predicate=$1 label=$2 count
  for _ in $(seq 60); do
    grim "$IMAGE"
    count=$("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count "$predicate")
    (( count > 100 )) && return 0
    sleep 0.025
  done
  echo "$label: only $count matching pixels"
  return 1
}
set_effect window true "#00FF00FF"
FILL_COLOR=0xFF202020 "$UMBRIEL_UNMAP_CLIENT" palette-overview 700 500 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  "$UMBRIEL" windows --json | jq -e 'any(.[]; .title == "palette-overview" and .w > 0)' > /dev/null && break
  sleep 0.025
done
"$UMBRIEL" settle > /dev/null
await_color 'g > 0.5 && r < 0.1 && b < 0.1' "desktop window palette missing"
for target in window overlay; do
  set_effect "$target" true "#00FF00FF"
  open_overview
  await_color 'g > 0.5 && r < 0.1 && b < 0.1' "overview $target palette missing"
  set_effect "$target" true "#FF0000FF"
  open_overview
  await_color 'r > 0.5 && g < 0.1 && b < 0.1' "overview $target palette did not reload"
  set_effect "$target" false "#FF0000FF"
  open_overview
  await_color 'b > 0.5 && r < 0.1 && g < 0.1' "overview $target palette did not clear"
done
echo "overview window and overlay palettes publish, reload and clear independently"
