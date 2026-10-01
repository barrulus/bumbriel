#!/usr/bin/env python3
"""Record process cost without changing a running compositor or its workload.

Run separately for each baseline/candidate and disabled/unused/active/off phase.
Frame timing and GPU metrics require external instrumentation; missing metrics
stay explicitly unmeasured rather than being inferred from polling intervals.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import statistics
import time


def process(pid):
    try:
        # comm can contain spaces or parentheses; fields after its final ')' are stable.
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
        return {"cpu_ticks": int(fields[11]) + int(fields[12]),
                "rss_bytes": int(fields[21]) * os.sysconf("SC_PAGE_SIZE"), "start_ticks": int(fields[19])}
    except (FileNotFoundError, ProcessLookupError):
        return None


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compositor-pid", type=int, required=True)
    parser.add_argument("--helper-pid", type=int, action="append", default=[])
    parser.add_argument("--revision", required=True)
    parser.add_argument("--phase", choices=["disabled", "declared-unused", "active", "returned-off"], required=True)
    parser.add_argument("--workload", choices=["idle", "small-updates", "video-light", "scene-two-output"], required=True)
    parser.add_argument("--resolution", required=True, help="All output dimensions and refresh rates")
    parser.add_argument("--scale", required=True)
    parser.add_argument("--format", required=True, help="Storage format and linear/encoded value space")
    parser.add_argument("--roles", choices=["display", "display+unfiltered"], required=True)
    parser.add_argument("--inputs", required=True, help="Preset/audio/palette/clock state and workload seed")
    parser.add_argument("--duration", type=float, default=30)
    parser.add_argument("--interval", type=float, default=.1)
    parser.add_argument("--frame-times-csv", type=Path,
                        help="Instrumented CSV with frame_ms and missed columns; one actual frame per row")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not math.isfinite(args.duration) or not math.isfinite(args.interval) or args.duration <= 0 or args.interval <= 0:
        parser.error("duration and interval must be positive")
    pids = [args.compositor_pid, *args.helper_pid]
    initial = {pid: process(pid) for pid in pids}
    if initial[args.compositor_pid] is None:
        parser.error("compositor PID is not live")
    samples = []
    begin = time.monotonic()
    deadline = begin + args.duration
    while True:
        now = time.monotonic()
        samples.append({"elapsed_s": now - begin, "processes": {pid: process(pid) for pid in pids}})
        if now >= deadline:
            break
        time.sleep(min(args.interval, deadline - now))
    elapsed = samples[-1]["elapsed_s"]
    report = {key: value for key, value in vars(args).items() if key not in {"output", "frame_times_csv"}}
    report.update({"elapsed_s": elapsed, "clock_ticks_per_s": os.sysconf("SC_CLK_TCK"),
                   "processes": {}, "gpu": "unmeasured", "frame_times": "unmeasured"})
    for pid in pids:
        states = [sample["processes"][pid] for sample in samples if sample["processes"][pid] is not None]
        first, final = initial[pid], samples[-1]["processes"][pid]
        survived = first is not None and final is not None and first["start_ticks"] == final["start_ticks"]
        report["processes"][pid] = {
            "role": "compositor" if pid == args.compositor_pid else "audio-helper",
            "survived": survived,
            "cpu_seconds": ((final["cpu_ticks"] - first["cpu_ticks"]) / report["clock_ticks_per_s"]
                            if survived else None),
            "rss_peak_bytes": max((state["rss_bytes"] for state in states), default=0),
            "rss_final_bytes": final["rss_bytes"] if final is not None else None,
        }
    if args.frame_times_csv:
        with args.frame_times_csv.open(newline="") as stream:
            frames = list(csv.DictReader(stream))
        durations = [float(frame["frame_ms"]) for frame in frames]
        if not durations or any(not math.isfinite(value) or value < 0 for value in durations):
            parser.error("frame timing input must contain finite nonnegative actual frame durations")
        report["frame_times"] = {"source": str(args.frame_times_csv), "count": len(durations),
                                  "mean_ms": statistics.mean(durations), "p50_ms": percentile(durations, .5),
                                  "p95_ms": percentile(durations, .95), "p99_ms": percentile(durations, .99),
                                  "max_ms": max(durations), "missed": sum(int(frame["missed"]) for frame in frames)}
    report["samples"] = samples
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
