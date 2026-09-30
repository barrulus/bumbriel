#!/usr/bin/env bash
# The configured workspace_set keeps its complete native identity set, including
# empty faces, at every supported count and with either framing policy.
set -euo pipefail
trap 'echo "carousel inventory assertion at line $LINENO"; "$UMBRIEL" effects --json' ERR
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.vert" "$UMBRIEL_RUNTIME_DIR/carousel.vert"
cp "$UMBRIEL_REPO/examples/effects/scene/carousel/shader.frag" "$UMBRIEL_RUNTIME_DIR/carousel.frag"
cp "$UMBRIEL_CONFIG" "$UMBRIEL_RUNTIME_DIR/carousel-base.toml"
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .workspace_presentation'; }
advance() { "$UMBRIEL" clock-advance 1; "$UMBRIEL" clock-advance 3000; }
"$UMBRIEL" clock-freeze
for framing in viewport fit_all; do
  for count in 1 2 3 4 5 8 64; do
    cp "$UMBRIEL_RUNTIME_DIR/carousel-base.toml" "$UMBRIEL_CONFIG"
    cat >> "$UMBRIEL_CONFIG" <<CONFIG

[output.HEADLESS-1]
mode = "320x180"
workspaces = $count
[animation]
duration_ms = 100
curve = "linear"
[workspace_presentation]
effect = "carousel"
framing = "$framing"
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
    "$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/native.json"
    "$UMBRIEL" msg workspace-presentation-enter
    advance
    state | jq -e --argjson count "$count" --slurpfile native "$UMBRIEL_RUNTIME_DIR/native.json" '
      .active and .phase=="held" and .memory_bytes>0 and .memory_bytes<=268435456 and
      (.sources.ids | length)==$count and .sources.ids==[$native[0][] | .id] and
      .sources.landing_width==320 and .sources.landing_height==180
    ' > /dev/null
    "$UMBRIEL" msg "workspace-presentation-select:$count"
    advance
    state | jq -e --argjson count "$count" '.active and .phase=="held" and (.navigation - ((.navigation / $count)|floor) * $count)==($count-1)' > /dev/null
    "$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/held.json"
    cmp "$UMBRIEL_RUNTIME_DIR/native.json" "$UMBRIEL_RUNTIME_DIR/held.json"
    "$UMBRIEL" msg workspace-presentation-accept
    advance
    "$UMBRIEL" settle
    state | jq -e '(.active | not) and .memory_bytes==0' > /dev/null
    "$UMBRIEL" workspaces --json | jq -e --argjson count "$count" 'any(.[]; .index==$count and .active)' > /dev/null
  done
done
echo "Configured carousel retained and committed complete 1/2/3/4/5/8/64 inventories with viewport and fit_all framing"
