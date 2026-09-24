#!/usr/bin/env bash
set -euo pipefail
cat > "$UMBRIEL_RUNTIME_DIR/tint.glsl" <<'GLSL'
uniform vec3 tint;
vec4 postprocess(vec3 p) { return vec4(tint, tex2D_screen(p.xy).a); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[animation]
enabled = false
[appearance]
effects = ["red"]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[render.effects]
in_capture = true
[effects.red.content]
passes = [{shader = "tint.glsl", params = {tint = [1.0, 0.0, 0.0]}}]
[effects.blue.content]
passes = [{shader = "tint.glsl", params = {tint = [0.0, 0.0, 1.0]}}]
[effects.favourites]
choose = ["red", "blue"]
selection = "round_robin"
[[window_rule]]
match.title = "^effect-runtime$"
default_floating = true
default_floating_size_px = {width = 300, height = 200}
default_position = {x = 400, y = 300, anchor = "top_left"}
TOML
"$UMBRIEL" msg config-reload >/dev/null
FILL_COLOR=0xFFFFFFFF "$UMBRIEL_UNMAP_CLIENT" effect-runtime 300 200 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 100); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" settle
id=$("$UMBRIEL" windows --json | jq -r '.[0].id')
inspect() { "$UMBRIEL" effects --window "$id" --json; }
expect() {
  "$UMBRIEL" settle
  grim "$UMBRIEL_RUNTIME_DIR/frame.png"
  local count
  count=$("$UMBRIEL_PIXEL_PROBE" "$UMBRIEL_RUNTIME_DIR/frame.png" count "$1")
  (( count >= 60000 )) || { echo "expected content $1, got $count pixels"; exit 1; }
}
expect 'r > 0.9 && g < 0.1 && b < 0.1'
"$UMBRIEL" msg 'effect:output toggle --scope content' >/dev/null
inspect | jq -e '.windows[0].scopes.content.suppression == "runtime_off"' >/dev/null
expect 'r > 0.9 && g > 0.9 && b > 0.9'
"$UMBRIEL" msg 'effect:window set blue --scope content' >/dev/null
expect 'b > 0.9 && r < 0.1 && g < 0.1'
"$UMBRIEL" msg 'effect:window default --scope content' >/dev/null
expect 'r > 0.9 && g > 0.9 && b > 0.9'
"$UMBRIEL" msg 'effect:output default --scope content' >/dev/null
"$UMBRIEL" msg 'effect:window cycle favourites --scope content' >/dev/null
inspect | jq -e '.windows[0].scopes.content.source | .effect == "blue" and .choice == "favourites"' >/dev/null
expect 'b > 0.9 && r < 0.1 && g < 0.1'
"$UMBRIEL" msg 'effect:window off --scope content' >/dev/null
"$UMBRIEL" msg config-reload >/dev/null
"$UMBRIEL" msg 'effect:window on --scope content' >/dev/null
inspect | jq -e '.windows[0].scopes.content.source.effect == "blue"' >/dev/null
expect 'b > 0.9 && r < 0.1 && g < 0.1'
"$UMBRIEL" msg 'effect:system off' >/dev/null
expect 'r > 0.9 && g > 0.9 && b > 0.9'
"$UMBRIEL" msg 'effect:system on' >/dev/null
expect 'b > 0.9 && r < 0.1 && g < 0.1'
generation=$(inspect | jq .generation)
sed -i 's/tint = \[0.0, 0.0, 1.0\]/missing = [0.0, 0.0, 1.0]/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload >/dev/null
[[ $(inspect | jq .generation) == "$generation" ]]
expect 'b > 0.9 && r < 0.1 && g < 0.1'
sed -i 's/missing = \[0.0, 0.0, 1.0\]/tint = [0.0, 0.0, 1.0]/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload >/dev/null
"$UMBRIEL" msg 'effect:window set blue --scope content' >/dev/null
sed -i '/\[effects.blue.content\]/,+1d; s/choose = \["red", "blue"\]/choose = ["red"]/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload >/dev/null
inspect | jq -e '.windows[0].scopes.content.source.effect == "red"' >/dev/null
expect 'r > 0.9 && g < 0.1 && b < 0.1'
if "$UMBRIEL" effects --window missing --json >/dev/null 2>&1; then exit 1; fi
if "$UMBRIEL" msg 'effect:window set red --scope screen' >/dev/null 2>&1; then exit 1; fi
echo 'runtime precedence, output surface defaults, leases, gates, typed reload rejection and deleted references verified'
