#!/usr/bin/env python3
"""Capture a selected region or the active window and edit it in Satty."""
from datetime import datetime
import json
from pathlib import Path
import subprocess
import sys


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("region", "window"):
        raise SystemExit("Usage: screengrab.py region|window")

    if sys.argv[1] == "region":
        selection = subprocess.run(["slurp"], capture_output=True, text=True)
        if selection.returncode or not selection.stdout.strip():
            return  # Escape cancels without opening the editor.
        target = ["-g", selection.stdout.strip()]
    else:
        windows = json.loads(subprocess.check_output(["umbriel", "windows", "--json"]))
        # 'focused' remembers focus per workspace; 'active' is seat-global.
        window = next((window for window in windows if window.get("active")), None)
        if window is None:
            raise RuntimeError("No active window to capture.")
        target = ["-T", window["id"]]

    screenshot = subprocess.check_output(["grim", *target, "-"])
    directory = Path.home() / "Pictures" / "Screenshots"
    directory.mkdir(parents=True, exist_ok=True)
    output = directory / f"Screenshot-{datetime.now():%Y-%m-%d_%H-%M-%S-%f}.png"
    subprocess.run(
        ["satty", "--filename", "-", "--output-filename", str(output)],
        input=screenshot,
        check=True,
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        message = f"Screenshot failed: {error}"
        print(message, file=sys.stderr)
        try:
            subprocess.run(["notify-send", "-a", "Screenshot", message], check=False)
        except OSError:
            pass
        sys.exit(1)
