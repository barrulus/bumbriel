#!/usr/bin/env bash
# Dynamic spectrum lookup, interpolation and clamping through the complete IPC/render path.
set -euo pipefail
cat > "$UMBRIEL_RUNTIME_DIR/audio-spectrum.glsl" <<'GLSL'
vec4 screen(vec2 uv) {
  return vec4(umbriel_audio_spectrum(uv.x * 1.2 - 0.1),
              umbriel_audio_spectrum(0.5), 1.0, 1.0);
}
GLSL
cat >> "$UMBRIEL_CONFIG" <<'TOML'
[effects]
screen = "audio-spectrum"
in_capture = true
[effects.preset.audio-spectrum]
kind = "screen"
shader = "audio-spectrum.glsl"
TOML
"$UMBRIEL" msg config-reload > /dev/null
python3 - <<'PY'
import json
import os
import socket
import subprocess
import threading
from pathlib import Path

image = str(Path(os.environ['UMBRIEL_RUNTIME_DIR']) / 'spectrum.png')
bands = [(i % 7) / 6 for i in range(64)]
message = (json.dumps({'cmd': 'effect-audio', 'version': 2, 'bands': bands}) + '\n').encode()
stop = threading.Event()
errors = []


def feed():
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(3)
            sock.connect(os.environ['UMBRIEL_SOCKET'])
            while True:
                sock.sendall(message)
                reply = b''
                while not reply.endswith(b'\n'):
                    chunk = sock.recv(256)
                    assert chunk, 'producer disconnected'
                    reply += chunk
                assert json.loads(reply) == {'ok': True}, reply
                ready.set()
                if stop.wait(0.1):
                    break
    except Exception as error:
        errors.append(error)
        ready.set()


def run(*args):
    return subprocess.check_output(args, text=True, timeout=10)


def sample(position):
    index = min(1, max(0, position)) * 63
    low = int(index)
    return bands[low] + (bands[min(low + 1, 63)] - bands[low]) * (index - low)


ready = threading.Event()
worker = threading.Thread(target=feed)
worker.start()
try:
    assert ready.wait(3), 'producer did not connect'
    assert not errors, errors
    run(os.environ['UMBRIEL'], 'settle')
    run('grim', '-s', '1', image)
    probe = os.environ['UMBRIEL_PIXEL_PROBE']
    width, height = map(int, run(probe, image, 'size').strip().split('x'))
    # All entries, half-entries, packed-vec4 boundaries, and both out-of-range clamps.
    positions = [-0.05, 1.05] + [i / 126 for i in range(127)]
    for position in positions:
        x = min(width - 1, max(0, round((position + 0.1) / 1.2 * width - 0.5)))
        actual = tuple(map(int, run(probe, image, 'pixel', str(x), str(height // 2)).split()))
        expected = (sample((x + 0.5) / width * 1.2 - 0.1) * 255, sample(0.5) * 255, 255)
        assert all(abs(a - b) < 4 for a, b in zip(actual, expected)), (position, actual, expected)
    assert not errors, errors
finally:
    stop.set()
    worker.join(3)
assert not worker.is_alive(), 'producer did not stop'
PY
