#!/usr/bin/env bash
# Effect-only frames follow the animation clock, not real time: a frozen clock renders the same instant across
# frames and through a close snapshot, a session lock suspends the effect ledger despite the lock client's own
# redraws, and effects.max_fps caps the render rate to its own due-frame interval.
set -euo pipefail

readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-border-frames.png"
cat > "$UMBRIEL_RUNTIME_DIR/clock.glsl" <<'GLSL'
// The red channel encodes the current effect time, so a frozen clock renders an unchanging ring and an advancing
// clock renders a different one.
vec4 border(vec2 uv) { return vec4(fract(umbriel_time), 0.0, 0.0, 1.0); }
GLSL

readonly BASE="$UMBRIEL_RUNTIME_DIR/frames-base.toml"
cp "$UMBRIEL_CONFIG" "$BASE"

frames() { "$UMBRIEL" effect-frames --json | jq -r '.outputs[0].effect_frames'; }
eligible() { "$UMBRIEL" effect-frames --json | jq -r '.outputs[0].eligible'; }
ring_pixel() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$1" "$2"; }

open_window() {
  FILL_COLOR=0xFF00FF00 "$UMBRIEL_UNMAP_CLIENT" frame-one 300 200 > "$UMBRIEL_RUNTIME_DIR/frame-one.log" 2>&1 &
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "frame-one")')
    [[ -n $window ]] && break
    sleep 0.025
  done
  [[ -n $window ]]
  "$UMBRIEL" settle > /dev/null
  read -r x y w h id < <(jq -r '"\(.x) \(.y) \(.w) \(.h) \(.id)"' <<< "$window")
}

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false
[appearance]
border_width = 4
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[effects]
border = "clock"
[effects.preset.clock]
kind = "border"
shader = "clock.glsl"
padding = 20
[effects.preset.clock.light]
spread = 40
intensity = 4
threshold = 0.2
[[window_rule]]
match.title = "^frame-one$"
default_floating = true
default_position = { x = 100, y = 100, anchor = "top_left" }
EOF
"$UMBRIEL" msg config-reload > /dev/null
open_window
ring_x=$((x + w / 2))
ring_y=$((y - 12))
light_x=$((x + w / 2))
light_y=$((y - 50))

# 1. A frozen clock renders the same instant across frames; only clock-advance moves it.
"$UMBRIEL" clock-freeze
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r ring_r1 ring_g1 ring_b1 < <(ring_pixel "$ring_x" "$ring_y")
read -r light_r1 light_g1 light_b1 < <(ring_pixel "$light_x" "$light_y")
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r ring_r2 ring_g2 ring_b2 < <(ring_pixel "$ring_x" "$ring_y")
read -r light_r2 light_g2 light_b2 < <(ring_pixel "$light_x" "$light_y")
if [[ "$ring_r1 $ring_g1 $ring_b1" != "$ring_r2 $ring_g2 $ring_b2" ]]; then
  echo "a frozen clock's ring pixel changed between two frames: $ring_r1 $ring_g1 $ring_b1 -> $ring_r2 $ring_g2 $ring_b2"
  exit 1
fi
if [[ "$light_r1 $light_g1 $light_b1" != "$light_r2 $light_g2 $light_b2" ]]; then
  echo "a frozen clock's light pixel changed between two frames: $light_r1 $light_g1 $light_b1 -> $light_r2 $light_g2 $light_b2"
  exit 1
fi
"$UMBRIEL" clock-advance 500
grim "$IMAGE"
read -r ring_r3 ring_g3 ring_b3 < <(ring_pixel "$ring_x" "$ring_y")
if [[ "$ring_r1 $ring_g1 $ring_b1" == "$ring_r3 $ring_g3 $ring_b3" ]]; then
  echo "advancing the clock 500ms did not change the time-keyed ring pixel: stayed at $ring_r1 $ring_g1 $ring_b1"
  exit 1
fi

# 2. A close snapshot under a frozen clock keeps rendering that frozen instant, and the frozen clock still produces
# no effect-only frames while the compositor stays up.
# The reconfigure below flips [animation] from off to on, which can start the border's own focus-color transition;
# resuming the clock first lets that transition (and the reload itself) settle before this check freezes it again.
"$UMBRIEL" clock-resume
cat "$BASE" > "$UMBRIEL_CONFIG"
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = true
[animation.windows_in]
enabled = false
[animation.windows_move]
enabled = false
[animation.windows_out]
enabled = true
style = "fade"
duration_ms = 5000
curve = "linear"
[appearance]
border_width = 4
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
[effects]
border = "clock"
[effects.preset.clock]
kind = "border"
shader = "clock.glsl"
padding = 20
[[window_rule]]
match.title = "^frame-one$"
default_floating = true
default_position = { x = 100, y = 100, anchor = "top_left" }
EOF
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
"$UMBRIEL" clock-freeze
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r ring_r5 ring_g5 ring_b5 < <(ring_pixel "$ring_x" "$ring_y")
before_close=$(frames)
"$UMBRIEL" msg "window-close:$id" > /dev/null
# The close animation is now running but frozen; settle would refuse (an active animation never finishes on a
# frozen clock), so this polls the window list instead, which drops the title once the close snapshot takes over.
for _ in $(seq 100); do
  [[ -z $("$UMBRIEL" windows --json | jq -c --arg title frame-one '.[] | select(.title == $title)') ]] && break
  sleep 0.02
done
if [[ -n $("$UMBRIEL" windows --json | jq -c --arg title frame-one '.[] | select(.title == $title)') ]]; then
  echo "the window never left the window list after window-close"
  exit 1
fi
grim "$IMAGE"
read -r ring_r6 ring_g6 ring_b6 < <(ring_pixel "$ring_x" "$ring_y")
if [[ "$ring_r5 $ring_g5 $ring_b5" != "$ring_r6 $ring_g6 $ring_b6" ]]; then
  echo "the close snapshot did not keep the frozen ring image: $ring_r5 $ring_g5 $ring_b5 -> $ring_r6 $ring_g6 $ring_b6"
  exit 1
fi
if (( $(frames) != before_close )); then
  echo "a frozen clock's close snapshot still produced effect-only frames"
  exit 1
fi
if ! "$UMBRIEL" windows > /dev/null; then
  echo "the compositor did not answer after closing a window under a frozen clock"
  exit 1
fi
"$UMBRIEL" clock-advance 5000
"$UMBRIEL" clock-resume
"$UMBRIEL" settle > /dev/null

# 3. A session lock suspends the effect ledger: eligible drops to 0 and effect_frames stops growing despite the
# lock client's own redraws; unlocking restores both.
open_window
ring_x=$((x + w / 2))
ring_y=$((y - 12))
mkfifo "$UMBRIEL_RUNTIME_DIR/lock-control"
exec {lock_fd}<> "$UMBRIEL_RUNTIME_DIR/lock-control"
"$UMBRIEL_LOCK_CLIENT" <&"$lock_fd" > "$UMBRIEL_RUNTIME_DIR/lock-client.log" 2>&1 &
for _ in $(seq 100); do
  grep -q '^locked$' "$UMBRIEL_RUNTIME_DIR/lock-client.log" && break
  sleep 0.05
done
if ! grep -q '^locked$' "$UMBRIEL_RUNTIME_DIR/lock-client.log"; then
  echo "the session never locked: $(cat "$UMBRIEL_RUNTIME_DIR/lock-client.log")"
  exit 1
fi
if [[ $(eligible) != 0 ]]; then
  echo "a session lock did not suspend the effect ledger: eligible=$(eligible)"
  exit 1
fi
before_locked=$(frames)
sleep 0.3 # real time: a locked session must produce no effect-only frames despite the lock client's own redraws
if (( $(frames) != before_locked )); then
  echo "a session lock still produced effect-only frames: $before_locked -> $(frames)"
  exit 1
fi
echo unlock >&"$lock_fd"
for _ in $(seq 100); do
  grep -q '^unlocked$' "$UMBRIEL_RUNTIME_DIR/lock-client.log" && break
  sleep 0.05
done
if ! grep -q '^unlocked$' "$UMBRIEL_RUNTIME_DIR/lock-client.log"; then
  echo "the session never unlocked: $(cat "$UMBRIEL_RUNTIME_DIR/lock-client.log")"
  exit 1
fi
"$UMBRIEL" settle > /dev/null
if [[ $(eligible) -le 0 ]]; then
  echo "unlocking did not restore effect eligibility: eligible=$(eligible)"
  exit 1
fi
before_unlocked=$(frames)
sleep 0.3 # real time: effect-only frames arrive on the output's own timer
if (( $(frames) <= before_unlocked )); then
  echo "unlocking did not restart effect-only frames"
  exit 1
fi

# 4. effects.max_fps caps the render rate to its own due-frame interval: at 2 fps (500ms) two captures 100ms apart
# never differ, but captures 850ms apart do.
sed -i '/^\[effects\]$/a max_fps = 2' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r ring_r7 ring_g7 ring_b7 < <(ring_pixel "$ring_x" "$ring_y")
sleep 0.15
grim "$IMAGE"
read -r ring_r8 ring_g8 ring_b8 < <(ring_pixel "$ring_x" "$ring_y")
if [[ "$ring_r7 $ring_g7 $ring_b7" != "$ring_r8 $ring_g8 $ring_b8" ]]; then
  echo "max_fps = 2 did not cap the render rate: the ring changed within 150ms ($ring_r7 $ring_g7 $ring_b7 -> $ring_r8 $ring_g8 $ring_b8)"
  exit 1
fi
sleep 0.7 # real time: effects.max_fps = 2 allows a due frame after its 500ms interval elapses
grim "$IMAGE"
read -r ring_r9 ring_g9 ring_b9 < <(ring_pixel "$ring_x" "$ring_y")
if [[ "$ring_r7 $ring_g7 $ring_b7" == "$ring_r9 $ring_g9 $ring_b9" ]]; then
  echo "max_fps = 2 never let the ring advance: stayed at $ring_r7 $ring_g7 $ring_b7"
  exit 1
fi
echo "frozen-clock determinism, close-snapshot freezing, lock suspension, and max_fps capping verified"
