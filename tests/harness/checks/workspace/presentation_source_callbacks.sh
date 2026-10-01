#!/usr/bin/env bash
# Suppressed native windows must not bypass source callback ownership, even
# though their native scene/output membership is intentionally unchanged.
set -euo pipefail
trap 'echo "source callback assertion at line $LINENO"; cat "$UMBRIEL_RUNTIME_DIR/source.log"' ERR
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"
workspaces = 2
[animation]
enabled = false
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
mkfifo "$UMBRIEL_RUNTIME_DIR/source-input"
exec 7<> "$UMBRIEL_RUNTIME_DIR/source-input"
SOURCE_UPDATES=1 LOG_OUTPUTS=1 "$UMBRIEL_SEAT_LOG_CLIENT" active-source <&7 > "$UMBRIEL_RUNTIME_DIR/source.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
"$UMBRIEL" settle
before=$(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log")
first=$("$UMBRIEL" workspaces --json | jq -r '.[0].id')
last=$("$UMBRIEL" workspaces --json | jq -r '.[1].id')
"$UMBRIEL" presentation-workspace-probe "open $last"
for _ in $(seq 100); do
  [[ $("$UMBRIEL" presentation-workspace-probe status --json | jq .active) == true ]] && break
  sleep .02
done
printf n >&7
"$UMBRIEL" settle
[[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log") == "$before" ]]
"$UMBRIEL" presentation-workspace-probe "select $first"
for _ in $(seq 100); do
  [[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log") == $((before+1)) ]] && break
  sleep .02
done
[[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log") == $((before+1)) ]]
"$UMBRIEL" output-commit-hold 'HEADLESS-1 on'
printf n >&7
for _ in $(seq 100); do
  [[ $("$UMBRIEL" effect-frames --json | jq '.outputs[0].rejected_buffer_commits') -gt 0 ]] && break
  sleep .02
done
[[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log") == $((before+1)) ]]
"$UMBRIEL" output-commit-hold 'HEADLESS-1 off'
for _ in $(seq 100); do
  [[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log") == $((before+2)) ]] && break
  sleep .02
done
[[ $(grep -c '^source frame done' "$UMBRIEL_RUNTIME_DIR/source.log") == $((before+2)) ]]
"$UMBRIEL" presentation-workspace-probe cancel
"$UMBRIEL" settle
echo "Native suppressed windows receive callbacks only when their live face successfully commits"
