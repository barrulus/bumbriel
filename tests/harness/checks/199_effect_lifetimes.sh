#!/usr/bin/env bash
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"
cat > "$UMBRIEL_RUNTIME_DIR/content.glsl" <<'GLSL'
vec4 postprocess(vec3 p) { vec4 c = tex2D_screen(p.xy); return vec4(0.0, c.r, 0.0, c.a); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/open.glsl" <<'GLSL'
vec4 animation(vec2 p) { vec4 c = umbriel_sample(p); return vec4(0.0, 0.0, c.g, c.a); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/close.glsl" <<'GLSL'
vec4 animation(vec2 p) { vec4 c = umbriel_sample(p); return vec4(c.b, 0.0, 0.0, c.a); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation]
duration_ms = 2000
curve = "linear"
[render.effects]
in_capture = true
[appearance]
effects = ["lifetime"]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[effects.lifetime.content]
passes = [{shader = "content.glsl"}]
[effects.lifetime.open]
passes = [{shader = "open.glsl"}]
[effects.lifetime.close]
passes = [{shader = "close.glsl"}]
[[window_rule]]
match.title = "^effect-lifetime$"
default_floating = true
default_floating_size_px = {width = 480, height = 300}
default_position = {x = 200, y = 150, anchor = "top_left"}
TOML
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-freeze
FILL_COLOR=0xFFFF0000 "$UMBRIEL_UNMAP_CLIENT" effect-lifetime 480 300 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
capture() {
  grim "$UMBRIEL_RUNTIME_DIR/frame.png"
  "$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/frame.png" bbox "$1"
}
"$UMBRIEL" clock-advance 100 > /dev/null
[[ $(capture 'b > 0.9 && r < 0.1 && g < 0.1') == '200 150 480 300' ]]
"$UMBRIEL" effects --json | jq -e '.windows[0].scopes.open | .active.source.effect == "lifetime" and .retained == false' >/dev/null
cat > "$UMBRIEL_RUNTIME_DIR/open.glsl" <<'GLSL'
vec4 animation(vec2 p) { return vec4(1.0, 1.0, 0.0, 1.0); }
GLSL
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-advance 100 > /dev/null
[[ $(capture 'b > 0.9 && r < 0.1 && g < 0.1') == '200 150 480 300' ]]
"$UMBRIEL" effects --json | jq -e '.windows[0].scopes.open | .active.source.effect == "lifetime" and .retained == true' >/dev/null
"$UMBRIEL" msg window-close > /dev/null
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 0 ]] && break
  sleep 0.025
done
"$UMBRIEL" clock-advance 100 > /dev/null
[[ $(capture 'r > 0.9 && b < 0.1 && g < 0.1') == '200 150 480 300' ]]
cat > "$UMBRIEL_RUNTIME_DIR/close.glsl" <<'GLSL'
vec4 animation(vec2 p) { return vec4(0.0, 1.0, 0.0, 1.0); }
GLSL
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-advance 100 > /dev/null
[[ $(capture 'r > 0.9 && b < 0.1 && g < 0.1') == '200 150 480 300' ]]
"$UMBRIEL" clock-advance 2200 > /dev/null
"$UMBRIEL" settle
echo 'content processing survived lifecycle capture; opening and closing programs survived reload'
