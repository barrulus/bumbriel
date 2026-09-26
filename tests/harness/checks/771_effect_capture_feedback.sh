#!/usr/bin/env bash
# A feedback animation enclosing a window effect keeps separate histories per composition role: with in_capture = false
# every captured frame excludes the window effect, and the display's feedback matches a run without any capture in
# flight at the same clock steps. Isolated toplevel captures follow the same policy.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-capture-feedback.png"
cat > "$UMBRIEL_RUNTIME_DIR/accumulate.glsl" <<'GLSL'
// Each rendered frame adds 0.01 red to the previous result, well under the 8-bit ceiling for a run's frame count.
vec4 animation(vec2 uv) { vec4 p = umbriel_sample_previous(uv); return vec4(min(p.r + 0.01, 1.0), 0.0, umbriel_sample(uv).b, 1.0); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/green.glsl" <<'GLSL'
vec4 window(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }
GLSL
readonly BASE="$UMBRIEL_RUNTIME_DIR/feedback-base.toml"
cp "$UMBRIEL_CONFIG" "$BASE"
write_config() {
  cat "$BASE" > "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<EOF

[colors]
backdrop = "#000000FF"
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[animation]
duration_ms = 4000
curve = "linear"
[animation.windows_in]
style = "none"
effect = "accumulate"
[effects]
window = "green"
in_capture = $1
[effects.preset.accumulate]
kind = "animation"
shader = "accumulate.glsl"
[effects.preset.green]
kind = "window"
shader = "green.glsl"
[[window_rule]]
match.title = "^feedback$"
default_floating = true
default_position = { x = 200, y = 150, anchor = "top_left" }
EOF
  "$UMBRIEL" msg config-reload > /dev/null
}
spawn() {
  FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" feedback 300 200 > "$UMBRIEL_RUNTIME_DIR/feedback.log" 2>&1 &
  client_pid=$!
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "feedback")')
    [[ -n $window ]] && break
    sleep 0.025
  done
  [[ -n $window ]] || { echo "the feedback client never mapped"; exit 1; }
  read -r x y w h id < <(jq -r '"\(.x) \(.y) \(.w) \(.h) \(.id)"' <<< "$window")
}
# unmap-client keeps its toplevel alive across a compositor close request, so a respawn under the same title must kill
# the process instead.
retire() {
  kill "$client_pid" 2>/dev/null || true
  wait "$client_pid" 2>/dev/null || true
  for _ in $(seq 80); do
    window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "feedback")')
    [[ -z $window ]] && break
    sleep 0.025
  done
  [[ -z $window ]] || { echo "the retired client's toplevel did not disappear"; exit 1; }
}
# The client is blue. The window effect paints it green; the enclosing accumulate program keeps only red (history) and
# blue (its input). So: an unfiltered frame shows blue > 0 (client seen), a filtered one shows blue = 0 (green window
# seen), and red counts how many display frames the history has accumulated.
centre_red() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$((x + w / 2))" "$((y + h / 2))" | cut -d' ' -f1; }
centre_blue() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$((x + w / 2))" "$((y + h / 2))" | cut -d' ' -f3; }
# Reads the display's accumulated red through a reload asserting in_capture = true, plus one clock-advance and one
# grim. That reload is a no-op in the reference run (already true) and flips the policy back in the capture run
# (redamaging and scheduling an extra frame there); either way it never touches display history, so the two
# readings below remain comparable.
read_display_red() {
  sed -i 's/^in_capture = false$/in_capture = true/' "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
  "$UMBRIEL" clock-advance 1 > /dev/null
  grim "$IMAGE"
  centre_red
}

# Reference run with effects included in captures: the same clock steps and the same three grim calls as the capture
# run below (grim itself requests a frame; read_display_red adds one more) so the two runs' display histories stay
# comparable, even though the capture run's own read_display_red does extra work of its own (see above).
write_config true
"$UMBRIEL" clock-freeze
spawn
for _ in $(seq 2); do "$UMBRIEL" clock-advance 100 > /dev/null; done
grim "$IMAGE"
"$UMBRIEL" clock-advance 100 > /dev/null
grim "$IMAGE"
for _ in $(seq 2); do "$UMBRIEL" clock-advance 100 > /dev/null; done
grim "$IMAGE"
reference=$(read_display_red)
retire
"$UMBRIEL" clock-advance 8000 > /dev/null
"$UMBRIEL" settle > /dev/null

# Capture run with effects excluded from captures: the same steps; every grim is a pending capture that stops between
# the calls and restarts.
write_config false
spawn
for _ in $(seq 2); do "$UMBRIEL" clock-advance 100 > /dev/null; done
grim "$IMAGE"
first_capture_blue=$(centre_blue)
"$UMBRIEL" clock-advance 100 > /dev/null
grim "$IMAGE"
second_capture_blue=$(centre_blue)
for _ in $(seq 2); do "$UMBRIEL" clock-advance 100 > /dev/null; done
grim "$IMAGE"
third_capture_blue=$(centre_blue)
if (( first_capture_blue < 200 || second_capture_blue < 200 || third_capture_blue < 200 )); then
  echo "captured frames included the window effect (client blue hidden): $first_capture_blue $second_capture_blue $third_capture_blue"
  exit 1
fi
# The display kept accumulating through its own history; the capture role kept its own.
display_red=$(read_display_red)
if (( display_red < reference - 12 || display_red > reference + 12 )); then
  echo "display feedback diverged from the capture-free run: $display_red vs $reference"
  exit 1
fi
# Isolated toplevel capture: the capture scene holds only client surfaces (no enclosing animation), so it shows the
# green window effect when included and the plain blue client when excluded.
read -r _ g b < <(timeout 10 "$UMBRIEL_TOPLEVEL_CAPTURE_CLIENT" feedback)
(( g > 240 )) || { echo "isolated capture with in_capture = true lacks the window effect: g=$g b=$b"; exit 1; }
sed -i 's/^in_capture = true$/in_capture = false/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" clock-advance 1 > /dev/null
read -r _ g b < <(timeout 10 "$UMBRIEL_TOPLEVEL_CAPTURE_CLIENT" feedback)
(( b > 200 && g < 15 )) || { echo "isolated capture with in_capture = false included the window effect: g=$g b=$b"; exit 1; }
echo "capture-role feedback isolation and isolated toplevel capture policy verified: display $display_red vs $reference"
