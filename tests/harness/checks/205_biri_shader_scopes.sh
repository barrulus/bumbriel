#!/usr/bin/env bash
# harness: outputs=2
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/scopes.png"
cat > "$UMBRIEL_RUNTIME_DIR/half.glsl" <<'GLSL'
vec4 postprocess(vec3 p) { vec4 c=tex2D_screen(p.xy); return vec4(c.rgb*0.5,c.a); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/swap.glsl" <<'GLSL'
vec4 postprocess(vec3 p) { vec4 c=tex2D_screen(p.xy); return vec4(c.b,c.g,c.r,c.a); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation]
enabled = false
[appearance]
corner_radius = 0
[appearance.shadow]
enabled = false
[shaders]
in_capture = true
global = "swap"
[[shaders.preset.half.passes]]
shader = "half.glsl"
[shaders.preset.half]
scope = "output"
[[shaders.preset.swap.passes]]
shader = "swap.glsl"
[shaders.preset.swap]
scope = "global"
[[shaders.preset."cursor.swap".passes]]
shader = "swap.glsl"
[[shaders.preset."screen.half".passes]]
shader = "half.glsl"
[[shaders.region]]
output = "HEADLESS-1"
preset = "half"
x = 0
y = 0
width = 3000
height = 3000
[output."HEADLESS-1"]
position = [0, 0]
shader = "invert"
[[window_rule]]
match.title = "scope-window"
default_output = "HEADLESS-1"
shader = "invert"
TOML
"$UMBRIEL" msg config-reload >/dev/null
FILL_COLOR=0xFF204080 "$UMBRIEL_UNMAP_CLIENT" scope-window 500 400 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "scope-window")')
  [[ -n $window ]] && (( $(jq -r .y <<< "$window") >= 6 )) && break
  sleep 0.025
done
[[ -n $window ]]
x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
y=$(jq -r '.y + (.h / 2 | floor)' <<< "$window")
expect() {
  grim -o HEADLESS-1 "$IMAGE"
  read -r r g b < <(magick "$IMAGE" -crop "2x2+$x+$y" -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)] %[fx:round(mean.b*255)]\n' info:)
  (( r >= $1-3 && r <= $1+3 && g >= $2-3 && g <= $2+3 && b >= $3-3 && b <= $3+3 )) || {
    echo "expected $*; got $r $g $b"; exit 1;
  }
}
expect 191 159 143
"$UMBRIEL" msg 'shader:global off' >/dev/null
expect 143 159 191
"$UMBRIEL" msg 'shader:output off HEADLESS-1' >/dev/null
expect 112 96 64
"$UMBRIEL" msg 'shader:window off' >/dev/null
expect 16 32 64
"$UMBRIEL" msg 'shader:window toggle' >/dev/null
expect 112 96 64
"$UMBRIEL" msg 'shader:output invert HEADLESS-2' >/dev/null
grim -o HEADLESS-2 "$IMAGE"
red=$(magick "$IMAGE" -crop 2x2+20+20 -format '%[fx:round(mean.r*255)]' info:)
(( red > 245 )) || { echo "targeted output action failed: $red"; exit 1; }
expect 112 96 64
"$UMBRIEL" msg 'shader:global cycle' >/dev/null
expect 64 96 112
# A broken pass drops the chain until the watcher reloads it.
printf '%s\n' 'broken shader' > "$UMBRIEL_RUNTIME_DIR/half.glsl"
sleep 0.3
expect 127 191 223
cat > "$UMBRIEL_RUNTIME_DIR/half.glsl" <<'GLSL'
vec4 postprocess(vec3 p) { vec4 c=tex2D_screen(p.xy); return vec4(c.rgb*0.5,c.a); }
GLSL
sleep 0.3
expect 64 96 112
"$UMBRIEL" msg 'shader:screen cycle' >/dev/null
expect 56 48 32
"$UMBRIEL" msg 'shader:cursor cycle' >/dev/null
expect 64 96 112
echo "window/region/output/global ordering, target isolation, runtime selection and source reload verified"
