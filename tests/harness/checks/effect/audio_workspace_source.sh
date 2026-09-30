#!/usr/bin/env bash
# Hidden workspace source views reuse the composing output's latch and original
# selection owners. Failed output commits retain every captured role together.
set -euo pipefail
helper=$(realpath "$(dirname "$UMBRIEL_UNMAP_CLIENT")/audio-synthetic")
cat > "$UMBRIEL_RUNTIME_DIR/source-native.glsl" <<'GLSL'
vec4 window(vec2 uv) { return vec4(umbriel_audio_rms(), 0.0, 0.0, 1.0); }
GLSL
cat > "$UMBRIEL_RUNTIME_DIR/source-hidden.glsl" <<'GLSL'
vec4 window(vec2 uv) { return vec4(0.0, umbriel_audio_rms(), 0.0, 1.0); }
GLSL
cat >> "$UMBRIEL_CONFIG" <<EOF_CONFIG

[animation]
enabled = false
[appearance]
border_width = 0
outer_border_width = 0
corner_radius = 0
[appearance.shadow]
enabled = false
[effects]
in_capture = true
max_fps = 12
[effects.preset.source-native]
kind = "window"
shader = "source-native.glsl"
audio = "native"
[effects.preset.source-hidden]
kind = "window"
shader = "source-hidden.glsl"
audio = "hidden"
[effects.audio.sources.native]
provider = "external"
mode = "playback"
target = "explicit-native-test"
executable = "$helper"
args = ["--external-test", "--silence"]
[effects.audio.sources.hidden]
provider = "external"
mode = "playback"
target = "explicit-hidden-test"
executable = "$helper"
args = ["--external-test", "--silence"]
[[window_rule]]
match.title = "^source-native$"
default_floating = true
default_position = { x = 100, y = 100, anchor = "top_left" }
window_effect = "source-native"
[[window_rule]]
match.title = "^source-hidden$"
default_floating = true
default_position = { x = 100, y = 100, anchor = "top_left" }
window_effect = "source-hidden"
EOF_CONFIG
"$UMBRIEL" msg config-reload > /dev/null
for _ in $(seq 100); do
  (( $(grep -c 'config reloaded' "$UMBRIEL_RUNTIME_DIR/compositor.log") >= 2 )) && break
  sleep .02
done
(( $(grep -c 'config reloaded' "$UMBRIEL_RUNTIME_DIR/compositor.log") >= 2 ))
spawn() {
  FILL_COLOR=0xFF000000 "$UMBRIEL_UNMAP_CLIENT" "$1" 300 200 > "$UMBRIEL_RUNTIME_DIR/$1.log" 2>&1 &
  for _ in $(seq 80); do
    "$UMBRIEL" windows --json | jq -e --arg title "$1" 'any(.[]; .title == $title)' > /dev/null && return
    sleep .025
  done
  return 1
}
spawn source-native
"$UMBRIEL" msg workspace-switch:2 > /dev/null
spawn source-hidden
"$UMBRIEL" msg workspace-switch:1 > /dev/null
"$UMBRIEL" settle > /dev/null
python3 - <<'PY'
import json
import os
import subprocess
import time

umbriel = os.environ["UMBRIEL"]
def run(*args):
    return subprocess.check_output([umbriel, *args], text=True, timeout=10)
def data(command):
    return json.loads(run(command, "--json"))
def probe(action="status"):
    result = json.loads(run("presentation-workspace-probe", action, "--json"))
    assert "err" not in result, result
    return result.get("ok", result)
def wait(get, predicate, reason):
    end = time.monotonic() + 5
    while True:
        value = get()
        if predicate(value):
            return value
        assert time.monotonic() < end, (reason, value)
        time.sleep(.01)
def source(effects, name):
    return next(s for s in effects["audio"] if s["name"] == name)
def frames():
    return data("effect-frames")["outputs"][0]
def latch(frame):
    return next(a for a in frame["audio"] if a["source"] == "hidden")
def inject(value):
    run("audio-inject", json.dumps(dict(source="hidden", rms=value, peak=value, envelope=value, bands=[value] * 16)))
def pixel(value):
    image = os.path.join(os.environ["UMBRIEL_RUNTIME_DIR"], "workspace-audio.png")
    subprocess.run(["grim", "-s", "1", image], check=True, timeout=10)
    rgb = [int(c) for c in subprocess.check_output([os.environ["UMBRIEL_PIXEL_PROBE"], image, "pixel", "150", "150"], text=True).split()]
    assert rgb[0] <= 2 and abs(rgb[1] - round(value * 255)) <= 3 and rgb[2] <= 2, ("source audio pixel", value, rgb)

hidden = next(w for w in data("windows") if w["title"] == "source-hidden")
wait(lambda: data("effects"), lambda e: e["audio_demanded_sources"] == 1 and source(e, "native")["available"] and not source(e, "hidden")["demanded"], "native baseline demand")
run("clock-freeze")
run("settle")
probe("open " + hidden["workspace"])
wait(lambda: data("effects"), lambda e: e["audio_demanded_sources"] == 1 and source(e, "hidden")["available"] and not source(e, "native")["demanded"], "hidden source did not replace native demand")
wait(probe, lambda p: p["active"] and p["committed_revision"] > 0, "preview never committed")
for value in (.8, 0.0):
    inject(value)
    wait(frames, lambda f: not latch(f)["pending"] and abs(latch(f)["presented_rms"] - value) < 1e-6, "source injection not consumed")
    pixel(value)

# Both capture roles and all faces retain the failed output's input/revision.
run("output-commit-hold", "HEADLESS-1 on")
inject(.25)
held = wait(frames, lambda f: f["rejected_buffer_commits"] > 0 and latch(f)["pending"] and latch(f)["latched_rms"] == .25, "source frame never failed")
before = probe()
inject(.75)
# Real time: exercise actual failed-output retries under the frozen clock.
time.sleep(.15)
after = probe()
assert after["captures"] == before["captures"], ("failed source was recaptured", before, after)
assert after["captured_revision"] == before["captured_revision"] and after["committed_revision"] == before["committed_revision"], ("failed source revision advanced", before, after)
assert latch(frames())["consumed_revision"] == latch(held)["consumed_revision"], "failed source input acknowledged"
run("output-commit-hold", "HEADLESS-1 off")
wait(frames, lambda f: not latch(f)["pending"] and latch(f)["presented_rms"] == .75, "source failed-submit recovery")
pixel(.75)
run("settle")
before, before_frame = probe(), frames()
time.sleep(.2)
after, after_frame = probe(), frames()
assert after["captures"] == before["captures"], ("unchanged source recaptured", before, after)
assert after_frame["buffer_commits"] == before_frame["buffer_commits"], ("unchanged source kept committing", before_frame, after_frame)
probe("cancel")
wait(lambda: data("effects"), lambda e: e["audio_demanded_sources"] == 1 and source(e, "native")["available"] and not source(e, "hidden")["demanded"], "source release did not restore native demand")
assert next(w for w in data("windows") if w["title"] == "source-hidden")["workspace"] == hidden["workspace"], "preview changed native membership"
print("hidden source audio replaced native demand, matched pixels, held failed roles, idled, and restored native demand")
PY
