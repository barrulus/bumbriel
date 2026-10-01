#!/usr/bin/env bash
# C0 selected-presentation admission with upstream bundled presets. This is a
# rigid-source compatibility check, not a window_scene deformation claim.
set -euo pipefail
readonly BASE="$UMBRIEL_RUNTIME_DIR/admission-base.toml"
cp "$UMBRIEL_CONFIG" "$BASE"
cat > "$UMBRIEL_RUNTIME_DIR/inversion.glsl" <<'GLSL'
vec4 window(vec2 uv) {
  vec4 color = umbriel_sample(uv);
  return vec4(vec3(color.a) - color.rgb, color.a);
}
GLSL

configure() {
  cat "$BASE" > "$UMBRIEL_CONFIG"
  cat >> "$UMBRIEL_CONFIG" <<EOF

[include]
files = ["$UMBRIEL_REPO/examples/effects/border/pulse/effect.toml", "$UMBRIEL_REPO/examples/effects/window/scanlines/effect.toml"]
[animation]
enabled = false
[appearance.shadow]
enabled = true
[appearance.blur]
enabled = $3
optimized = false
[effects]
border = "$1"
window = "$2"
[effects.preset.inversion]
kind = "window"
shader = "inversion.glsl"
[effects.preset.overlay-border]
kind = "border"
shader = "$UMBRIEL_REPO/examples/effects/border/pulse/shader.glsl"
overlay = "scanlines"
[[window_rule]]
match.title = "^admission-window$"
blur = $3
EOF
  "$UMBRIEL" msg config-reload > /dev/null
  "$UMBRIEL" settle
}

check_admission() {
  local name=$1 supported=$2 detail=$3
  local report
  report=$("$UMBRIEL" participant-admission-probe --json)
  jq -e --argjson supported "$supported" --arg detail "$detail" \
    'length == 1 and .[0].visible and .[0].supported == $supported and .[0].detail == $detail' <<< "$report" > /dev/null || {
      echo "$name admission mismatch: $report"
      exit 1
    }
  printf '%s %s\n' "$name" "$report" >> "$UMBRIEL_RUNTIME_DIR/participant-admission.jsonl"
}

configure off off false
FILL_COLOR=0x80008000 "$UMBRIEL_UNMAP_CLIENT" admission-window 400 300 > "$UMBRIEL_RUNTIME_DIR/admission-client.log" 2>&1 &
for _ in $(seq 80); do
  [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
  sleep 0.025
done
"$UMBRIEL" settle
"$UMBRIEL" clock-freeze
check_admission plain true supported

configure pulse off false
check_admission bundled-pulse-with-shadow-and-light true supported
"$UMBRIEL" windows --json | jq -e '.[0].border_effect.name == "pulse"' > /dev/null
configure off scanlines false
check_admission bundled-scanlines false in_place_effect
configure off inversion false
check_admission inversion false in_place_effect
configure overlay-border off false
check_admission border-overlay false in_place_effect
configure off off true
check_admission backdrop-blur false backdrop_blur
configure pulse off false
check_admission revalidated-pulse true supported

# The inspector consumes cached selections without compiling, picking a pool,
# allocating sources, starting providers, or mutating an owner.
before=$("$UMBRIEL" effects --json | jq -S .)
for _ in $(seq 8); do
  "$UMBRIEL" participant-admission-probe --json > /dev/null
done
after=$("$UMBRIEL" effects --json | jq -S .)
[[ $before == "$after" ]]
echo "plain and bundled pulse admitted; scanlines, inversion, overlay and blur explicitly rejected; revalidation and inspection purity verified"
