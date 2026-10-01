#!/usr/bin/env bash
# Typed scene preparation is separate from legacy13slots. Only referenced
# bundles compile; every declared stage remains watched across failure/repair.
set -euo pipefail
cat > "$UMBRIEL_RUNTIME_DIR/common.glsl" <<'GLSL'
float shade() { return 0.25; }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/pair.glsl" <<'GLSL'
vec4 transition(vec2 uv) {
  return mix(umbriel_sample_from(uv), umbriel_sample_to(uv), umbriel_progress) * shade();
}
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/unreferenced.glsl" <<'GLSL'
This is intentionally not GLSL and must not compile until referenced.
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/legacy.glsl" <<'GLSL'
vec4 screen(vec2 uv) { return umbriel_sample(uv); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/set.vert" <<'GLSL'
vec4 transition_vertex(vec2 uv) { return vec4(uv * 2.0 - 1.0, 0.0, 1.0); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/set.frag" <<'GLSL'
vec4 transition_fragment(vec2 uv, vec2 output_uv) { return umbriel_sample_item(uv); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'

[workspace_presentation]
effect = "set"
framing = "fit_all"
[animation]
enabled = true
[animation.workspaces]
effect = "pair"
[effects]
screen = "legacy"
[effects.preset.pair]
kind = "animation"
interface = "scene-v1"
scope = "workspace_pair"
common_shader = "common.glsl"
shader = "pair.glsl"
[effects.preset.unused]
kind = "animation"
interface = "scene-v1"
scope = "workspace_pair"
shader = "unreferenced.glsl"
[effects.preset.set]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "set.vert"
shader = "set.frag"
[effects.preset.legacy]
kind = "screen"
shader = "legacy.glsl"
TOML
"$UMBRIEL" msg config-reload > /dev/null
state() {
  "$UMBRIEL" effects --json | jq -r --arg name "$1" '.presets[] | select(.name == $name) | .state'
}
wait_state() {
  for _ in $(seq 100); do
    [[ $(state "$1") == "$2" ]] && return
    sleep .02
  done
  echo "expected $1 state $2; got $(state "$1")"
  return 1
}
wait_state pair compiled
wait_state set compiled
[[ $(state unused) == unreferenced ]]
[[ $(state legacy) == compiled ]]
# The optional common stage belongs to the same atomic source version.
cat > "$UMBRIEL_RUNTIME_DIR/common.glsl" <<'GLSL'
float shade() { invalid shader replacement; }
GLSL
wait_state pair failed
[[ $(state unused) == unreferenced ]]
[[ $(state legacy) == compiled ]]
cat > "$UMBRIEL_RUNTIME_DIR/common.glsl" <<'GLSL'
float shade() { return 0.5; }
GLSL
wait_state pair compiled
mv "$UMBRIEL_RUNTIME_DIR/pair.glsl" "$UMBRIEL_RUNTIME_DIR/pair.saved"
wait_state pair inert
mv "$UMBRIEL_RUNTIME_DIR/pair.saved" "$UMBRIEL_RUNTIME_DIR/pair.glsl"
wait_state pair compiled
# Changing roots admits the previously unreferenced invalid program and releases
# the old cache entry without treating either as a legacy animation slot.
sed -i 's/effect = "pair"/effect = "unused"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
wait_state unused failed
[[ $(state pair) == unreferenced ]]
[[ $(state legacy) == compiled ]]
# Empty presentation binding removes its compilation root independently of the
# global native animation switch.
sed -i 's/effect = "set"/effect = ""/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
wait_state set unreferenced
echo "typed scene roots, watched stage failure/repair, and legacy compile separation passed"
