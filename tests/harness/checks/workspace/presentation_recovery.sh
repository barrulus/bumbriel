#!/usr/bin/env bash
set -euo pipefail
trap 'echo "scene recovery assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[output.HEADLESS-1]
mode = "640x360"
workspaces = 3
transform = "normal"
[animation]
enabled = true
duration_ms = 100
curve = "linear"
[workspace_presentation]
effect = "carousel"
[effects.preset.carousel]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "carousel.vert"
shader = "carousel.frag"
[effects.preset.carousel.parameters]
max_elevation_degrees = 35.0
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
state() {
  "$UMBRIEL" effects --json | jq '.owners[] | select(.type == "output" and .name == "HEADLESS-1") | .workspace_presentation'
}
enter() {
  "$UMBRIEL" settle
  "$UMBRIEL" msg workspace-presentation-enter
  "$UMBRIEL" clock-advance 1
  "$UMBRIEL" clock-advance 3000
  state | jq -e '.active and .phase == "held" and .memory_bytes > 0' > /dev/null
}
released() {
  for _ in $(seq 100); do
    [[ $(state | jq .active) == false ]] && break
    sleep .02
  done
  state | jq -e '(.active | not) and .memory_bytes == 0' > /dev/null
}
enter
grim "$UMBRIEL_RUNTIME_DIR/before.png"
printf '\nthis is deliberately invalid GLSL\n' >> "$UMBRIEL_RUNTIME_DIR/carousel.vert"
"$UMBRIEL" msg config-reload > /dev/null
for _ in $(seq 100); do
  [[ $("$UMBRIEL" effects --json | jq -r '.presets[] | select(.name == "carousel") | .state') == failed ]] && break
  sleep .02
done
state | jq -e '.active and .phase == "held"' > /dev/null
grim "$UMBRIEL_RUNTIME_DIR/retained.png"
cmp "$UMBRIEL_RUNTIME_DIR/before.png" "$UMBRIEL_RUNTIME_DIR/retained.png"
sed -i 's/effect = "carousel"/effect = ""/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
released
state | jq -e '.fallback == "binding_removed"' > /dev/null
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
sed -i 's/effect = ""/effect = "carousel"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
enter
"$UMBRIEL" renderer-recover > /dev/null
released
state | jq -e '.fallback == "renderer_lost"' > /dev/null
# Recovery recompiles the registry and permits another transaction.
for _ in $(seq 100); do
  [[ $("$UMBRIEL" effects --json | jq -r '.presets[] | select(.name == "carousel") | .state') == compiled ]] && break
  sleep .02
done
enter
sed -i 's/transform = "normal"/transform = "90"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
released
enter
"$UMBRIEL" msg overview-open
released
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" msg overview-close
"$UMBRIEL" clock-advance 3000
enter
mkfifo "$UMBRIEL_RUNTIME_DIR/lock-control"
exec 7<> "$UMBRIEL_RUNTIME_DIR/lock-control"
"$UMBRIEL_LOCK_CLIENT" <&7 > "$UMBRIEL_RUNTIME_DIR/lock.log" 2>&1 &
for _ in $(seq 100); do
  grep -q '^locked$' "$UMBRIEL_RUNTIME_DIR/lock.log" && break
  sleep .02
done
grep -q '^locked$' "$UMBRIEL_RUNTIME_DIR/lock.log"
released
state | jq -e '.fallback == "locked"' > /dev/null
echo unlock >&7
for _ in $(seq 100); do
  grep -q '^unlocked$' "$UMBRIEL_RUNTIME_DIR/lock.log" && break
  sleep .02
done
enter
"$UMBRIEL" msg workspace-presentation-cancel
"$UMBRIEL" clock-advance 1
"$UMBRIEL" clock-advance 3000
released
echo 'Configured carousel retains bundles across failed edits and releases cleanly on binding removal, renderer loss, transform, overview and lock'
