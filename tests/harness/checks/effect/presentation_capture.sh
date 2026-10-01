#!/usr/bin/env bash
# Output capture retains the unfiltered visual role while the display presents
# the filtered role of the same frozen, displaced desktop source.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/presentation-capture.png"
cat > "$UMBRIEL_RUNTIME_DIR/presentation-tint.glsl" <<'GLSL'
vec4 window(vec2 uv) { vec4 c = umbriel_sample(uv); return vec4(c.a, 0.0, 0.0, c.a); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'CONFIG'

[output.HEADLESS-1]
mode = "640x360"

[colors]
backdrop = "#000000FF"

[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0

[appearance.shadow]
enabled = false

[animation.windows_in]
duration_ms = 1600
curve = "linear"
style = "fade"

[animation.windows_move]
enabled = false

[effects]
window = "presentation_tint"
in_capture = false

[effects.preset.presentation_tint]
kind = "window"
shader = "presentation-tint.glsl"
CONFIG
if [[ ${UMBRIEL_PRESENTATION_FILTERED:-0} == 1 ]]; then
  sed -i 's/^in_capture = false$/in_capture = true/' "$UMBRIEL_CONFIG"
fi
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" presentation-capture 600 320 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 800
"$UMBRIEL" presentation-scene-probe arm
# The native effect entry is now suppressed from ordinary output rendering;
# capture eligibility must still select the retained unfiltered role.
grim "$IMAGE"
"$UMBRIEL" presentation-scene-probe status --json | jq -e '.active and .reserved_bytes > 0' > /dev/null
read -r r g b < <("$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel 320 180)
if [[ ${UMBRIEL_PRESENTATION_FILTERED:-0} == 1 ]]; then
  (( r > 40 && g < 20 && b < 20 )) || { echo "filtered retained role mismatch: $r $g $b"; exit 1; }
else
  (( b > 40 && r < 20 && g < 20 )) || { echo "unfiltered retained role mismatch: $r $g $b"; exit 1; }
fi
[[ $("$UMBRIEL_PIXEL_PROBE" "$IMAGE" count 'r < 0.01 && g < 0.01 && b < 0.01' 8x8+2+2) == 64 ]]
"$UMBRIEL" presentation-scene-probe cancel
"$UMBRIEL" clock-advance 1600
"$UMBRIEL" settle
if [[ -n ${UMBRIEL_PRESENTATION_ARTIFACTS:-} ]]; then
  mkdir -p "$UMBRIEL_PRESENTATION_ARTIFACTS/capture-${UMBRIEL_PRESENTATION_FILTERED:-0}"
  cp "$IMAGE" "$UMBRIEL_PRESENTATION_ARTIFACTS/capture-${UMBRIEL_PRESENTATION_FILTERED:-0}/capture.png"
fi
echo "Retained desktop presentation preserved the configured capture role while native effect entries were suppressed"
