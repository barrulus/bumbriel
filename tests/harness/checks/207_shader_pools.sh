#!/usr/bin/env bash
# Pool assignments survive focus/reload, release on close, and drive actual pixels.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/pools.png"
for color in red green blue; do
  case $color in
    red) rgb='1,0,0' ;;
    green) rgb='0,1,0' ;;
    blue) rgb='0,0,1' ;;
  esac
  printf 'vec4 ring_color(vec2 p) { return vec4(%s,1); }\n' "$rgb" > "$UMBRIEL_RUNTIME_DIR/$color.glsl"
done
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation]
enabled = false
[layout]
gap = 30
[appearance]
border_width = 6
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[shaders]
in_capture = true
window = "grayscale"
window_pool = "reading"
[shaders.pool.reading]
presets = ["invert", "grayscale"]
[shaders.border.red]
shader = "red.glsl"
[shaders.border.green]
shader = "green.glsl"
[shaders.border.blue]
shader = "blue.glsl"
[shaders.pool.rings]
scope = "border"
presets = ["red", "green", "blue"]
[[window_rule]]
match.title = "^pool-"
[window_rule.border_shader]
pool = "rings"
TOML
"$UMBRIEL" msg config-reload >/dev/null
spawn() {
  FILL_COLOR=0xFF204080 "$UMBRIEL_UNMAP_CLIENT" "pool-$1" 400 300 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  client_pid=$!
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "pool-$1" '.[] | select(.title == $title)')
    [[ -n $window ]] && break
    sleep 0.025
  done
  [[ -n $window ]]
}
expect_ring() {
  local title=$1 want=$2
  window=$("$UMBRIEL" windows --json | jq -c --arg title "pool-$title" '.[] | select(.title == $title)')
  local id
  id=$(jq -r .id <<< "$window")
  "$UMBRIEL" msg "window-focus:$id" >/dev/null
  for _ in $(seq 40); do
    window=$("$UMBRIEL" windows --json | jq -c --arg title "pool-$title" '.[] | select(.title == $title)')
    x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
    y=$(jq -r '.y - 3' <<< "$window")
    grim "$IMAGE"
    read -r r g b < <(magick "$IMAGE" -crop "1x1+$x+$y" -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)] %[fx:round(mean.b*255)]\n' info:)
    case $want in
      red) (( r > 240 && g < 10 && b < 10 )) && return ;;
      green) (( r < 10 && g > 240 && b < 10 )) && return ;;
      blue) (( r < 10 && g < 10 && b > 240 )) && return ;;
    esac
    sleep 0.025
  done
  echo "$title expected $want ring, got $r $g $b at $x,$y"
  exit 1
}
spawn one
expect_ring one red
spawn two
two_pid=$client_pid
expect_ring two green
expect_ring one red
# A real config change reorders the pool without changing surviving assignments.
sed -i 's/presets = \["red", "green", "blue"\]/presets = ["blue", "green", "red"]/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload >/dev/null
expect_ring one red
expect_ring two green
spawn three
expect_ring three blue
kill "$two_pid"
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq '[.[] | select(.title == "pool-two")] | length') == 0 ]] && break
  sleep 0.025
done
spawn four
expect_ring four green
"$UMBRIEL" msg 'shader:border cycle' >/dev/null
expect_ring four red
"$UMBRIEL" msg 'shader:border toggle' >/dev/null
"$UMBRIEL" msg 'shader:border toggle' >/dev/null
expect_ring four red
# Cycling resumes after a directly selected preset, not the old allocation cursor.
"$UMBRIEL" msg 'shader:border blue' >/dev/null
expect_ring four blue
"$UMBRIEL" msg 'shader:border cycle' >/dev/null
expect_ring four green
"$UMBRIEL" msg 'shader:border cycle' >/dev/null
expect_ring four red
# Window cycle uses precisely the configured order and wraps without a default step.
expect_content() {
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "pool-four")')
  x=$(jq -r '.x + (.w / 2 | floor)' <<< "$window")
  y=$(jq -r '.y + (.h / 2 | floor)' <<< "$window")
  grim "$IMAGE"
  read -r r g b < <(magick "$IMAGE" -crop "1x1+$x+$y" -format '%[fx:round(mean.r*255)] %[fx:round(mean.g*255)] %[fx:round(mean.b*255)]\n' info:)
  (( r >= $1-3 && r <= $1+3 && g >= $2-3 && g <= $2+3 && b >= $3-3 && b <= $3+3 )) || {
    echo "expected content $*, got $r $g $b"; exit 1;
  }
}
"$UMBRIEL" msg 'shader:window cycle' >/dev/null
expect_content 223 191 127
"$UMBRIEL" msg 'shader:window cycle' >/dev/null
expect_content 62 62 62
"$UMBRIEL" msg 'shader:window cycle' >/dev/null
expect_content 223 191 127
if "$UMBRIEL" msg 'shader:window cycle:missing' >/dev/null 2>&1; then
  echo 'unknown pool action unexpectedly succeeded'; exit 1
fi
expect_content 223 191 127
# Removing the active ring entry reallocates to a remaining member.
sed -i 's/presets = \["blue", "green", "red"\]/presets = ["blue", "green"]/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload >/dev/null
expect_ring four green
# Existing assigned blue remains stable across the removal.
expect_ring three blue
echo 'ring allocation, focus/reload stability, release, cycling and ordered window pools verified'
