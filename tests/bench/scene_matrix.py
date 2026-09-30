#!/usr/bin/env python3
"""Run isolated, repeatable scene cost cells. Native launch requires an unused TTY."""
import argparse
import fcntl
import hashlib
import json
import math
import stat
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time

from scene_cost import process
from scene_trace import summarize

ROOT = Path(__file__).resolve().parents[2]
PHASES = ("disabled", "declared-unused", "active", "returned-off")
WORKLOADS = ("idle", "small-updates", "video-light", "scene-single-output", "scene-two-output")
SCENE_REQUESTS = {
    0: ("pair-out", "workspace_transition"), 1: ("pair-back", "workspace_transition"),
    2: ("carousel-enter", "workspace_presentation"), 3: ("carousel-next", "workspace_presentation"),
    4: ("carousel-accept", "workspace_presentation"), 5: ("window-open", "window_presentation"),
    7: ("window-close", "window_presentation"),
}


def scene_coverage(actions, output_name):
    requests = []
    phases = {name: {"requested": 0, "admitted": 0, "fallbacks": [], "unobserved": 0}
              for name, kind in SCENE_REQUESTS.values()}
    for action in actions:
        if action.get('step') not in SCENE_REQUESTS:
            continue
        name, kind = SCENE_REQUESTS[action['step']]
        owner = next((owner for owner in action.get('state', {}).get('effects', {}).get('owners', [])
                      if owner.get('type') == 'output' and owner.get('name') == output_name), {})
        state = owner.get(kind, {})
        admitted = state.get('active') is True and state.get('memory_bytes', 0) > 0
        fallback = state.get('fallback', '')
        phases[name]['requested'] += 1
        phases[name]['admitted'] += admitted
        if not admitted:
            if fallback:
                phases[name]['fallbacks'].append(fallback)
            else:
                phases[name]['unobserved'] += 1
        requests.append({'step': action['step'], 'phase': name, 'output': output_name,
                         'monotonic_ns': action.get('monotonic_ns'), 'admitted': admitted,
                         'state': state})
    return {"requests": requests, "phases": phases,
            "validated": all(value['requested'] > 0 and value['admitted'] == value['requested'] for value in phases.values()),
            "note": "Admission observations per requested step; whole-cell timing also includes rest, landing and native rendering. Not every sampled frame is an active effect."}


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def command(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, timeout=kwargs.pop("timeout", 30), **kwargs)


def ensure_tracy_port_available(port=8086):
    # A crashed runner can leave its compositor alive after releasing the file
    # lock. Otherwise capture could silently attach to that unrelated session.
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            probe.bind(("127.0.0.1", port))
        except OSError as error:
            raise RuntimeError(f"Tracy port {port} is occupied; retire the prior profiling session before running") from error


def build_client(output):
    output.mkdir(parents=True, exist_ok=True)
    protocol_dir = command(["pkg-config", "--variable=pkgdatadir", "wayland-protocols"], capture_output=True, text=True).stdout.strip()
    protocol = Path(protocol_dir) / "stable/xdg-shell/xdg-shell.xml"
    header, source = output / "xdg-shell-client-protocol.h", output / "xdg-shell-client-protocol.c"
    command(["wayland-scanner", "client-header", protocol, header])
    command(["wayland-scanner", "private-code", protocol, source])
    import shlex
    flags = shlex.split(command(["pkg-config", "--cflags", "--libs", "wayland-client"], capture_output=True, text=True).stdout)
    target = output / "scene-workload"
    command([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", f"-I{output}", ROOT / "tests/bench/scene_workload.c", source, "-o", target, *flags])
    return target


def children(pid):
    """Only this isolated compositor's descendants, never the user's session."""
    found = []
    pending = [pid]
    while pending:
        parent = pending.pop()
        try:
            direct = [int(x) for x in Path(f"/proc/{parent}/task/{parent}/children").read_text().split()]
        except (FileNotFoundError, ProcessLookupError):
            continue
        pending.extend(direct)
        found.extend(direct)
    return found


def sample(pid):
    result = {}
    for child in [pid, *children(pid)]:
        state = process(child)
        if state is not None:
            try:
                state["command"] = Path(f"/proc/{child}/cmdline").read_bytes().replace(b"\0", b" ").decode(errors="replace")
            except FileNotFoundError:
                continue
            result[str(child)] = state
    return {"monotonic_ns": time.monotonic_ns(), "processes": result}


def process_summary(samples, pid):
    report = {}
    ticks = os.sysconf("SC_CLK_TCK")
    for identity in sorted({(key, state["start_ticks"]) for s in samples for key, state in s["processes"].items()}):
        key, start = identity
        states = [(s["monotonic_ns"], s["processes"][key]) for s in samples if key in s["processes"] and s["processes"][key]["start_ticks"] == start]
        first, last = states[0][1], states[-1][1]
        report[f"{key}:{start}"] = {"role": "compositor" if int(key) == pid else "compositor-child",
            "command": first["command"], "cpu_seconds_observed": (last["cpu_ticks"]-first["cpu_ticks"])/ticks,
            "rss_peak_bytes": max(s["rss_bytes"] for t, s in states), "rss_final_observed_bytes": last["rss_bytes"],
            "observation_start_ns": states[0][0], "observation_end_ns": states[-1][0],
            "present_at_end": samples[-1]["processes"].get(key, {}).get("start_ticks") == start}
    return report


def config(output_names, phase, candidate, workload, runtime, args):
    declared = phase in ("declared-unused", "active")
    active = phase == "active"
    includes = []
    if declared and candidate:
        includes = [str(ROOT / f"examples/effects/scene/{name}/effect.toml") for name in ("melt", "carousel", "water", "portal")]
    lines = [f'[include]\nfiles = {json.dumps(includes)}',
             '[general]\nxwayland = false\nshow_cheatsheet = false\nautostart = []',
             '[keybinds]\n"Mod+Shift+Escape" = "session-quit:skip-confirmation"',
             '[animation]\nenabled = true\nduration_ms = 180\ncurve = "linear"',
             '[appearance]\nborder_width = 4\nouter_border_width = 0',
             '[effects]\nmax_fps = 60\nborder = ' + json.dumps("cost-border" if active else "") + '\nwindow = ' + json.dumps("cost-window" if active and workload == "small-updates" else "")]
    if workload.startswith("scene-"):
        lines += ['[appearance.blur]\nenabled = false']
    if declared:
        shader = 'vec4 border(vec2 uv) { return vec4(0.2, 0.65, 0.9, 1.0); }\n'
        if workload == "video-light":
            shader = 'vec4 border(vec2 uv) { return vec4(0.2 + 0.1*sin(umbriel_time), 0.65, 0.9, 1.0); }\n'
        if candidate:
            shader = shader.replace('0.65', '0.65 + 0.1*umbriel_audio_rms()')
        (runtime / "border.glsl").write_text(shader)
        (runtime / "window.glsl").write_text('vec4 window(vec2 uv) { vec4 c = umbriel_sample(uv); return vec4(c.rgb * 0.98, c.a); }\n')
        lines += ['[effects.preset.cost-border]\nkind = "border"\nshader = "border.glsl"\nanimated = ' + ('true' if workload == "video-light" else 'false') + ('\nspeed = 0' if args.time_mode == 'frozen' else '') + ('\naudio = "cost-silence"' if candidate else ''),
                  '[effects.preset.cost-window]\nkind = "window"\nshader = "window.glsl"']
        if workload == "small-updates":
            border_index = next(i for i, line in enumerate(lines) if line.startswith('[effects.preset.cost-border]'))
            lines[border_index] += '\noverlay = "cost-overlay"'
            lines += ['[effects.preset.cost-overlay]\nkind = "window"\nshader = "window.glsl"']
        if workload in ("video-light", "scene-single-output", "scene-two-output"):
            lines += ['[effects.preset.cost-border.light]\nspread = 32\nintensity = 1.2\nthreshold = 0.2']
        if candidate:
            lines += ['[effects.audio.sources.cost-silence]\nprovider = "external"\nmode = "playback"\ntarget = "synthetic-cost-silence"\nexecutable = ' + json.dumps(str(args.helper)) + '\nargs = ["--external-test", "--silence"]']
    if active and candidate and workload.startswith("scene-"):
        lines += ['[workspace_presentation]\neffect = "carousel"', '[animation.workspaces]\neffect = "melt"',
                  '[animation.windows_in]\neffect = "water"', '[animation.windows_out]\neffect = "portal"']
    width, height = (int(x) for x in args.resolution.split("x"))
    for index, name in enumerate(output_names):
        lines += [f'[output.{json.dumps(name)}]\nmode = "{width}x{height}"\nscale = {args.scale}\nposition = [{round(index*width/args.scale)}, 0]\nworkspaces = 3']
    for title, out, x in (("cost-main", output_names[0], 80), ("cost-neighbour", output_names[0], 260), ("cost-secondary", output_names[-1], 80), ("cost-target", output_names[0], 120)):
        floating = not workload.startswith("scene-") or title == "cost-secondary"
        lines += ['[[window_rule]]\nmatch.title = ' + json.dumps(f"^{title}$") + '\ndefault_output = ' + json.dumps(out) + '\ndefault_floating = ' + str(floating).lower() + (f'\ndefault_position = {{ x = {x}, y = 100, anchor = "top_left" }}' if floating else '')]
    return "\n\n".join(lines) + "\n"


def validate_configs(args):
    """Use each measured revision's parser before starting any compositor.

    TOML parsing cannot detect unsupported semantic keys. Keep complete shader
    siblings with every checked config so the evidence can be validated again.
    """
    results = []
    print("Checking generated configs with each measured binary before compositor startup...", flush=True)
    for label, binary in (("baseline", args.baseline), ("candidate", args.candidate)):
        if binary is None:
            continue
        for workload in args.workloads:
            print(f"  Validating {label}/{workload} ({len(PHASES)} phases)", flush=True)
            names = (["HEADLESS-1", "HEADLESS-2"] if workload == "scene-two-output" else ["HEADLESS-1"]) if args.backend == "headless" else args.outputs.split(",")
            for phase in PHASES:
                directory = args.output / "config-validation" / label / workload / phase
                directory.mkdir(parents=True)
                path = directory / "config.toml"
                path.write_text(config(names, phase, label == "candidate", workload, directory, args))
                try:
                    checked = subprocess.run([str(binary), "config", "validate", "-c", str(path)],
                                             capture_output=True, text=True, timeout=30)
                    returncode, diagnostics = checked.returncode, checked.stdout + checked.stderr
                except (OSError, subprocess.SubprocessError) as error:
                    returncode, diagnostics = -1, f"validator could not complete: {error}\n"
                (directory / "validation.log").write_text(diagnostics)
                if returncode:
                    print(f"  Rejected {label}/{workload}/{phase}:\n{diagnostics}", file=sys.stderr, flush=True)
                results.append({"revision": label, "workload": workload, "phase": phase,
                                "returncode": returncode, "config": str(path), "binary": str(binary),
                                "config_sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    write_json(args.output / "config-validation.json", results)
    failures = [result for result in results if result["returncode"] != 0]
    if failures:
        raise RuntimeError(f"{len(failures)} benchmark configs rejected; no compositor started. See {args.output}/config-validation.json")
    print(f"Validated {len(results)} benchmark configs with the exact measured binaries", flush=True)


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=4)


class Session:
    def __init__(self, binary, names, candidate, workload, directory, args):
        self.binary, self.names, self.candidate, self.workload, self.directory, self.args = binary, names, candidate, workload, directory, args
        self.runtime = Path(tempfile.mkdtemp(prefix="umb-cost-"))
        self.runtime.chmod(0o700)
        self.env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "UMBRIEL_SOCKET", "WLR_BACKENDS", "WLR_HEADLESS_OUTPUTS")}
        self.env.update(XDG_RUNTIME_DIR=str(self.runtime))
        if args.backend == "headless":
            self.env.update(WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS=str(len(names)), WLR_LIBINPUT_NO_DEVICES="1")
        else:
            self.env["WLR_BACKENDS"] = "drm,libinput"
        self.clients = []
        self.files = []
        self.server = None
        self.runtime.joinpath("config.toml").write_text(config(names, "disabled", candidate, workload, self.runtime, args))
        log = self.open("compositor.log")
        self.server = subprocess.Popen([sys.executable, __file__, "--exec-compositor", str(binary), "-c", str(self.runtime / "config.toml")], env=self.env, stdout=log, stderr=log, start_new_session=True)
        deadline = time.monotonic() + 15
        socket = self.runtime / "umbriel-wayland-0.sock"
        while not socket.exists():
            if self.server.poll() is not None or time.monotonic() > deadline:
                self.close()
                raise RuntimeError(f"compositor did not start: {directory}/compositor.log")
            time.sleep(.02)
        self.env.update(UMBRIEL_SOCKET=str(socket), WAYLAND_DISPLAY="wayland-0")

    def open(self, name):
        stream = (self.directory / name).open("w")
        self.files.append(stream)
        return stream

    def ipc(self, *args):
        if args[0] == 'outputs':
            # Output/mode enumeration is a Wayland protocol client, not IPC.
            # snapshot() calls this only outside the timed capture interval.
            return command([self.binary, *args], env=self.env, capture_output=True, text=True, timeout=10).stdout
        # Do not launch the instrumented binary for inspection: starting its
        # Tracy threads on every CLI call perturbs and can skip short cycles.
        request = {"cmd": args[0]}
        if args[0] == "msg":
            request["arg"] = args[1]
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(10)
            connection.connect(self.env["UMBRIEL_SOCKET"])
            connection.sendall((json.dumps(request) + "\n").encode())
            chunks = []
            while chunk := connection.recv(65536):
                chunks.append(chunk)
        response = json.loads(b"".join(chunks))
        if "ok" not in response:
            raise RuntimeError(f"IPC {args[0]}: {response.get('err', 'missing response')}")
        return json.dumps(response["ok"])

    def spawn(self, mode, title, width=640, height=360):
        stream = self.open(f"{title}-{len(self.clients)}.jsonl")
        proc = subprocess.Popen([self.args.client, mode, title, str(width), str(height)], env=self.env, stdout=stream, stderr=stream)
        self.clients.append(proc)
        return proc

    def focus_main(self):
        deadline = time.monotonic() + 5
        while True:
            windows = json.loads(self.ipc("windows", "--json"))
            main = next((w for w in windows if w["title"] == "cost-main"), None)
            if main is not None:
                self.ipc("msg", f"window-focus-warp:{main['id']}")
                return
            if time.monotonic() > deadline:
                raise RuntimeError("workload client did not map")
            time.sleep(.02)

    def snapshot(self, *, include_outputs=True):
        result = {}
        # Observe short-lived presentation reservations before slower metadata
        # requests, so a completed transition is not mistaken for no admission.
        for cmd in ("effects", "effect-frames", "outputs", "color", "windows"):
            if cmd == 'outputs' and not include_outputs:
                continue
            try:
                result[cmd] = json.loads(self.ipc(cmd, "--json"))
            except (OSError, RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
                result[cmd] = {"unavailable": str(error)}
        return result

    def set_phase(self, phase):
        if self.candidate and self.workload.startswith("scene-"):
            owners = json.loads(self.ipc("effects", "--json")).get("owners", [])
            if any(owner.get("workspace_presentation", {}).get("active") for owner in owners):
                self.ipc("msg", "workspace-presentation-cancel")
        self.focus_main()
        contents = config(self.names, phase, self.candidate, self.workload, self.runtime, self.args)
        self.runtime.joinpath("config.toml").write_text(contents)
        self.ipc("msg", "config-reload")
        (self.directory / f"{phase}.toml").write_text(contents)
        for shader in self.runtime.glob("*.glsl"):
            shutil.copyfile(shader, self.directory / f"{phase}-{shader.name}")
        time.sleep(self.args.warmup)

    def close(self):
        for proc in self.clients:
            stop(proc)
        if self.server is not None:
            stop(self.server)
            # Only this start_new_session process group; retire owned helper descendants.
            try:
                os.killpg(self.server.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        for stream in self.files:
            stream.close()
        shutil.copytree(self.runtime, self.directory / "runtime", dirs_exist_ok=True,
                        ignore=lambda directory, names: [name for name in names if not stat.S_ISREG((Path(directory)/name).lstat().st_mode) and not (Path(directory)/name).is_dir()])
        shutil.rmtree(self.runtime)


def resource_summary(snapshots):
    observations = {}
    def walk(value, path):
        if isinstance(value, dict):
            for key, child in value.items():
                current = f"{path}/{key}"
                if key in ("memory_bytes", "reserved_bytes") and isinstance(child, int):
                    observations.setdefault(current, []).append(child)
                elif key in ("fallback", "admission") and isinstance(child, str):
                    observations.setdefault(current, []).append(child)
                else:
                    walk(child, current)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                walk(child, f"{path}/{index}")
    for snapshot in snapshots:
        walk(snapshot, "")
    return {"note": "Observed per-owner reservation maxima, not allocator high-water; nested aliases are not summed",
            "paths": {key: {"observed_max_bytes": max(values), "last_bytes": values[-1]} if isinstance(values[0], int)
                      else {"observed_reasons": sorted(set(values))} for key, values in observations.items()}}


def trace_cell(session, phase):
    args = session.args
    cell = session.directory / phase
    cell.mkdir()
    before = session.snapshot()
    write_json(cell / "before.json", before)
    capture_log = (cell / "capture.log").open("w")
    capture = subprocess.Popen([args.capture, "-a", "127.0.0.1", "-s", str(args.duration), "-f", "-o", str(cell / "trace.tracy")], stdout=capture_log, stderr=capture_log)
    samples, actions = [], []
    start, step, transient = time.monotonic(), -1, None
    try:
        while capture.poll() is None:
            elapsed = time.monotonic() - start
            if elapsed > args.duration + 20 or session.server.poll() is not None:
                raise RuntimeError("capture timed out or compositor stopped")
            samples.append(sample(session.server.pid))
            if session.workload.startswith("scene-") and elapsed >= step + 1:
                step += 1
                index = step % 8
                if index in (0, 1):
                    action = f"workspace-switch:{2 if index == 0 else 1}"
                elif index in (2, 3, 4):
                    action = ("workspace-presentation-enter", "workspace-presentation-next", "workspace-presentation-accept")[index-2] if phase == "active" and session.candidate else f"workspace-switch:{1 if index == 2 else 2}"
                else:
                    action = None
                if action:
                    session.ipc("msg", action)
                    actions.append({"monotonic_ns": time.monotonic_ns(), "action": action})
                if index == 5:
                    session.focus_main()
                    actions.append({"monotonic_ns": time.monotonic_ns(), "action": "focus-main-before-window"})
                    # The carousel may accept another workspace. Apply the same
                    # real-time pause in both revisions so its focus-induced
                    # 180 ms native switch lands before requesting window open.
                    time.sleep(.25)
                    transient = session.spawn("idle", "cost-target", 400, 280)
                if index == 7 and transient is not None:
                    stop(transient)
                    transient = None
                time.sleep(.075)
                actions.append({"monotonic_ns": time.monotonic_ns(), "step": index, "state": session.snapshot(include_outputs=False)})
            time.sleep(.1)
        if capture.returncode:
            raise RuntimeError(f"Tracy capture failed ({capture.returncode})")
    finally:
        stop(capture)
        capture_log.close()
        if transient is not None:
            stop(transient)
    samples.append(sample(session.server.pid))
    write_json(cell / "process-samples.json", samples)
    write_json(cell / "actions.json", actions)
    after = session.snapshot()
    write_json(cell / "after.json", after)
    if args.defer_export:
        report = {"profiling": "awaiting explicit export; no CPU/GPU timing claims", "cpu_render": {"count": None}, "gpu": {"query_zones": None}}
    else:
        for name, flags in (("cpu", ["-u", "-f", "Output::render"]), ("gpu", ["-g"]), ("messages", ["-m"])):
            with (cell / f"{name}.csv").open("w") as stream, (cell / f"{name}-export.log").open("w") as error:
                command([args.exporter, *flags, cell / "trace.tracy"], stdout=stream, stderr=error, timeout=60)
        report = summarize(cell / "cpu.csv", cell / "gpu.csv", cell / "messages.csv", backend=args.backend)
    report.update(phase=phase, workload=session.workload, time_mode=args.time_mode,
        process_cost=process_summary(samples, session.server.pid),
        process_sampling_scope="Tracy capture process lifetime (includes connection handshake); raw monotonic samples retained",
        elapsed_sample_seconds=(samples[-1]["monotonic_ns"]-samples[0]["monotonic_ns"])/1e9,
        helper_source="synthetic unchanged silence, no device acquisition" if session.candidate else "not supported by baseline",
        effect_comparison="candidate scene-v1 versus native workspace/window operations" if session.workload.startswith("scene-") else "same native workload and visually equivalent border; candidate also binds silent audio",
        format="default encoded SDR; verify actual output metadata in before/after.json", roles="display")
    client_activity = {}
    for path in session.directory.glob("cost-*.jsonl"):
        records = [json.loads(line) for line in path.read_text().splitlines() if line.startswith('{')]
        selected = [r for r in records if samples[0]["monotonic_ns"] <= r["monotonic_ns"] <= samples[-1]["monotonic_ns"]]
        client_activity[path.name] = {"commit_requests": sum(r["event"] == "commit_request" for r in selected),
                                      "frame_callbacks": sum(r["event"] == "frame_done" for r in selected)}
    report["client_activity"] = client_activity
    report["scene_cycle_complete_duration"] = args.duration >= 8 if session.workload.startswith("scene-") else None
    report["scene_cycle_steps"] = sorted({item["step"] for item in actions if "step" in item})
    report["scene_cycle_complete"] = set(range(8)).issubset(report["scene_cycle_steps"]) if session.workload.startswith("scene-") else None
    report["scene_resources"] = resource_summary([before, *(item["state"] for item in actions if "state" in item), after])
    if session.workload.startswith("scene-") and session.candidate and phase == "active":
        kinds = {kind: [] for kind in ("workspace_transition", "workspace_presentation", "window_presentation")}
        for action in actions:
            for owner in action.get("state", {}).get("effects", {}).get("owners", []):
                if owner.get("type") == "output":
                    for kind in kinds:
                        if isinstance(owner.get(kind), dict):
                            kinds[kind].append(owner[kind])
        report["scene_admission"] = {kind: {"observed_active": any(s.get("active") and s.get("memory_bytes", 0) > 0 for s in states),
                                           "states": states} for kind, states in kinds.items()}
        report["scene_coverage"] = scene_coverage(actions, session.names[0])
        report["scene_cost_validated"] = report["scene_cycle_complete"] and report["scene_coverage"]["validated"]
    report["baseline_schema"] = "candidate scene/audio declarations" if session.candidate else "legacy declarations only; scene/audio schema unavailable"
    write_json(cell / "summary.json", report)
    print(f"{cell}: {report['cpu_render']['count']} render zones, {report['gpu']['query_zones']} GPU zones", flush=True)


def terminate(signum, frame):
    raise KeyboardInterrupt(f"signal {signum}")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--exec-compositor":
        signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT, signal.SIGTERM})
        os.execv(sys.argv[2], sys.argv[2:])
    signal.signal(signal.SIGTERM, terminate)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-client", type=Path, help="build workload client into directory and exit")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--exporter", type=Path)
    parser.add_argument("--helper", type=Path, help="explicit release-built synthetic helper; no physical audio acquisition")
    parser.add_argument("--client", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--backend", choices=("headless", "drm"), default="headless")
    parser.add_argument("--allow-native-session", action="store_true", help="explicitly launch DRM from an unused TTY")
    parser.add_argument("--outputs", default="eDP-1", help="comma-separated physical outputs for DRM only")
    parser.add_argument("--resolution", default="1920x1080")
    parser.add_argument("--scale", type=float, default=1)
    parser.add_argument("--duration", type=int, default=30)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--warmup", type=float, default=2)
    parser.add_argument("--workloads", nargs="+", choices=WORKLOADS, default=list(WORKLOADS))
    parser.add_argument("--validate-configs-only", action="store_true", help="validate every generated config using each exact binary without starting a compositor")
    parser.add_argument("--defer-export", action="store_true", help="save raw captures and mark timing unmeasured; diagnostic smoke only")
    parser.add_argument("--time-mode", choices=("advancing", "frozen"), default="advancing")
    args = parser.parse_args()
    if args.build_client:
        print(build_client(args.build_client.resolve()))
        return
    for name in (("candidate", "helper", "output") if args.validate_configs_only else ("candidate", "capture", "exporter", "client", "helper", "output")):
        if getattr(args, name) is None:
            parser.error(f"--{name} required")
    if args.duration <= 0 or args.runs <= 0 or not math.isfinite(args.scale) or args.scale <= 0 or not math.isfinite(args.warmup) or args.warmup < 0:
        parser.error("duration, runs and scale must be positive; warmup must be nonnegative")
    try:
        dimensions = [int(x) for x in args.resolution.split("x")]
        if len(dimensions) != 2 or min(dimensions) < 64 or max(dimensions) > 8192:
            raise ValueError()
    except ValueError:
        parser.error("resolution must be WIDTHxHEIGHT within64..8192")
    if args.backend == "drm" and not args.validate_configs_only and (not args.allow_native_session or os.environ.get("WAYLAND_DISPLAY") or os.environ.get("DISPLAY")):
        parser.error("native runs require --allow-native-session from an unused TTY without WAYLAND_DISPLAY/DISPLAY")
    if args.time_mode == "frozen" and any(w != "video-light" for w in args.workloads):
        parser.error("--time-mode frozen is the video-light variant; select --workloads video-light")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    for name in ("candidate", "baseline", "capture", "exporter", "client", "helper"):
        path = getattr(args, name)
        if path is not None:
            setattr(args, name, path.resolve(strict=True))
    connectors = {p.parent.name: p.read_text().strip() for p in Path("/sys/class/drm").glob("card*-*/status")}
    metadata = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
    metadata.update(connectors=connectors, uname=list(os.uname()), limitations=[
        "No requested presentation deadlines: missed-deadline count remains unmeasured",
        "Headless results do not prove physical direct scanout, HDR, or two-display isolation",
        "Runner covers display-only encoded SDR; unmanaged10-bit/managedHDR and unfiltered capture need separately instrumented sessions",
        "Synthetic audio measures transport/helper cost, not PipeWire analysis or acoustic latency"],
        artifact_sha256={label: hashlib.sha256(path.read_bytes()).hexdigest() for label, path in (("baseline", args.baseline), ("candidate", args.candidate), ("synthetic_helper", args.helper), ("workload_client", args.client), ("capture", args.capture), ("exporter", args.exporter)) if path is not None})
    write_json(args.output / "metadata.json", metadata)
    validate_configs(args)
    if args.validate_configs_only:
        return
    # One Tracy TCP endpoint and GPU workload at a time, including other bench runs.
    with Path("/tmp/umbriel-effects-tracy-cost.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        ensure_tracy_port_available()
        for run in range(args.runs):
            # Alternate revision order to reduce thermal/order bias.
            revisions = [("baseline", args.baseline), ("candidate", args.candidate)]
            if run % 2:
                revisions.reverse()
            for label, binary in revisions:
                if binary is None:
                    continue
                for workload in args.workloads:
                    directory = args.output / f"run-{run+1}" / label / workload
                    directory.mkdir(parents=True)
                    names = (["HEADLESS-1", "HEADLESS-2"] if workload == "scene-two-output" else ["HEADLESS-1"]) if args.backend == "headless" else args.outputs.split(",")
                    if workload == "scene-two-output" and len(names) < 2:
                        write_json(directory / "unavailable.json", {"reason": "two physical outputs not provided; no physical substitute", "phases": PHASES})
                        continue
                    ensure_tracy_port_available()
                    session = Session(binary, names, label == "candidate", workload, directory, args)
                    try:
                        session.spawn({"idle": "idle", "small-updates": "small", "video-light": "video", "scene-single-output": "small", "scene-two-output": "small"}[workload], "cost-main")
                        if workload in ("small-updates", "scene-single-output", "scene-two-output"):
                            session.spawn("idle", "cost-neighbour")
                        if workload == "scene-two-output":
                            session.spawn("small", "cost-secondary", 320, 240)
                        for phase in PHASES:
                            session.set_phase(phase)
                            trace_cell(session, phase)
                    finally:
                        session.close()
    if not args.defer_export:
        # Success means the declared matrix and its admission evidence passed,
        # not merely that the last subprocess returned zero.
        from scene_report import main as report_main
        report_main([str(args.output)])


if __name__ == "__main__":
    main()
