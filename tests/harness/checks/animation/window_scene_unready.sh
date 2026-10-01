#!/usr/bin/env bash
# Never acknowledge neighbour reflow: preparation must expire before publishing
# any displaced image, even when the animation clock is frozen.
set -euo pipefail
cat >> "$UMBRIEL_CONFIG" <<CONFIG

[output.HEADLESS-1]
mode = "640x360"
[layout]
mode = "master"
[animation]
enabled = false
[include]
files = ["$UMBRIEL_REPO/examples/effects/scene/water/effect.toml"]
[animation.windows_in]
effect = "water"
duration_ms = 1000
[animation.windows_move]
duration_ms = 2400
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/held-input"
exec 7<> "$UMBRIEL_RUNTIME_DIR/held-input"
HOLD_RESIZE_CONTROL=1 "$UMBRIEL_SEAT_LOG_CLIENT" window-scene-held <&7 > "$UMBRIEL_RUNTIME_DIR/held.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
"$UMBRIEL" settle
sed -i 's/enabled = false/enabled = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
printf h >&7
for _ in $(seq 100); do
  grep -q '^resize-hold-armed$' "$UMBRIEL_RUNTIME_DIR/held.log" && break
  sleep .02
done
grep -q '^resize-hold-armed$' "$UMBRIEL_RUNTIME_DIR/held.log"
"$UMBRIEL_UNMAP_CLIENT" window-scene-peer 600 320 > "$UMBRIEL_RUNTIME_DIR/peer.log" 2>&1 &
state() { "$UMBRIEL" effects --json | jq '.owners[] | select(.type=="output" and .name=="HEADLESS-1") | .window_presentation'; }
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
  sleep .02
done
for _ in $(seq 100); do
  grep -q '^resize-configure-held$' "$UMBRIEL_RUNTIME_DIR/held.log" && break
  sleep .02
done
grep -q '^resize-configure-held$' "$UMBRIEL_RUNTIME_DIR/held.log"
"$UMBRIEL" clock-advance 1
for _ in $(seq 200); do
  [[ $(state | jq '.active') == false ]] && break
  sleep .02
done
state | jq -e '(.active|not) and .frames==0 and .memory_bytes==0 and .fallback=="source_unavailable"' > /dev/null
"$UMBRIEL" windows --json | jq -e 'length==2' > /dev/null
echo "Unacknowledged native geometry preparation expires without publishing a window scene or advancing the frozen clock"
