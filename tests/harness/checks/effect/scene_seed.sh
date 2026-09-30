#!/usr/bin/env bash
# An authored shader sees one held transaction seed through navigation and
# accept/cancel retargets; a new presentation receives a fresh seed.
set -euo pipefail
cat > "$UMBRIEL_RUNTIME_DIR/seed.vert" <<'GLSL'
vec4 transition_vertex(vec2 uv) { return vec4(uv * 2.0 - 1.0, 0.0, 1.0); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/seed.frag" <<'GLSL'
vec4 transition_fragment(vec2 uv, vec2 output_uv) { return vec4(umbriel_random_seed.xyz, 1.0); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[output.HEADLESS-1]
mode = "320x180"
workspaces = 2
[animation]
duration_ms = 1000
curve = "linear"
[workspace_presentation]
effect = "seed"
[effects.preset.seed]
kind = "animation"
interface = "scene-v1"
scope = "workspace_set"
vertex_shader = "seed.vert"
shader = "seed.frag"
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .workspace_presentation'; }
for action in accept cancel; do
  "$UMBRIEL" msg workspace-presentation-enter
  "$UMBRIEL" clock-advance 1
  "$UMBRIEL" clock-advance 3000
  state | jq -e '.active and .phase=="held"' > /dev/null
  grim "$UMBRIEL_RUNTIME_DIR/$action-held.png"
  "$UMBRIEL" msg workspace-presentation-next
  "$UMBRIEL" clock-advance 1
  "$UMBRIEL" clock-advance 3000
  grim "$UMBRIEL_RUNTIME_DIR/$action-navigated.png"
  cmp "$UMBRIEL_RUNTIME_DIR/$action-held.png" "$UMBRIEL_RUNTIME_DIR/$action-navigated.png"
  "$UMBRIEL" msg "workspace-presentation-$action"
  "$UMBRIEL" clock-advance 1
  "$UMBRIEL" clock-advance 250
  state | jq -e '.active' > /dev/null
  grim "$UMBRIEL_RUNTIME_DIR/$action-exiting.png"
  cmp "$UMBRIEL_RUNTIME_DIR/$action-held.png" "$UMBRIEL_RUNTIME_DIR/$action-exiting.png"
  "$UMBRIEL" clock-advance 3000
  "$UMBRIEL" settle
  state | jq -e '(.active|not) and .memory_bytes==0' > /dev/null
done
if cmp -s "$UMBRIEL_RUNTIME_DIR/accept-held.png" "$UMBRIEL_RUNTIME_DIR/cancel-held.png"; then
  echo 'new transaction reused its previous shader seed'
  exit 1
fi
echo 'Authored workspace seed stays fixed through navigation and both exit paths, then changes on new acquisition'
