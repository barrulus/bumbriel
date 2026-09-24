#!/usr/bin/env bash
set -euo pipefail
readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
readonly BASE="$UMBRIEL_RUNTIME_DIR/base.toml"
cp "$UMBRIEL_CONFIG" "$BASE"
for library in packaged personal; do
  cat "$BASE" > "$UMBRIEL_CONFIG"
  if [[ $library == packaged ]]; then
    files="\"$ROOT/examples/shaders/barrulus/collection.toml\", \"$ROOT/examples/shaders/barrulus/choices.toml\""
    selected='"lightning", "lightning-melt", "cursor.comet", "window.parchment-dark"'
    expected=57
  else
    files="\"$ROOT/docs/examples/effects.toml\", \"$ROOT/docs/examples/choices.toml\", \"$ROOT/docs/examples/elastic.toml\""
    selected='"flowering-vine", "elastic", "window.liquid-glass"'
    expected=30
  fi
  cat >> "$UMBRIEL_CONFIG" <<TOML
[include]
files = [$files]
[appearance]
effects = [$selected]
[render.effects]
in_capture = true
TOML
  "$UMBRIEL" msg config-reload >/dev/null
  "$UMBRIEL" effects --json | jq -e --argjson expected "$expected" '.library | length >= $expected' >/dev/null
  FILL_COLOR=0xFF204080 "$UMBRIEL_UNMAP_CLIENT" "library-$library" 600 400 > "$UMBRIEL_RUNTIME_DIR/$library.log" 2>&1 &
  pid=$!
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == 1 ]] && break
    sleep 0.025
  done
  "$UMBRIEL" settle
  grim "$UMBRIEL_RUNTIME_DIR/$library.png"
  "$UMBRIEL" effects --json | jq -e '.windows[0].scopes.content.suppression == null and .windows[0].scopes["border.outer"].suppression == null' >/dev/null
  kill "$pid"
  for _ in $(seq 100); do
    [[ $("$UMBRIEL" windows --json | jq length) == 0 ]] && break
    sleep 0.025
  done
  "$UMBRIEL" settle
done
echo 'complete packaged and personal effect libraries prepared on the GPU and rendered their selected pipelines'
