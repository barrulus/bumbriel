#!/usr/bin/env bash
# harness: outputs=1
# A fixed-size window opens floating, and a default_maximize rule matching it (an Electron splash shares its app's
# app_id) cannot grow it. It keeps its size, centered in the maximized box, instead of being pinned to the box's corner.
set -euo pipefail

readonly CLIENT="${UMBRIEL_SUBSURFACE_CLIENT:-./build-debug/tests/subsurface-client}"

cat >> "$UMBRIEL_CONFIG" << 'EOF'

[animation]
enabled = false

[[window_rule]]
match.title = "^fixed-splash$"
default_maximize = true
EOF
"$UMBRIEL" msg config-reload > /dev/null

FIXED_SIZE=1 "$CLIENT" fixed-splash 300 200 > "$UMBRIEL_RUNTIME_DIR/fixed-splash.log" 2>&1 &

field_of() {
  "$UMBRIEL" windows --json \
    | jq -r --arg title "$1" --arg field "$2" '.[] | select(.title == $title) | .[$field]'
}

# Defaults: gap 8 and border 2 give an edge pad of 10, so the maximized box is 1260x700+10+10 on the 1280x720 output.
expected=300x200+490+260
actual=
for _ in $(seq 80); do
  actual="$(field_of fixed-splash w)x$(field_of fixed-splash h)+$(field_of fixed-splash x)+$(field_of fixed-splash y)"
  [[ $actual == "$expected" ]] && break
  sleep 0.1
done
"$UMBRIEL" settle
actual="$(field_of fixed-splash w)x$(field_of fixed-splash h)+$(field_of fixed-splash x)+$(field_of fixed-splash y)"
if [[ $actual != "$expected" ]]; then
  echo "expected the fixed-size maximized float at $expected, got $actual"
  exit 1
fi

echo "a fixed-size float opened by default_maximize kept its size, centered in the maximized box"
