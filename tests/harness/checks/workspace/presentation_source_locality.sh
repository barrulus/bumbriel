#!/usr/bin/env bash
# harness: outputs=2
# One output's held workspace source leaves its neighbour's pixels, native
# membership, audio/frame scheduling and callbacks under ordinary ownership.
set -euo pipefail
trap 'echo "source locality assertion at line $LINENO"; "$UMBRIEL" presentation-workspace-probe status --json' ERR
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
position = [0, 0]
workspaces = 2
[output.HEADLESS-2]
mode = "640x360"
position = [640, 0]
workspaces = 2
[animation]
enabled = false
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[[window_rule]]
match.title = "^local-source$"
default_output = "HEADLESS-1"
default_workspace = 2
default_focused = false
[[window_rule]]
match.title = "^neighbour-source$"
default_output = "HEADLESS-2"
default_focused = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF00CC33 "$UMBRIEL_UNMAP_CLIENT" local-source 600 320 > "$UMBRIEL_RUNTIME_DIR/local.log" 2>&1 &
"$UMBRIEL_SEAT_LOG_CLIENT" neighbour-source > "$UMBRIEL_RUNTIME_DIR/neighbour.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
  sleep .02
done
"$UMBRIEL" settle
grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/home-before.png"
grim -o HEADLESS-2 "$UMBRIEL_RUNTIME_DIR/neighbour-before.png"
second=$("$UMBRIEL" workspaces --json | jq -r '.[] | select(.output=="HEADLESS-1" and .index==2) | .id')
"$UMBRIEL" presentation-workspace-probe "open $second"
"$UMBRIEL" settle
grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/home-source.png"
grim -o HEADLESS-2 "$UMBRIEL_RUNTIME_DIR/neighbour-during.png"
cmp "$UMBRIEL_RUNTIME_DIR/neighbour-before.png" "$UMBRIEL_RUNTIME_DIR/neighbour-during.png"
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/home-source.png" pixel 320 180)
(( g > 170 && r < 20 && b < 80 )) || { echo "source included foreign output: $r $g $b"; exit 1; }
"$UMBRIEL" settle
before=$("$UMBRIEL" effect-frames --json | jq '.outputs[] | select(.name=="HEADLESS-2") | .buffer_commits')
# Captures can leave another buffer pending after settle's one-frame barrier.
# Establish an idle neighbour before attributing any subsequent commit to the
# source selection; continuous setup activity must fail this bounded wait.
quiet=0
for _ in $(seq 100); do
  sleep .02
  current=$("$UMBRIEL" effect-frames --json | jq '.outputs[] | select(.name=="HEADLESS-2") | .buffer_commits')
  if [[ $current == "$before" ]]; then
    quiet=$((quiet + 1))
    (( quiet == 3 )) && break
  else
    quiet=0
    before=$current
  fi
done
(( quiet == 3 )) || { echo "neighbour did not become idle before source selection"; exit 1; }
# A source content change occurs only on the owning output. Poll its completed
# revision: settle explicitly schedules every output and would itself wake the
# neighbour, obscuring the property under test.
revision=$("$UMBRIEL" presentation-workspace-probe status --json | jq '.committed_revision')
"$UMBRIEL" presentation-workspace-probe "select HEADLESS-1:1"
for _ in $(seq 100); do
  [[ $("$UMBRIEL" presentation-workspace-probe status --json | jq --argjson before "$revision" '.committed_revision > $before') == true ]] && break
  sleep .02
done
"$UMBRIEL" presentation-workspace-probe status --json | jq -e --argjson before "$revision" '.committed_revision > $before' > /dev/null
# Observe several headless frame periods after the source revision commits so
# a foreign frame scheduled later than its owner's frame cannot escape detection.
for _ in $(seq 3); do
  sleep .02
  after=$("$UMBRIEL" effect-frames --json | jq '.outputs[] | select(.name=="HEADLESS-2") | .buffer_commits')
  [[ $before == "$after" ]] || { echo "source selection woke neighbouring output: $before -> $after"; exit 1; }
done
"$UMBRIEL" presentation-touch-probe create
"$UMBRIEL" presentation-touch-probe 'down 1 0.75 0.5'
"$UMBRIEL" presentation-touch-probe 'up 1'
for _ in $(seq 100); do
  [[ $(grep -c '^touch-down' "$UMBRIEL_RUNTIME_DIR/neighbour.log" || true) == 1 ]] && break
  sleep .02
done
[[ $(grep -c '^touch-down' "$UMBRIEL_RUNTIME_DIR/neighbour.log" || true) == 1 ]]
"$UMBRIEL" presentation-workspace-probe status --json | jq -e '.active' > /dev/null
# The same touch device dismisses only when its coordinates hit the owning output.
"$UMBRIEL" presentation-touch-probe 'down 2 0.25 0.5'
"$UMBRIEL" presentation-touch-probe 'up 2'
"$UMBRIEL" settle
"$UMBRIEL" presentation-workspace-probe status --json | jq -e '(.active | not)' > /dev/null
"$UMBRIEL" presentation-touch-probe 'down 3 0.75 0.5'
"$UMBRIEL" presentation-touch-probe 'up 3'
for _ in $(seq 100); do
  [[ $(grep -c '^touch-down' "$UMBRIEL_RUNTIME_DIR/neighbour.log" || true) == 2 ]] && break
  sleep .02
done
[[ $(grep -c '^touch-down' "$UMBRIEL_RUNTIME_DIR/neighbour.log" || true) == 2 ]]
"$UMBRIEL" presentation-touch-probe destroy
"$UMBRIEL" settle
grim -o HEADLESS-1 "$UMBRIEL_RUNTIME_DIR/home-after.png"
grim -o HEADLESS-2 "$UMBRIEL_RUNTIME_DIR/neighbour-after.png"
cmp "$UMBRIEL_RUNTIME_DIR/home-before.png" "$UMBRIEL_RUNTIME_DIR/home-after.png"
cmp "$UMBRIEL_RUNTIME_DIR/neighbour-before.png" "$UMBRIEL_RUNTIME_DIR/neighbour-after.png"
echo "Workspace sources remain output-local without foreign pixels or neighbour frame wakes"
