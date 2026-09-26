#!/usr/bin/env bash
# Reload behaviour of effects: a missing shader renders plainly and recovers once the file appears; unknown and
# mismatched names are reported and leave the setting off; a [colors] change reaches a palette shader without a
# recompile.
set -euo pipefail
readonly IMAGE="$UMBRIEL_RUNTIME_DIR/effect-reload.png"
readonly LOG_MARK=$(($(wc -l < "$UMBRIEL_LOG") + 1))
cat > "$UMBRIEL_RUNTIME_DIR/palette.glsl" <<'GLSL'
// umbriel_scale keeps the palette index a runtime value (always 0 here) rather than a shader-compile-time constant.
vec4 border(vec2 uv) { return umbriel_palette_at(umbriel_scale * 1e-9); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<'EOF'

[animation]
enabled = false
[appearance]
border_width = 6
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[colors]
backdrop = "#000000FF"
accent_primary = "#00FF00FF"
[colors.border]
focused = "#FFFFFFFF"
[effects]
border = "later"
window = "later"
screen = "nope"
[effects.preset.later]
kind = "border"
shader = "later.glsl"
[[window_rule]]
match.title = "^reload$"
default_floating = true
EOF
"$UMBRIEL" msg config-reload > /dev/null
for _ in $(seq 50); do
  grep -q "config reloaded" <(tail -n +"$LOG_MARK" "$UMBRIEL_LOG") && break
  sleep 0.02
done
if ! tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -q "ignoring effects.window (effect 'later' is a border preset, not a window preset)"; then
  echo "a mismatched preset kind was not reported"
  exit 1
fi
if ! tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -q "ignoring effects.screen (unknown effect 'nope')"; then
  echo "an unknown preset name was not reported"
  exit 1
fi
# A missing shader file is not logged by the running compositor, only offline validation reports it. `validate`
# exits non-zero whenever any diagnostic fires, so its output is captured before checking for this one.
validation=$("$UMBRIEL" validate -c "$UMBRIEL_CONFIG" 2>&1 || true)
if ! grep -q "cannot read shader file" <<< "$validation"; then
  echo "the missing shader file was not reported"
  exit 1
fi
FILL_COLOR=0xFF0000FF "$UMBRIEL_UNMAP_CLIENT" reload 300 200 > "$UMBRIEL_RUNTIME_DIR/client.log" 2>&1 &
for _ in $(seq 80); do
  window=$("$UMBRIEL" windows --json | jq -c '.[] | select(.title == "reload")')
  [[ -n $window ]] && break
  sleep 0.025
done
[[ -n $window ]]
"$UMBRIEL" settle > /dev/null
read -r x y w _ < <(jq -r '"\(.x) \(.y) \(.w) \(.h)"' <<< "$window")
probe() { "$UMBRIEL_PIXEL_PROBE" "$IMAGE" pixel "$((x + w / 2))" "$((y - 3))"; }
grim "$IMAGE"
read -r r g b < <(probe)
if (( r < 240 || g < 240 || b < 240 )); then
  echo "an inert preset did not leave the plain white ring: $r $g $b"
  exit 1
fi
# The missing file appears: the watcher reloads and the ring turns accent_primary green.
cp "$UMBRIEL_RUNTIME_DIR/palette.glsl" "$UMBRIEL_RUNTIME_DIR/later.glsl"
sed -i 's/^shader = "later.glsl"$/shader = "later.glsl"\npalette = true/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r r g b < <(probe)
if (( g < 240 || r > 15 || b > 15 )); then
  echo "the preset did not recover once its shader file appeared: $r $g $b"
  exit 1
fi
# A [colors] change updates the palette uniform without recompiling.
compiles=$(tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -c "Compiling border shader" || true)
sed -i 's/^accent_primary = "#00FF00FF"$/accent_primary = "#0000FFFF"/' "$UMBRIEL_CONFIG"
"$UMBRIEL" msg config-reload > /dev/null
"$UMBRIEL" settle > /dev/null
grim "$IMAGE"
read -r r g b < <(probe)
if (( b < 240 || g > 15 )); then
  echo "a colour change did not reach the palette uniform: $r $g $b"
  exit 1
fi
if (( $(tail -n +"$LOG_MARK" "$UMBRIEL_LOG" | grep -c "Compiling border shader" || true) != compiles )); then
  echo "a colour change recompiled the preset"
  exit 1
fi
echo "inert preset recovery, reference diagnostics, and palette updates without recompilation verified"
