#!/usr/bin/env bash
# Runtime ownership freezes the complete native identity inventory. Automatic
# pruning resumes on release; explicit mutation cancels before changing IDs.
set -euo pipefail
trap 'echo "inventory assertion failed at line $LINENO"; "$UMBRIEL" presentation-inventory-probe status --json; "$UMBRIEL" workspaces --json' ERR
readonly BASE="$UMBRIEL_RUNTIME_DIR/inventory-base.toml"
cp "$UMBRIEL_CONFIG" "$BASE"
write_config() {
  cp "$BASE" "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<CONFIG

[animation]
enabled = false
[output.HEADLESS-1]
mode = "640x360"
workspaces = $1
CONFIG
  "$UMBRIEL" msg config-reload > /dev/null
}
for count in 1 2 3 4 5 8 64; do
  write_config "$count"
  "$UMBRIEL" presentation-inventory-probe hold --json > "$UMBRIEL_RUNTIME_DIR/inventory-held.json"
  "$UMBRIEL" workspaces --json > "$UMBRIEL_RUNTIME_DIR/inventory-native.json"
  jq -e --argjson count "$count" --slurpfile native "$UMBRIEL_RUNTIME_DIR/inventory-native.json" '
    .held and (.ids | length) == $count and (.ids | unique | length) == $count and
    .ids == [$native[0][] | .id] and .original == ($native[0][] | select(.active) | .id)
  ' "$UMBRIEL_RUNTIME_DIR/inventory-held.json" > /dev/null
  if "$UMBRIEL" presentation-inventory-probe hold > /dev/null 2>&1; then
    echo "duplicate native inventory owner admitted"
    exit 1
  fi
  if [[ $count != 64 ]]; then
    "$UMBRIEL" presentation-inventory-probe release
  fi
done
# Reconfigure while the full 64-entry inventory is still owned.
write_config 2
"$UMBRIEL" presentation-inventory-probe status --json | jq -e '(.held | not) and .invalidations == 1' > /dev/null
[[ $("$UMBRIEL" workspaces --json | jq length) == 2 ]]
write_config '"dynamic"'
spawn() {
  "$UMBRIEL_UNMAP_CLIENT" "$1" 600 320 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  spawned_pid=$!
  for _ in $(seq 80); do
    [[ $("$UMBRIEL" windows --json | jq --arg name "$1" 'any(.[]; .title == $name)') == true ]] && return
    sleep 0.025
  done
  exit 1
}
spawn inventory-a
closing_pid=$spawned_pid
"$UMBRIEL" settle
"$UMBRIEL" msg workspace-switch:2
spawn inventory-b
"$UMBRIEL" settle
[[ $("$UMBRIEL" workspaces --json | jq length) == 3 ]]
"$UMBRIEL" presentation-inventory-probe hold --json > "$UMBRIEL_RUNTIME_DIR/dynamic-held.json"
# Exit the hidden client directly: the public close action is scoped to the
# active workspace and must not be repurposed to mutate an invisible target.
kill "$closing_pid"
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
[[ $("$UMBRIEL" windows --json | jq length) == 1 ]]
"$UMBRIEL" settle
"$UMBRIEL" presentation-inventory-probe status --json | jq -e --slurpfile before "$UMBRIEL_RUNTIME_DIR/dynamic-held.json" '
  .held and .reconciliation_pending and .ids == $before[0].ids and .original == $before[0].original
' > /dev/null
[[ $("$UMBRIEL" workspaces --json | jq length) == 3 ]]
"$UMBRIEL" presentation-inventory-probe release
"$UMBRIEL" presentation-inventory-probe status --json | jq -e '(.held | not) and (.reconciliation_pending | not)' > /dev/null
[[ $("$UMBRIEL" workspaces --json | jq length) == 2 ]]
# Renumbering did not replace the active workspace's original stable identity.
"$UMBRIEL" workspaces --json | jq -e --slurpfile before "$UMBRIEL_RUNTIME_DIR/dynamic-held.json" '
  (.[] | select(.active) | .id) == $before[0].original
' > /dev/null
"$UMBRIEL" presentation-inventory-probe hold
"$UMBRIEL" msg workspace-switch:2
"$UMBRIEL" presentation-inventory-probe status --json | jq -e '(.held | not) and .invalidations == 2' > /dev/null
# A held output can disappear without a dangling group pointer or a second release.
"$UMBRIEL" output-create HEADLESS-2 > /dev/null
"$UMBRIEL" settle
"$UMBRIEL" presentation-inventory-probe hold
"$UMBRIEL" output-destroy HEADLESS-1
"$UMBRIEL" settle
"$UMBRIEL" presentation-inventory-probe status --json | jq -e '(.held | not)' > /dev/null
"$UMBRIEL" workspaces --json | jq -e 'length > 0' > /dev/null
echo "Native inventory holds preserve all 1/2/3/4/5/8/64 identities, defer pruning until release, and cancel safely on explicit mutation/output teardown"
