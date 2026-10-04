#!/usr/bin/env bash
# Clients must not be offered minimize while Umbriel has no minimize behavior.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly CLIENT_LOG="$UMBRIEL_RUNTIME_DIR/wm-capabilities-client.log"

if [[ ! -x $CLIENT ]]; then
  echo "unmap-client is not built"
  exit 1
fi

LOG_WM_CAPABILITIES=1 "$CLIENT" wm-capabilities > "$CLIENT_LOG" 2>&1 &
client_pid=$!

for _ in $(seq 80); do
  grep -q '^wm-capabilities-done$' "$CLIENT_LOG" && break
  if ! kill -0 "$client_pid" 2>/dev/null; then
    wait "$client_pid" 2>/dev/null || true
    echo "capability client exited before the initial configure: $(< "$CLIENT_LOG")"
    exit 1
  fi
  sleep 0.05
done

if ! grep -q '^wm-capabilities-done$' "$CLIENT_LOG"; then
  echo "capability client did not receive the initial capabilities: $(< "$CLIENT_LOG")"
  exit 1
fi
for capability in window-menu maximize fullscreen; do
  if ! grep -q "^wm-capability=$capability$" "$CLIENT_LOG"; then
    echo "expected $capability capability was absent: $(< "$CLIENT_LOG")"
    exit 1
  fi
done
if grep -q '^wm-capability=minimize$' "$CLIENT_LOG"; then
  echo "unsupported minimize capability was advertised: $(< "$CLIENT_LOG")"
  exit 1
fi
