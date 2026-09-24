#!/usr/bin/env bash
set -euo pipefail
readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cat >> "$UMBRIEL_CONFIG" <<TOML

[animation]
enabled = true

[appearance]
effects = ["lightning", "lightning-melt", "workspace-reveal", "comet"]

[render.effects]
enabled = true
redraw = "auto"
in_capture = false
reads_cursor = false
fps = 0

[effects.lightning.border.outer]
padding = 30
animated = true
speed = 0.35
palette = true
passes = [{ shader = "$ROOT/examples/shaders/barrulus/rings/lightning.glsl" }]
light = { enabled = true, spread = 80, intensity = 1.6, threshold = 0.5 }

[effects.lightning.border.inner]
enabled = false

[effects.lightning-melt.open]
duration_ms = 400
curve = "linear"
passes = [{ shader = "$ROOT/examples/shaders/barrulus/animations/lightning-open.glsl" }]

[effects.lightning-melt.close]
duration_ms = 500
curve = "linear"
passes = [{ shader = "$ROOT/examples/shaders/barrulus/animations/melt-close.glsl" }]

[effects.workspace-reveal.workspace]
passes = [{ shader = "$ROOT/examples/shaders/barrulus/animations/reveal.glsl" }]

[effects.comet.overlay]
passes = [
  { shader = "$ROOT/examples/shaders/barrulus/cursor/comet-0.glsl" },
  { shader = "$ROOT/examples/shaders/barrulus/cursor/comet-1.glsl" },
]

[effects.sentient.content]
palette = true
passes = [{ shader = "$ROOT/docs/examples/shaders/window/sentient-circuit-v2.glsl" }]

[effects.smoke.content]
palette = true
passes = [{ shader = "$ROOT/examples/shaders/barrulus/window/rainbow-smoke.glsl" }]

[[window_rule]]
match.title = "^ghostty$"
default_workspace = 2
effects = ["sentient"]

[[window_rule]]
match.title = "^rmpc$"
default_workspace = 5
effects = ["smoke"]

[[window_rule]]
match.app_id = "^steam_app_.*$"
effects = ["no-content"]

[effects.no-content.content]
enabled = false
TOML
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[output.HEADLESS-1]
workspaces = 5
TOML
"$UMBRIEL" msg config-reload >/dev/null
"$UMBRIEL" effects --json | jq -e '.library | length == 7' >/dev/null
"$UMBRIEL_UNMAP_CLIENT" review-anchor 600 400 > "$UMBRIEL_RUNTIME_DIR/anchor.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
for title in ghostty rmpc; do
  if [[ $title == ghostty ]]; then workspace=2; content=sentient; else workspace=5; content=smoke; fi
  "$UMBRIEL" msg "workspace-switch:$workspace" >/dev/null
  "$UMBRIEL" clock-advance 50
  "$UMBRIEL" effects --json | jq -e '.outputs[0].scopes.workspace.active.source.effect == "workspace-reveal" and .outputs[0].scopes.overlay.source.effect == "comet"' >/dev/null
  FILL_COLOR=0xFF204080 "$UMBRIEL_UNMAP_CLIENT" "$title" 600 400 > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
  pid=$!
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
    sleep 0.025
  done
  "$UMBRIEL" clock-advance 100
  "$UMBRIEL" effects --json | jq -e --arg content "$content" '.windows[] | select(.scopes.content.source.effect == $content) | .scopes["border.outer"].source.effect == "lightning" and .scopes.open.active.source.effect == "lightning-melt"' >/dev/null
  grim "$UMBRIEL_RUNTIME_DIR/$title-open.png"
  "$UMBRIEL" clock-advance 1000
  kill "$pid"
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
    sleep 0.025
  done
  "$UMBRIEL" clock-advance 100
  grim "$UMBRIEL_RUNTIME_DIR/$title-close.png"
  "$UMBRIEL" clock-advance 1000
done
echo 'section 11 definitions compiled and rendered across workspace, overlay, ring, content and lifecycle scopes'
