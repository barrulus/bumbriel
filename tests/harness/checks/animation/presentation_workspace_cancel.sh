#!/usr/bin/env bash
# Workspace mutation cancels before the next presentation, with input still
# guarded while restoration cannot commit. Hidden old fades cannot own a new lease.
set -euo pipefail
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 2

[animation.windows_in]
duration_ms = 1600
curve = "linear"
style = "fade"

[animation.workspaces]
enabled = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
"$UMBRIEL_UNMAP_CLIENT" presentation-workspace 600 320 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 400
"$UMBRIEL" presentation-scene-probe arm
grim "$UMBRIEL_RUNTIME_DIR/displaced.png"
"$UMBRIEL" output-commit-hold 'HEADLESS-1 on'
"$UMBRIEL" msg workspace-switch:2
for _ in $(seq 80); do
  [[ $("$UMBRIEL" presentation-scene-probe status --json | jq '.active') == false ]] && break
  sleep 0.025
done
"$UMBRIEL" presentation-scene-probe status --json | jq -e '
  (.active | not) and .fallback == "topology_changed" and .reserved_bytes == 0 and .restore_pending and (.native | length) == 0
' > /dev/null
"$UMBRIEL" presentation-input-probe status --json | jq -e '.restore_pending' > /dev/null
"$UMBRIEL" output-commit-hold 'HEADLESS-1 off'
for _ in $(seq 80); do
  [[ $("$UMBRIEL" presentation-input-probe status --json | jq '.restore_pending') == false ]] && break
  sleep 0.025
done
"$UMBRIEL" presentation-input-probe status --json | jq -e '(.restore_pending | not) and (.pending | not)' > /dev/null
if "$UMBRIEL" presentation-scene-probe arm > /dev/null 2>&1; then
  echo "hidden opening incorrectly admitted as current workspace presentation"
  exit 1
fi
"$UMBRIEL" clock-advance 2000
"$UMBRIEL" settle
echo "Workspace mutation canceled the lease and preserved restoration input ownership; hidden native fades cannot reacquire"
