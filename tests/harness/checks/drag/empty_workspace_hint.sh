#!/usr/bin/env bash
# harness: outputs=2
# A window moved over an empty master or dwindle workspace previews the box it will fill: the centered master box in
# center mode, the whole content area in dwindle.
set -euo pipefail
source "$UMBRIEL_HARNESS_LIB"

readonly BTN_LEFT=272
readonly LAYOUT_W=2560
readonly LAYOUT_H=720
readonly SHOT="$UMBRIEL_RUNTIME_DIR/empty-workspace-hint.png"

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[colors]
insert_hint = "#FF0000FF"

[appearance]
drag_opacity = 0.0

[layout]
mode = "master"

[layout.master]
position = "center"
EOF
"$UMBRIEL" msg config-reload > /dev/null

foot --title=empty-hint sh -c 'sleep 120' > /dev/null 2>&1 &
for _ in $(seq 60); do
  [[ $("$UMBRIEL" windows --json | jq 'length') -eq 1 ]] && break
  sleep 0.25
done
"$UMBRIEL" settle

# Mod+drags the window from its center onto the center of the other, empty output and checks the red hint drawn there
# against the output-local box "$2", then drops and checks the window filled that box.
expect_hint() {
  local label=$1 expected=$2 source target target_x from_x from_y to_x drawn
  source=$("$UMBRIEL" windows --json | jq -r '.[0].workspace | split(":")[0]')
  read -r target target_x < <("$UMBRIEL" outputs --json |
    jq -r --arg source "$source" '.[] | select(.name != $source) | "\(.name) \(.position.x)"')
  read -r from_x from_y < <("$UMBRIEL" windows --json | jq -r '.[0] | "\(.x + .w / 2 | round) \(.y + .h / 2 | round)"')
  to_x=$((target_x + 640))
  # Animation time only moves by clock-advance while the hint is sampled.
  "$UMBRIEL" clock-freeze
  pointer_hold "$LAYOUT_W" "$LAYOUT_H" mod logo move "$from_x" "$from_y" press "$BTN_LEFT" \
    move $(((from_x + to_x) / 2)) 360 move "$to_x" 360 -- release "$BTN_LEFT" mod none
  "$UMBRIEL" clock-advance 500 > /dev/null
  grim -o "$target" "$SHOT"
  drawn=$("$UMBRIEL_PIXEL_PROBE" "$SHOT" bbox 'r > 0.8 && g < 0.2 && b < 0.2')
  pointer_release
  "$UMBRIEL" clock-resume
  "$UMBRIEL" settle
  if [[ $drawn != "$expected" ]]; then
    echo "$label: the hint on the empty $target workspace was drawn at '$drawn', expected '$expected'"
    exit 1
  fi
  local x y w h
  read -r x y w h <<< "$expected"
  if ! "$UMBRIEL" windows --json | jq -e --arg target "$target" --argjson x $((target_x + x)) --argjson y "$y" \
    --argjson w "$w" --argjson h "$h" \
    '.[0] | (.workspace | startswith($target + ":")) and .x == $x and .y == $y and .w == $w and .h == $h' > /dev/null; then
    echo "$label: the drop did not fill the hinted box on $target: $("$UMBRIEL" windows --json)"
    exit 1
  fi
}

# Content is 1260x700 at (10, 10). Center mode: available 1236, master 680, side 278, so the master starts at 300.
expect_hint master "300 10 680 700"

sed -i 's/^mode = "master"$/mode = "dwindle"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle
expect_hint dwindle "10 10 1260 700"

echo "empty master and dwindle workspaces previewed the box the dropped window filled"
