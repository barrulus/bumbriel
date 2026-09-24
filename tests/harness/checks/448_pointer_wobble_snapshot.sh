#!/usr/bin/env bash
# harness: outputs=1
# Closing a wobbling opener must retain both the opening effect and frozen
# deformation, and then apply the closing effect to that complete snapshot.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

cat > "$UMBRIEL_RUNTIME_DIR/open.glsl" <<'GLSL'
vec4 animation(vec2 uv) { vec4 c = umbriel_sample(uv); return vec4(0.0, c.r, 0.0, c.a); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/close.glsl" <<'GLSL'
vec4 animation(vec2 uv) {
    vec4 c = umbriel_sample(uv);
    return vec4(0.0, 0.0, c.g, c.a) * (1.0 - umbriel_clamped_progress);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation]
duration_ms = 1600
curve = "linear"
[animation.windows_in]
shader = "open.glsl"
[animation.windows_out]
shader = "close.glsl"
[animation.windows_move]
wobble = true
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[[window_rule]]
match.title = "^wobble-snapshot$"
default_floating = true
default_floating_size_px = { width = 480, height = 300 }
default_position = { x = 200, y = 150, anchor = "top_left" }
TOML
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
FILL_COLOR=0xFFFF0000 "$UMBRIEL_UNMAP_CLIENT" wobble-snapshot 480 300 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq 'length') == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 100 > /dev/null
pointer_hold 1280 720 move 260 210 mod logo press 272 move 320 210 -- release 272 mod none
"$UMBRIEL" msg window-close > /dev/null
pointer_release
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq 'length') == 0 ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 100 > /dev/null
grim "$UMBRIEL_RUNTIME_DIR/closed.png"
blue=$("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/closed.png" count 'b > 0.5 && r < 0.1 && g < 0.1')
((blue > 10000)) || { echo "wobble snapshot lost its opening or closing shader: blue=$blue"; exit 1; }
"$UMBRIEL" clock-advance 2000 > /dev/null
"$UMBRIEL" settle
grim "$UMBRIEL_RUNTIME_DIR/settled.png"
[[ $("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/settled.png" count 'b > 0.5 && r < 0.1 && g < 0.1') == 0 ]]
echo "wobbling close snapshot retained its opening and closing shaders and retired cleanly"
