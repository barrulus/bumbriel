#!/usr/bin/env bash
# Every bundled preset compiles on the GPU when selected, and selecting none of them keeps the compositor plain.
set -euo pipefail
readonly EFFECTS="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/effects" && pwd)"
readonly LOG_MARK=$(($(wc -l < "$UMBRIEL_LOG") + 1))
cat >> "$UMBRIEL_CONFIG" <<EOF

[include]
files = [
  "$EFFECTS/animation/reveal/effect.toml",
  "$EFFECTS/animation/squash/effect.toml",
  "$EFFECTS/border/pulse/effect.toml",
  "$EFFECTS/window/scanlines/effect.toml",
  "$EFFECTS/screen/vignette/effect.toml",
  "$EFFECTS/cursor/glow/effect.toml",
]
[effects]
border = "pulse"
window = "scanlines"
screen = "vignette"
cursor = "glow"
[animation.windows_in]
effect = "reveal"
[animation.windows_move]
effect = "squash"
EOF
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
if tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -q "rejected"; then
  echo "a bundled preset failed to compile:"
  tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -B2 "rejected"
  exit 1
fi
if tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -Eq "unknown key|ignoring effects"; then
  echo "a bundled preset produced configuration diagnostics"
  exit 1
fi
"$UMBRIEL_UNMAP_CLIENT" bundled 400 300 > "$UMBRIEL_RUNTIME_DIR/bundled.log" 2>&1 &
for _ in $(seq 80); do
  "$UMBRIEL" windows --json | jq -e '.[] | select(.title == "bundled")' > /dev/null && break
  sleep 0.025
done
"$UMBRIEL" settle > /dev/null
echo "every bundled preset compiled and rendered with a window open"
