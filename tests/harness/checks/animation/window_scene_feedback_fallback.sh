#!/usr/bin/env bash
# Feedback-bearing participants retain their selected native shader when the
# current WindowScene capture contract cannot share its evaluation safely.
set -euo pipefail
cat > "$UMBRIEL_RUNTIME_DIR/history-border.glsl" <<'GLSL'
vec4 border(vec2 uv) {
    vec4 previous = umbriel_sample_previous(uv);
    return vec4(0.7 + previous.r * 0.1, 0.0, 0.0, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<CONFIG

[output.HEADLESS-1]
mode = "640x360"
[include]
files = ["$UMBRIEL_REPO/examples/effects/scene/water/effect.toml"]
[animation]
enabled = false
[animation.windows_in]
effect = "water"
duration_ms = 1000
[effects]
border = "history-border"
[effects.preset.history-border]
kind = "border"
shader = "history-border.glsl"
animated = false
[appearance]
border_width = 8
outer_border_width = 0
CONFIG
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" history-neighbour 600 320 > "$UMBRIEL_RUNTIME_DIR/neighbour.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep .02
done
"$UMBRIEL" settle
sed -i 's/^enabled = false$/enabled = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" history-target 600 320 > "$UMBRIEL_RUNTIME_DIR/target.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 2 ]] && break
  sleep .02
done
"$UMBRIEL" clock-advance 250
grim "$UMBRIEL_RUNTIME_DIR/native-feedback.png"
"$UMBRIEL" effects --json | jq -e '.owners[] | select(.type=="output") | .window_presentation |
  (.active|not) and .frames==0 and .memory_bytes==0 and .fallback=="unsupported_capability" and .admission=="feedback_effect"' > /dev/null
"$UMBRIEL" windows --json | jq -e 'length==2 and all(.[]; .border_effect.name=="history-border")' > /dev/null
red=$("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/native-feedback.png" count 'r>0.6 && g<0.1 && b<0.1')
(( red > 100 ))
echo "WindowScene rejects feedback before acquisition and preserves the original native border shader and pixels"
