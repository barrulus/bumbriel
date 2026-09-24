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
effects = ["swap"]
corner_radius = 0
[appearance.shadow]
enabled = false
[render.effects]
in_capture = true
[effects.half.screen]
passes = [{shader = "half.glsl"}]
[effects.swap.overlay]
passes = [{shader = "swap.glsl"}]
[effects.invert.content]
passes = [{builtin = "invert"}]
[effects.invert.screen]
passes = [{builtin = "invert"}]
[effects.screens]
choose = ["half"]
[effects.cursors]
choose = ["swap"]
[[effect_region]]
name = "test-region"
output = "HEADLESS-1"
effects = ["half"]
x = 0
y = 0
width = 3000
height = 3000
[output."HEADLESS-1"]
position = [0, 0]
effects = ["invert"]
[[window_rule]]
match.title = "scope-window"
default_output = "HEADLESS-1"
effects = ["invert"]
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
# Polls the window centre until it shows the expected colour: IPC actions apply on the next frame and the shader
# watcher reloads a changed source file on its own schedule.
expect() {
  local r g b
  for _ in $(seq 80); do
    grim -o HEADLESS-1 "$IMAGE"
    read -r r g b < <(magick "$IMAGE" -crop "2x2+$x+$y" -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)] %[fx:round(mean.b*255)]\n' info:)
    (( r >= $1-3 && r <= $1+3 && g >= $2-3 && g <= $2+3 && b >= $3-3 && b <= $3+3 )) && return 0
    sleep 0.025
  done
  echo "expected $*; got $r $g $b"
  exit 1
}
expect 191 159 143
"$UMBRIEL" msg 'effect:global off --scope overlay' >/dev/null
expect 143 159 191
"$UMBRIEL" msg 'effect:output off --target HEADLESS-1 --scope screen' >/dev/null
expect 112 96 64
"$UMBRIEL" msg 'effect:window off --scope content' >/dev/null
expect 16 32 64
"$UMBRIEL" msg 'effect:window toggle --scope content' >/dev/null
expect 112 96 64
"$UMBRIEL" msg 'effect:output set invert --target HEADLESS-2 --scope screen' >/dev/null
grim -o HEADLESS-2 "$IMAGE"
red=$(magick "$IMAGE" -crop 2x2+20+20 -format '%[fx:round(mean.r*255)]' info:)
(( red > 245 )) || { echo "targeted output action failed: $red"; exit 1; }
expect 112 96 64
"$UMBRIEL" msg 'effect:global cycle cursors --scope overlay' >/dev/null
expect 64 96 112
# An invalid generation retains the selected pipelines.
generation=$("$UMBRIEL" effects --json | jq .generation)
printf '%s\n' 'broken shader' > "$UMBRIEL_RUNTIME_DIR/half.glsl"
"$UMBRIEL" msg config-reload >/dev/null
[[ $("$UMBRIEL" effects --json | jq .generation) == "$generation" ]]
expect 64 96 112
cat > "$UMBRIEL_RUNTIME_DIR/half.glsl" <<'GLSL'
vec4 postprocess(vec3 p) { vec4 c=tex2D_screen(p.xy); return vec4(c.rgb*0.5,c.a); }
GLSL
expect 64 96 112
"$UMBRIEL" msg 'effect:output cycle screens --target HEADLESS-1 --scope screen' >/dev/null
expect 32 48 56
"$UMBRIEL" msg 'effect:global cycle cursors --scope overlay' >/dev/null
expect 32 48 56
"$UMBRIEL" msg 'effect:global off --scope overlay' >/dev/null
expect 56 48 32
echo "window/region/output/global ordering, target isolation, runtime selection and source reload verified"
