#!/usr/bin/env bash
# The effect registry compiles exactly the presets something enabled references,
# keeps a compiled or failed program across reloads that leave its source alone,
# and reports a referenced preset that fails to compile.
set -euo pipefail
readonly BASE="$UMBRIEL_RUNTIME_DIR/registry-base.toml"
readonly USED="$UMBRIEL_RUNTIME_DIR/used.glsl"
readonly BROKEN="$UMBRIEL_RUNTIME_DIR/broken.glsl"
readonly IDLE="$UMBRIEL_RUNTIME_DIR/idle.glsl"
readonly SHOWN="$UMBRIEL_RUNTIME_DIR/shown.glsl"
cp "$UMBRIEL_CONFIG" "$BASE"
echo 'vec4 screen(vec2 uv) { return umbriel_sample(uv); }' > "$USED"
echo 'this is not GLSL' > "$BROKEN"
echo 'vec4 animation(vec2 uv) { return umbriel_sample(uv); }' > "$IDLE"
echo 'vec4 animation(vec2 uv) { return umbriel_sample(uv) * umbriel_clamped_progress; }' > "$SHOWN"

write_config() {
  cp "$BASE" "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<EOF

[effects]
screen = "used"
[effects.preset.used]
kind = "screen"
shader = "$USED"
[effects.preset.broken]
kind = "screen"
shader = "$BROKEN"
[effects.preset.idle]
kind = "animation"
shader = "$IDLE"
[effects.preset.shown]
kind = "animation"
shader = "$SHOWN"
[animation.layers]
enabled = false
effect = "idle"
EOF
  printf '%s\n' "$@" >> "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
}

compiled() { grep -c "Compiling $1 shader: $2\$" "$UMBRIEL_LOG" || true; }
diagnosed() { grep -c "effect preset '$1' ($2) failed to compile; rendering plainly" "$UMBRIEL_LOG" || true; }
expect() {
  if [[ $2 != "$3" ]]; then
    echo "$1: got $2, expected $3"
    exit 1
  fi
}

write_config
expect "referenced preset compilations" "$(compiled screen "$USED")" 1
expect "unreferenced preset compilations" "$(compiled screen "$BROKEN")" 0
expect "unreferenced preset diagnostics" "$(diagnosed broken screen)" 0

write_config '[output.HEADLESS-1]' 'screen_effect = "broken"'
expect "output-rule preset compilations" "$(compiled screen "$BROKEN")" 1
expect "failed preset diagnostics" "$(diagnosed broken screen)" 1
expect "retained preset compilations after reload" "$(compiled screen "$USED")" 1

write_config '[animation]' 'duration_ms = 300' '[output.HEADLESS-1]' 'screen_effect = "broken"'
expect "failed preset compilations after an unrelated reload" "$(compiled screen "$BROKEN")" 1
expect "failed preset diagnostics after an unrelated reload" "$(diagnosed broken screen)" 1

cp "$USED" "$BROKEN"
write_config '[animation]' 'duration_ms = 300' '[output.HEADLESS-1]' 'screen_effect = "broken"'
expect "repaired preset compilations" "$(compiled screen "$BROKEN")" 2
expect "repaired preset diagnostics" "$(diagnosed broken screen)" 1

expect "compilations of a preset only a disabled event names" "$(compiled animation "$IDLE")" 0
expect "compilations of a preset no event names" "$(compiled animation "$SHOWN")" 0

write_config '[animation.windows_move]' 'effect = "shown"'
expect "compilations of a preset an enabled event names" "$(compiled animation "$SHOWN")" 1
echo "only enabled references compiled, programs and failures retained across reload, repairs recompiled"
