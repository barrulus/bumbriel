#!/usr/bin/env bash
# Even RGBA8's image-storage lower bound does not admit the complete 4K/64-face
# transaction: native landing, paired versions and scratch also count. FP16's
# stricter bound is independently covered by the renderer resource tests.
set -euo pipefail
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'
[output.HEADLESS-1]
mode = "3840x2160"
workspaces = 64
[animation]
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
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/native.json"
# Acquisition may reject synchronously or during hidden-source preparation.
"$UMBRIEL" msg workspace-presentation-enter > "$UMBRIEL_RUNTIME_DIR/enter.log" 2>&1 || true
"$UMBRIEL" clock-advance 1
"$UMBRIEL" settle
"$UMBRIEL" effects --json | jq -e '
  [.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .workspace_presentation |
   (.active|not) and .memory_bytes==0 and .fallback=="resource_budget"] | length==1 and all
' > /dev/null
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/after.json"
cmp "$UMBRIEL_RUNTIME_DIR/native.json" "$UMBRIEL_RUNTIME_DIR/after.json"
"$UMBRIEL" msg workspace-switch:64
"$UMBRIEL" clock-advance 3000
"$UMBRIEL" settle
"$UMBRIEL" workspaces --json | jq -e 'length==64 and any(.[]; .index==64 and .active)' > /dev/null
# Actual ordinary output remains capturable after releasing every reservation.
grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/native-after-budget.png"
echo 'Complete 4K/64-face budget rejection releases resources and preserves every native workspace'
