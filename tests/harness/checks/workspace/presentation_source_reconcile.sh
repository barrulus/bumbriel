#!/usr/bin/env bash
# Windows can join and leave a held workspace source inventory. Source buffer
# ownership survives native client teardown and IDs are never replaced.
set -euo pipefail
trap 'echo "source reconcile assertion at line $LINENO"; "$UMBRIEL" presentation-workspace-probe status --json' ERR
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 2
[animation]
enabled = false
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[[window_rule]]
match.title = "^live-source-"
default_workspace = 2
default_focused = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/native-ids.json"
second=$(jq -r '.[1].id' "$UMBRIEL_RUNTIME_DIR/native-ids.json")
"$UMBRIEL" presentation-workspace-probe "open $second"
"$UMBRIEL" settle
grim "$UMBRIEL_RUNTIME_DIR/empty.png"
for number in 1 2; do
  FILL_COLOR=0xFF00CC33 "$UMBRIEL_UNMAP_CLIENT" "live-source-$number" 600 320 > "$UMBRIEL_RUNTIME_DIR/client-$number.log" 2>&1 &
  child=$!
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
    sleep .02
  done
  "$UMBRIEL" settle
  "$UMBRIEL" presentation-workspace-probe status --json | jq -e --slurpfile native "$UMBRIEL_RUNTIME_DIR/native-ids.json" '.active and .ids==[$native[0][] | .id]' > /dev/null
  grim "$UMBRIEL_RUNTIME_DIR/joined.png"
  read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/joined.png" pixel 320 180)
  (( g > 170 && r < 20 && b < 80 )) || { echo "new source owner absent: $r $g $b"; exit 1; }
  first=$(jq -r '.[0].id' "$UMBRIEL_RUNTIME_DIR/native-ids.json")
  owner=$("$UMBRIEL" windows --json | jq -r '.[0].id')
  "$UMBRIEL" presentation-workspace-probe "move-view $owner $first"
  "$UMBRIEL" settle
  "$UMBRIEL" presentation-workspace-probe status --json | jq -e '.active' > /dev/null
  grim "$UMBRIEL_RUNTIME_DIR/migrated.png"
  cmp "$UMBRIEL_RUNTIME_DIR/empty.png" "$UMBRIEL_RUNTIME_DIR/migrated.png"
  "$UMBRIEL" presentation-workspace-probe "move-view $owner $second"
  "$UMBRIEL" settle
  grim "$UMBRIEL_RUNTIME_DIR/returned.png"
  cmp "$UMBRIEL_RUNTIME_DIR/joined.png" "$UMBRIEL_RUNTIME_DIR/returned.png"
  "$UMBRIEL" output-commit-hold 'HEADLESS-1 on'
  kill "$child"
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == 0 ]] && break
    sleep .02
  done
  "$UMBRIEL" presentation-workspace-probe status --json | jq -e '.active' > /dev/null
  "$UMBRIEL" output-commit-hold 'HEADLESS-1 off'
  "$UMBRIEL" settle
  grim "$UMBRIEL_RUNTIME_DIR/left.png"
  cmp "$UMBRIEL_RUNTIME_DIR/empty.png" "$UMBRIEL_RUNTIME_DIR/left.png"
done
"$UMBRIEL" presentation-workspace-probe cancel
"$UMBRIEL" settle
echo "Live native maps/unmaps refreshed held paired sources without changing inventory IDs or dangling client resources"
