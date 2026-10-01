#!/usr/bin/env python3
"""Summarize actual Tracy render/query/presentation events, never inferred FPS."""
import argparse
import csv
from collections import Counter
import json
import math
from pathlib import Path
import statistics


def distribution(values):
    if not values:
        return {"count": 0}
    if any(not math.isfinite(v) or v < 0 for v in values):
        raise ValueError("durations must be finite and nonnegative")
    ordered = sorted(values)
    return {"count": len(values), "mean_ms": statistics.mean(values),
            **{f"p{p}_ms": ordered[max(0, math.ceil(len(values) * p / 100) - 1)] for p in (50, 95, 99)},
            "max_ms": max(values)}


def union_ns(intervals):
    total, end = 0, None
    for start, stop in sorted(intervals):
        if stop < start:
            raise ValueError("negative GPU duration")
        total += max(0, stop - max(start, end if end is not None else start))
        end = max(stop, end if end is not None else stop)
    return total


def rows(path, required):
    with Path(path).open(newline="") as source:
        reader = csv.DictReader(source)
        if not set(required).issubset(reader.fieldnames or []):
            raise ValueError(f"{path}: missing columns {required}")
        result = list(reader)
    if any(None in row or any(value is None for value in row.values()) for row in result):
        raise ValueError(f"{path}: malformed CSV (export filtered CPU zones, not multiline zone text)")
    return result


def summarize(cpu_path, gpu_path, message_path, *, backend="unknown"):
    cpu = rows(cpu_path, ("name", "ns_since_start", "exec_time_ns"))
    if any(row["name"] != "Output::render" for row in cpu):
        raise ValueError("CPU export must be filtered to Output::render")
    gpu = rows(gpu_path, ("Time from start of program", "GPU execution time"))
    messages = ([] if Path(message_path).read_text().strip() == "There are currently no messages!"
                else rows(message_path, ("MessageName", "total_ns")))
    renders = [(int(r["ns_since_start"]), int(r["exec_time_ns"])) for r in cpu]
    intervals = [(int(r["Time from start of program"]), int(r["Time from start of program"]) + int(r["GPU execution time"])) for r in gpu]
    events = []
    for row in messages:
        if not row["MessageName"].startswith("umbriel.output|"):
            continue
        event = dict(part.split("=", 1) for part in row["MessageName"].split("|")[1:])
        event["trace_ns"] = int(row["total_ns"])
        events.append(event)
    if renders and not events:
        raise ValueError("render zones present without actual output instrumentation")
    commits = [e for e in events if e["phase"] == "commit_end" and e["success"] == "true" and e["buffer"] == "true"]
    if commits and not gpu:
        raise ValueError("buffer commits but empty GPU export: exporter readiness/instrumentation failure")
    if intervals and renders:
        low = min(t for t, d in renders) - 100_000_000
        high = max(t + d for t, d in renders) + 100_000_000
        if any(start < low or end > high for start, end in intervals):
            raise ValueError("GPU/CPU clocks are not aligned within 100 ms (calibration or capture boundary)")
    outputs = {}
    for name in sorted({e["output"] for e in events}):
        selected = [e for e in events if e["output"] == name]
        presents = [e for e in selected if e["phase"] == "present" and e["success"] == "true"]
        begins = {e["commit_seq"]: e for e in selected if e["phase"] == "commit_begin"}
        latencies = []
        for p in presents:
            start = begins.get(p["commit_seq"])
            if start is not None:
                delay = int(p["observed_monotonic_ns"]) - int(start["observed_monotonic_ns"])
                if delay >= 0:
                    latencies.append(delay / 1e6)
        # requested_deadline_ns is currently zero. A sequence gap is not a missed
        # deadline: clients can be idle, cadence-limited or occluded.
        deadlines = [p for p in presents if int(p.get("requested_deadline_ns", 0)) > 0]
        outputs[name] = {
            "render_begins": sum(e["phase"] == "frame_begin" for e in selected),
            "successful_buffer_commits": sum(e["phase"] == "commit_end" and e["success"] == "true" and e["buffer"] == "true" for e in selected),
            "failed_commits": sum(e["phase"] == "commit_end" and e["success"] == "false" for e in selected),
            "presentation_events": len(presents),
            "commit_begin_to_present_observation": distribution(latencies),
            "refresh_ns": sorted({int(p["refresh_ns"]) for p in presents}),
            "presentation_flags": sorted({int(p["flags"]) for p in presents}),
            "missed_deadlines": (sum(int(p["presented_ns"]) > int(p["requested_deadline_ns"]) for p in deadlines)
                                 if deadlines and backend == "drm" else None),
            "missed_deadlines_reason": "No recorded requested deadlines; idle gaps and headless cadence are not misses" if not deadlines else "Only physical DRM deadlines are eligible",
        }
    return {"cpu_render": distribution([d / 1e6 for t, d in renders]),
            "gpu": {"query_zones": len(intervals), "zone_counts": dict(Counter(r.get("name", "unknown") for r in gpu)), "busy_union_ms": union_ns(intervals) / 1e6,
                    "zone_durations_inclusive": distribution([(b-a)/1e6 for a, b in intervals]),
                    "note": "Wall-time union across exported contexts avoids nested double counting; not summed device utilization or per-output frame latency"},
            "outputs": outputs, "event_count": len(events),
            "observed_monotonic_range_ns": ([min(int(e["observed_monotonic_ns"]) for e in events),
                                             max(int(e["observed_monotonic_ns"]) for e in events)] if events else None),
            "declared_backend": backend, "physical_scanout_validated": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu", required=True)
    parser.add_argument("--gpu", required=True)
    parser.add_argument("--messages", required=True)
    parser.add_argument("--backend", choices=("headless", "drm", "unknown"), default="unknown")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(summarize(args.cpu, args.gpu, args.messages, backend=args.backend), indent=2) + "\n")


if __name__ == "__main__":
    main()
