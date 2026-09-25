#!/usr/bin/env bash
# The effect registry compiles exactly the presets something references, keeps a
# compiled program across reloads that still reference it, and reports a
# referenced preset that fails to compile.
set -euo pipefail
readonly BASE="$UMBRIEL_RUNTIME_DIR/registry-base.toml"
readonly USED="$UMBRIEL_RUNTIME_DIR/used.glsl"
readonly BROKEN="$UMBRIEL_RUNTIME_DIR/broken.glsl"
cp "$UMBRIEL_CONFIG" "$BASE"
echo 'vec4 screen(vec2 uv) { return umbriel_sample(uv); }' > "$USED"
echo 'this is not GLSL' > "$BROKEN"

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
EOF
  printf '%s\n' "$@" >> "$UMBRIEL_CONFIG"
  "$UMBRIEL" msg config-reload > /dev/null
}

compiled() { grep -c "Compiling screen shader: $1\$" "$UMBRIEL_LOG" || true; }

write_config
if [[ $(compiled "$USED") != 1 ]]; then
  echo "referenced preset compiled $(compiled "$USED") times, expected once"
  exit 1
fi
if [[ $(compiled "$BROKEN") != 0 ]] || grep -q "effect preset 'broken'" "$UMBRIEL_LOG"; then
  echo "an unreferenced preset was compiled"
  exit 1
fi

write_config '[output.HEADLESS-1]' 'screen_effect = "broken"'
if [[ $(compiled "$BROKEN") != 1 ]]; then
  echo "preset referenced by an output rule compiled $(compiled "$BROKEN") times, expected once"
  exit 1
fi
if ! grep -q "effect preset 'broken' (screen) failed to compile; rendering plainly" "$UMBRIEL_LOG"; then
  echo "failed preset compilation produced no diagnostic"
  exit 1
fi
if [[ $(compiled "$USED") != 1 ]]; then
  echo "a reload that still references a preset recompiled it"
  exit 1
fi
echo "only referenced presets compiled, programs retained across reload, failure reported"
