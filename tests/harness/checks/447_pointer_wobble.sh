#!/usr/bin/env bash
# harness: outputs=1
# Real held-pointer moves deform the window beyond its box, reverse direction,
# settle after release, and disappear immediately when disabled during a grab.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation.windows_in]
enabled = false
[animation.windows_out]
enabled = false
[animation.windows_move]
wobble = true
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[[window_rule]]
match.title = "^pointer-wobble$"
default_floating = true
default_floating_size_px = { width = 480, height = 300 }
default_position = { x = 200, y = 150, anchor = "top_left" }
TOML
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFFFF0000 "$UMBRIEL_UNMAP_CLIENT" pointer-wobble 480 300 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq 'length') == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze

capture() {
  grim "$UMBRIEL_RUNTIME_DIR/$1.png"
  "$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/$1.png" bbox 'r > 0.5 && g < 0.1 && b < 0.1'
}

[[ $(capture initial) == '200 150 480 300' ]]
# Grip near the top-left, then move right. The body must trail to the left of
# its new x=260 input box; this also proves the expanded rendering quad works.
pointer_hold 1280 720 move 260 210 mod logo press 272 move 320 210 \
  -- move 200 210 mark reversed hold release 272 mod none
read -r x y w h < <(capture right)
((w > 400 && x < 255)) || { echo "right drag did not trail outside its box: $x $y $w $h"; exit 1; }
"$UMBRIEL" clock-advance 16 > /dev/null
pointer_step reversed
read -r x y w h < <(capture left)
((x + w > 625)) || { echo "left drag did not trail right of its box: $x $y $w $h"; exit 1; }
pointer_release
capture released > /dev/null
"$UMBRIEL" clock-advance 100 > /dev/null
capture settling > /dev/null
! cmp -s "$UMBRIEL_RUNTIME_DIR/released.png" "$UMBRIEL_RUNTIME_DIR/settling.png"
for _ in $(seq 30); do "$UMBRIEL" clock-advance 100 > /dev/null; done
"$UMBRIEL" settle
[[ $(capture settled) == '140 150 480 300' ]]

# Reloading the opt-out during a held grab must restore the ordinary rectangle.
pointer_hold 1280 720 move 200 210 mod logo press 272 move 260 210 -- release 272 mod none
sed -i 's/wobble = true/wobble = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-advance 16 > /dev/null
[[ $(capture disabled) == '200 150 480 300' ]]
pointer_release

# Closing the owner while it is grabbed must release the simulation and input.
sed -i 's/wobble = false/wobble = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
pointer_hold 1280 720 move 260 210 mod logo press 272 move 300 210 -- release 272 mod none
"$UMBRIEL" msg window-close > /dev/null
pointer_release
"$UMBRIEL" clock-advance 100 > /dev/null
"$UMBRIEL" settle
[[ $("$UMBRIEL" windows --json | jq 'length') == 0 ]]
echo "pointer wobble followed direction, rendered outside the window box, settled, disabled and unmapped cleanly"
