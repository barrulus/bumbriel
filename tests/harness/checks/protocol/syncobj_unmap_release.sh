#!/usr/bin/env bash
# A client that unmaps an explicitly synchronized surface with a NULL buffer gets the release point of its last buffer
# signalled, whether it keeps the xdg role or destroys it first the way Chromium hides a window for a tab drag.
# Chromium waits for that release before it draws again, so a pending point leaves its re-shown window unmapped.
set -euo pipefail

readonly CLIENT="${UMBRIEL_SYNCOBJ_CLIENT:-./build-debug/tests/syncobj-client}"

for mode in role-destroy unmap; do
  log="$UMBRIEL_RUNTIME_DIR/syncobj-$mode.log"
  if ! "$CLIENT" "$mode" > "$log" 2>&1; then
    echo "$mode: $(cat "$log")"
    exit 1
  fi
  if ! grep -q '^released$' "$log"; then
    echo "$mode: release point not reported: $(cat "$log")"
    exit 1
  fi
done

echo "release points signalled after a NULL buffer commit, with and without the xdg role"
