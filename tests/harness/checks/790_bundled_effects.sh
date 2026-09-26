#!/usr/bin/env bash
# Including every bundled preset and selecting none compiles none of them and requests no effect frames with a window
# open; selecting them all compiles each on the GPU without diagnostics.
set -euo pipefail
readonly EFFECTS="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../examples/effects" && pwd)"
readonly LOG_MARK=$(($(wc -l < "$UMBRIEL_LOG") + 1))
frames() { "$UMBRIEL" effect-frames --json | jq '[.outputs[].effect_frames] | add'; }
open_window() {
  "$UMBRIEL_UNMAP_CLIENT" "$1" 400 300 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  for _ in $(seq 80); do
    "$UMBRIEL" windows --json | jq -e --arg t "$1" '.[] | select(.title == $t)' > /dev/null && return
    sleep 0.025
  done
  echo "the $1 client never mapped"
  exit 1
}
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
EOF
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
open_window plain
"$UMBRIEL" settle > /dev/null
if tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -q "Compiling .* shader: $EFFECTS/"; then
  echo "an included but unselected preset compiled:"
  tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep "Compiling .* shader: $EFFECTS/"
  exit 1
fi
if (( $(frames) != 0 )); then
  echo "included but unselected presets requested effect frames: $(frames)"
  exit 1
fi
cat >> "$UMBRIEL_CONFIG" <<EOF
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
if tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -q "failed to compile"; then
  echo "a bundled preset failed to compile:"
  tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -B2 "failed to compile"
  exit 1
fi
if tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -Eq "unknown key|ignoring effects"; then
  echo "a bundled preset produced configuration diagnostics"
  exit 1
fi
open_window bundled
"$UMBRIEL" settle > /dev/null
echo "included presets stayed uncompiled until selected; every bundled preset compiled and rendered with a window open"
