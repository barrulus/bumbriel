#!/usr/bin/env bash
# A never-visited workspace is prepared by normal layout, captured without
# native membership changes, and refreshed atomically after client commits.
set -euo pipefail
trap 'echo "workspace source assertion at line $LINENO"; "$UMBRIEL" presentation-workspace-probe status --json; cat "$UMBRIEL_RUNTIME_DIR/source.log"' ERR
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 3
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
match.title = "^hidden-source$"
default_workspace = 2
default_focused = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/source-input"
exec 7<> "$UMBRIEL_RUNTIME_DIR/source-input"
SOURCE_UPDATES=1 LOG_OUTPUTS=1 "$UMBRIEL_SEAT_LOG_CLIENT" hidden-source <&7 > "$UMBRIEL_RUNTIME_DIR/source.log" 2>&1 &
client=$!
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.02
done
"$UMBRIEL" settle
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/workspaces-before.json"
jq -e '.[0].active and (.[1].active | not)' "$UMBRIEL_RUNTIME_DIR/workspaces-before.json" > /dev/null
selected=$(jq -r '.[1].id' "$UMBRIEL_RUNTIME_DIR/workspaces-before.json")
empty=$(jq -r '.[2].id' "$UMBRIEL_RUNTIME_DIR/workspaces-before.json")
grim "$UMBRIEL_RUNTIME_DIR/native.png"
"$UMBRIEL" presentation-workspace-probe "open $selected"
for _ in $(seq 100); do
  [[ $("$UMBRIEL" presentation-workspace-probe status --json | jq .active) == true ]] && break
  sleep 0.02
done
"$UMBRIEL" presentation-workspace-probe status --json | jq -e '.active and (.ids | length)==3 and .reserved_bytes > 0' > /dev/null
grim "$UMBRIEL_RUNTIME_DIR/blue.png"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/blue.png" pixel 320 180)
(( b > 170 && r < 80 && g > 90 )) || { echo "hidden face not blue: $r $g $b"; exit 1; }
"$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/workspaces-after.json"
cmp "$UMBRIEL_RUNTIME_DIR/workspaces-before.json" "$UMBRIEL_RUNTIME_DIR/workspaces-after.json"
[[ $(grep -c '^surface-output-enter' "$UMBRIEL_RUNTIME_DIR/source.log" || true) == 0 ]]
# Drain every initial configure callback, then fail real output commits.
for _ in $(seq 100); do
  requested=$(grep -c '^source frame requested' "$UMBRIEL_RUNTIME_DIR/source.log" || true)
  completed=$(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log" || true)
  [[ $requested -ge 1 && $completed == "$requested" ]] && break
  sleep 0.02
done
[[ $requested -ge 1 && $completed == "$requested" ]]
before=$(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log" || true)
"$UMBRIEL" output-commit-hold 'HEADLESS-1 on'
printf n >&7
for _ in $(seq 100); do
  [[ $("$UMBRIEL" effect-frames --json | jq '.outputs[0].rejected_buffer_commits') -gt 0 ]] && break
  sleep 0.02
done
# Real time deliberately crosses two 100ms hidden-workspace background ticks.
# That native fallback must not bypass the presentation's successful-commit
# ownership and complete this new source callback while submission is held.
sleep .25 # real time: cross two native 100ms background callback ticks while output submission is held
[[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log" || true) == "$before" ]]
"$UMBRIEL" output-commit-hold 'HEADLESS-1 off'
for _ in $(seq 100); do
  [[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log" || true) == $((before+1)) ]] && break
  sleep 0.02
done
[[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log" || true) == $((before+1)) ]]
grim "$UMBRIEL_RUNTIME_DIR/green.png"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/green.png" pixel 320 180)
(( g > 170 && r < 20 && b < 80 )) || { echo "live face not green: $r $g $b"; exit 1; }
"$UMBRIEL" presentation-workspace-probe "select $empty"
"$UMBRIEL" settle
grim "$UMBRIEL_RUNTIME_DIR/empty.png"
cmp "$UMBRIEL_RUNTIME_DIR/native.png" "$UMBRIEL_RUNTIME_DIR/empty.png"
"$UMBRIEL" presentation-workspace-probe "select $selected"
"$UMBRIEL" settle
grim "$UMBRIEL_RUNTIME_DIR/selected-again.png"
cmp "$UMBRIEL_RUNTIME_DIR/green.png" "$UMBRIEL_RUNTIME_DIR/selected-again.png"
"$UMBRIEL" presentation-workspace-probe cancel
"$UMBRIEL" settle
"$UMBRIEL" presentation-workspace-probe status --json | jq -e '(.active | not) and .reserved_bytes==0 and (.restore_pending | not)' > /dev/null
grim "$UMBRIEL_RUNTIME_DIR/restored.png"
cmp "$UMBRIEL_RUNTIME_DIR/native.png" "$UMBRIEL_RUNTIME_DIR/restored.png"
kill "$client"
echo "Complete paired inventory prepared a never-visited workspace, refreshed live, and delivered callbacks only after successful output submission"
