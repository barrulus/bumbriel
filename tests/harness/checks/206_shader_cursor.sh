#!/usr/bin/env bash
# harness: outputs=2
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/cursor-effect.png"
readonly POINTER="${UMBRIEL_POINTER_CLIENT:-./build-debug/tests/pointer-client}"
cat > "$UMBRIEL_RUNTIME_DIR/pointer.glsl" <<'GLSL'
vec4 postprocess(vec3 p) {
  vec2 d=p.xy*umbriel_output_size-umbriel_cursor;
  if (abs(d.x)<20.0 && abs(d.y)<20.0) return vec4(d.x<0.0 ? vec3(1,0,0) : vec3(0,1,0),1);
  return tex2D_screen(p.xy);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation]
enabled = false
[output.HEADLESS-1]
position = [0, 0]
scale = 1.25
transform = "90"
[output.HEADLESS-2]
position = [576, 0]
scale = 2
[render.effects]
in_capture = true
[appearance]
effects = ["pointer"]
[effects.pointer.overlay]
cursor_radius = 40
[[effects.pointer.overlay.passes]]
shader = "pointer.glsl"
TOML
"$UMBRIEL" msg config-reload >/dev/null
move() { "$POINTER" 1216 1024 move "$1" "$2" pause 80 >/dev/null; }
sample() {
  magick "$IMAGE" -crop "2x2+$1+$2" -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)]\n' info:
}
move 200 200
grim -s 1 -o HEADLESS-1 "$IMAGE"
read -r r g < <(sample 195 200)
(( r>245 && g<5 )) || { echo "rotated cursor left wrong: $r $g"; exit 1; }
read -r r g < <(sample 205 200)
(( r<5 && g>245 )) || { echo "rotated cursor right wrong: $r $g"; exit 1; }
move 400 500
grim -s 1 -o HEADLESS-1 "$IMAGE"
read -r r g < <(sample 200 200)
(( r<5 && g<5 )) || { echo "old cursor footprint survived movement"; exit 1; }
read -r r g < <(sample 405 500)
(( r<5 && g>245 )) || { echo "new cursor footprint missing"; exit 1; }
move 800 200
grim -s 1 -o HEADLESS-1 "$IMAGE"
read -r r g < <(sample 405 500)
(( r<5 && g<5 )) || { echo "cursor effect survived leaving its output"; exit 1; }
grim -s 1 -o HEADLESS-2 "$IMAGE"
read -r r g < <(sample 229 200)
(( r<5 && g>245 )) || { echo "second output cursor coordinates wrong: $r $g"; exit 1; }
echo "cursor-local pixels, rotated fractional coordinates, motion damage and output crossing verified"
