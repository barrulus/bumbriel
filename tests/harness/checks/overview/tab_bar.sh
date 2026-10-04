#!/usr/bin/env bash
# A tab group's card in the overview carries its tab bar, scaled with the card, as the group does on the desktop.
set -euo pipefail

readonly CLIENT="${UMBRIEL_UNMAP_CLIENT:-./build-debug/tests/unmap-client}"
readonly DESKTOP="$UMBRIEL_RUNTIME_DIR/overview-tab-bar-desktop.png"
readonly OVERVIEW="$UMBRIEL_RUNTIME_DIR/overview-tab-bar-overview.png"
# The shown tab's slot is the only magenta on screen: the windows fill red and blue.
readonly MAGENTA='r > 0.9 && g < 0.1 && b > 0.9'

if [[ ! -x $CLIENT ]]; then
  echo "unmap client is not built"
  exit 1
fi

cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false

[appearance.shadow]
enabled = false

[overview]
background_blur = false

[appearance.tab_bar]
style = "indicator"
height = 24

[colors.tab_bar]
background = "#202020FF"
active = "#FF00FFFF"
active_unfocused = "#FF00FFFF"
EOF
"$UMBRIEL" msg config-reload > /dev/null

window_of() { "$UMBRIEL" windows --json | jq -c --arg title "$1" '.[] | select(.title == $title)'; }

spawn() {
  local title=$1 color=$2
  RESIZE_FILL_COLOR="$color" "$CLIENT" "$title" 600 400 > "$UMBRIEL_RUNTIME_DIR/$title.log" 2>&1 &
  for _ in $(seq 80); do
    [[ -n "$(window_of "$title")" ]] && return 0
    sleep 0.025
  done
  echo "window '$title' never appeared: $(cat "$UMBRIEL_RUNTIME_DIR/$title.log")"
  return 1
}

spawn tab-red 0xFFFF0000
spawn tab-blue 0xFF0000FF
"$UMBRIEL" msg window-consume-left > /dev/null
"$UMBRIEL" msg column-toggle-tabbed > /dev/null
"$UMBRIEL" settle

grim "$DESKTOP"
read -r _ _ desktop_w desktop_h < <("$UMBRIEL_PIXEL_PROBE" "$DESKTOP" bbox "$MAGENTA")
if ((desktop_w == 0 || desktop_h == 0)); then
  echo "the tab group drew no active slot on the desktop"
  exit 1
fi

"$UMBRIEL" msg overview-open > /dev/null
"$UMBRIEL" settle
grim "$OVERVIEW"
read -r card_x card_y card_w card_h < <("$UMBRIEL_PIXEL_PROBE" "$OVERVIEW" bbox "$MAGENTA")
if ((card_w == 0 || card_h == 0)); then
  echo "the tab group's overview card has no tab bar"
  exit 1
fi
# Scaled with the card, the slot is narrower than on the desktop: it is the card's copy, not the live bar behind.
if ((card_w >= desktop_w)); then
  echo "the overview slot is ${card_w}px wide at ${card_x},${card_y}, not scaled below the desktop's ${desktop_w}px"
  exit 1
fi

echo "the overview card carried its tab group's bar, scaled with the card"
