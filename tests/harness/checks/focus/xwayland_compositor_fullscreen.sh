#!/usr/bin/env bash
# harness: xwayland=true
# A compositor fullscreen toggle owns an X11 window's state until the
# compositor releases it. Client-owned fullscreen still follows client requests.
set -euo pipefail

source "$UMBRIEL_HARNESS_LIB"

readonly CLIENT="${UMBRIEL_XWAYLAND_FOCUS_CLIENT:-./build-debug/tests/xwayland-focus-client}"
readonly CLIENT_LOG="$UMBRIEL_RUNTIME_DIR/xwayland-fullscreen-client.log"
readonly CONTROL="$UMBRIEL_RUNTIME_DIR/xwayland-fullscreen-control"
readonly TITLE=xwayland-compositor-fullscreen

if [[ ! -x $CLIENT || -z ${DISPLAY:-} ]]; then
  echo "the Xwayland helper or private DISPLAY is not available"
  exit 1
fi

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false
EOF
"$UMBRIEL" msg config-reload > /dev/null

mkfifo "$CONTROL"
exec {control_fd}<>"$CONTROL"
"$CLIENT" "$TITLE" <&"$control_fd" > "$CLIENT_LOG" 2>&1 &

window_state() {
  "$UMBRIEL" tearing --json | jq -r --arg title "$TITLE" '.surfaces[] | select(.title == $title) | .fullscreen'
}

wait_for_fullscreen() {
  local expected=$1 label=$2
  for _ in $(seq 60); do
    if [[ $(window_state) == "$expected" ]]; then
      return 0
    fi
    sleep 0.1
  done
  echo "$label: $($UMBRIEL windows --json)"
  echo "X11 log: $(tr '\n' '|' < "$CLIENT_LOG")"
  return 1
}

wait_for_log() {
  local start=$1 pattern=$2 label=$3
  for _ in $(seq 60); do
    if tail -n +"$start" "$UMBRIEL_LOG" | grep -Fq "$pattern"; then
      return 0
    fi
    sleep 0.1
  done
  echo "$label: $(tail -n +"$start" "$UMBRIEL_LOG" | tr '\n' '|')"
  return 1
}

for _ in $(seq 60); do
  [[ -n $(window_state) ]] && break
  sleep 0.1
done
if [[ -z $(window_state) ]]; then
  echo "the X11 fullscreen fixture did not map: $($UMBRIEL windows --json)"
  exit 1
fi

"$UMBRIEL" msg window-toggle-fullscreen > /dev/null
wait_for_fullscreen true "the compositor toggle did not enter fullscreen"

readonly LOG_MARK=$(($(wc -l < "$UMBRIEL_LOG") + 1))
printf 'fullscreen false\n' >&"$control_fd"
for _ in $(seq 60); do
  grep -q '^fullscreen-requested=false$' "$CLIENT_LOG" && break
  sleep 0.1
done
if ! grep -q '^fullscreen-requested=false$' "$CLIENT_LOG"; then
  echo "the X11 client did not send its unfullscreen request: $(tr '\n' '|' < "$CLIENT_LOG")"
  exit 1
fi
wait_for_log "$LOG_MARK" "request_fullscreen denied for compositor-owned X11" \
  "the compositor did not reject the X11 unfullscreen request"
wait_for_fullscreen true "an X11 request overrode compositor-owned fullscreen"

"$UMBRIEL" msg window-toggle-fullscreen > /dev/null
wait_for_fullscreen false "the compositor could not release its fullscreen state"

printf 'fullscreen true\n' >&"$control_fd"
wait_for_fullscreen true "the X11 client could not enter its own fullscreen state"
printf 'fullscreen false\n' >&"$control_fd"
wait_for_fullscreen false "the X11 client could not leave its own fullscreen state"

echo "compositor-owned X11 fullscreen resisted client rejection while client-owned fullscreen remained controllable"
