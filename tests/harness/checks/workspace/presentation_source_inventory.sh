#!/usr/bin/env bash
# Complete inventories remain complete while native-sized paired sources and
# live-refresh candidates are charged before any source allocation.
set -euo pipefail
trap 'echo "source inventory assertion at line $LINENO"; "$UMBRIEL" presentation-workspace-probe status --json' ERR
cp "$UMBRIEL_CONFIG" "$UMBRIEL_RUNTIME_DIR/source-base.toml"
for count in 1 2 3 4 5 8 64; do
  cp "$UMBRIEL_RUNTIME_DIR/source-base.toml" "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<CONFIG

[output.HEADLESS-1]
mode = "320x180"
workspaces = $count
[animation]
enabled = false
CONFIG
  "$UMBRIEL" msg config-reload > /dev/null
  "$UMBRIEL" settle
  "$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/native-inventory.json"
  first=$(jq -r '.[0].id' "$UMBRIEL_RUNTIME_DIR/native-inventory.json")
  last=$(jq -r '.[-1].id' "$UMBRIEL_RUNTIME_DIR/native-inventory.json")
  "$UMBRIEL" presentation-workspace-probe "open $first"
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" presentation-workspace-probe status --json | jq '.active and .committed_revision == .captured_revision') == true ]] && break
    sleep 0.02
  done
  "$UMBRIEL" presentation-workspace-probe status --json | jq -e --argjson count "$count" --slurpfile native "$UMBRIEL_RUNTIME_DIR/native-inventory.json" '
    .active and (.ids | length)==$count and .ids==[$native[0][] | .id] and
    .retained_bytes > 0 and .reserved_bytes >= .retained_bytes and .reserved_bytes <= 268435456
  ' > /dev/null
  "$UMBRIEL" presentation-workspace-probe "select $last"
  "$UMBRIEL" settle
  "$UMBRIEL" presentation-workspace-probe status --json | jq -e --arg last "$last" '.active and .selected==$last' > /dev/null
  "$UMBRIEL" presentation-workspace-probe "landing $last"
  "$UMBRIEL" settle
  "$UMBRIEL" presentation-workspace-probe status --json | jq -e '.landing_width==320 and .landing_height==180 and .reserved_bytes <= 268435456' > /dev/null
  "$UMBRIEL" presentation-workspace-probe cancel
  "$UMBRIEL" settle
  "$UMBRIEL" presentation-workspace-probe status --json | jq -e '.reserved_bytes==0 and (.active | not)' > /dev/null
done
echo "Native source provider retained every face in 1/2/3/4/5/8/64 paired inventories with bounded atomic refresh"
