#!/usr/bin/env python3
"""Start one portal recording, or stop and finalize that recording."""
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
from datetime import datetime


def notify(message):
    try:
        subprocess.run(["notify-send", "-t", "4000", "-a", "Screen Recording", message], check=False)
    except OSError:
        pass


def identity(pid):
    # /proc field 22: distinguish this process from a reused PID.
    return Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()[19]


def main():
    runtime = Path(os.environ["XDG_RUNTIME_DIR"]) / "umbriel-recording"
    runtime.mkdir(mode=0o700, exist_ok=True)
    state = runtime / "process.json"
    if len(sys.argv) != 2 or sys.argv[1] not in ("start", "stop"):
        raise SystemExit("Usage: screen-record.py start|stop")

    if sys.argv[1] == "stop":
        try:
            recording = json.loads(state.read_text())
            pid = recording["pid"]
            # A pidfd ensures the signal cannot reach a replacement process.
            fd = os.pidfd_open(pid)
            try:
                if identity(pid) != recording["identity"]:
                    raise ProcessLookupError()
                signal.pidfd_send_signal(fd, signal.SIGINT)
            finally:
                os.close(fd)
        except (FileNotFoundError, ProcessLookupError):
            notify("No Umbriel recording is running.")
        return

    with (runtime / "lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            notify("A recording is already running. Mod+Shift+PrintScreen stops it.")
            return

        directory = Path.home() / "Videos" / "Screencasts"
        directory.mkdir(parents=True, exist_ok=True)
        output = directory / f"Screenrecord-{datetime.now():%Y-%m-%d_%H-%M-%S-%f}.mp4"
        log = runtime / "recorder.log"
        state.unlink(missing_ok=True)
        try:
            with log.open("w") as recording_log:
                process = subprocess.Popen([
                    "gpu-screen-recorder", "-w", "portal",
                    "-a", "default_output|default_input", "-f", "30",
                    # One quality step up; retain 30 fps to keep file growth down.
                    "-q", "high", "-ac", "aac", "-o", str(output),
                ], stdout=recording_log, stderr=subprocess.STDOUT)
                try:
                    temporary = runtime / "process.tmp"
                    temporary.write_text(json.dumps({"pid": process.pid, "identity": identity(process.pid)}))
                    temporary.replace(state)
                except FileNotFoundError:
                    pass  # Recorder exited immediately; report its failure below.
                result = process.wait()
            if result == 0 and output.exists() and output.stat().st_size:
                notify(f"Recording saved: {output}")
            else:
                notify(f"Recording cancelled or failed. Details: {log}")
        except OSError as error:
            notify(f"Could not start recording: {error}")
        finally:
            state.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
