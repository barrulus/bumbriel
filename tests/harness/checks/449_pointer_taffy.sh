#!/usr/bin/env bash
# harness: outputs=1
# Taffy hangs below its input rectangle during a horizontal drag, then settles.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation.windows_in]
enabled = false
[animation.windows_out]
enabled = false
[animation.windows_move]
drag_physics = false
[effects.taffy.drag]
coupling = 48
damping = 4.8
stiffness_gradient = -0.55
lag_gradient = 0.9
downward_pull = 18
[appearance]
effects = ["taffy"]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[[window_rule]]
match.title = "^pointer-taffy$"
default_floating = true
default_floating_size_px = { width = 480, height = 300 }
default_position = { x = 200, y = 150, anchor = "top_left" }
TOML
"$UMBRIEL" msg config-reload > /dev/null
FILL_COLOR=0xFFFF0000 "$UMBRIEL_UNMAP_CLIENT" pointer-taffy 480 300 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
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
pointer_hold 1280 720 move 440 174 mod logo press 272 move 470 174 -- release 272 mod none
for _ in $(seq 3); do "$UMBRIEL" clock-advance 100 > /dev/null; done
read -r x y w h < <(capture drooping)
((y + h > 470)) || { echo "taffy did not droop below its bottom edge: $x $y $w $h"; exit 1; }
before="$x $y $w $h"
sed -i 's/downward_pull = 18/downward_pull = 12/; s/damping = 4.8/damping = 6.0/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" effects --json | jq -e '.windows[0].scopes.drag.pipeline.physics | .downward_pull == 12 and .damping == 6' >/dev/null
[[ $(capture reloaded) == "$before" ]]
pointer_release
for _ in $(seq 70); do "$UMBRIEL" clock-advance 100 > /dev/null; done
"$UMBRIEL" settle
[[ $(capture settled) == '230 150 480 300' ]]
echo "taffy rendered a hanging bottom during horizontal movement and settled back to its rectangle"
