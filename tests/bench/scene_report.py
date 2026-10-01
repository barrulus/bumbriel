#!/usr/bin/env python3
"""Export retained cost traces and collect an auditable per-cell matrix."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

from scene_matrix import PHASES, WORKLOADS, process_summary, scene_coverage
from scene_trace import summarize


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result
    def constant(value):
        raise ValueError(f"non-finite JSON value: {value}")
    return json.loads(path.read_text(), object_pairs_hook=unique, parse_constant=constant)


def expected_cells(metadata):
    runs = metadata.get('runs')
    workloads = metadata.get('workloads')
    if type(runs) is not int or runs < 1:
        raise ValueError('metadata requires a positive integer runs')
    if not isinstance(workloads, list) or not workloads or any(w not in WORKLOADS for w in workloads) or len(set(workloads)) != len(workloads):
        raise ValueError('metadata workloads must be known, nonempty and unique')
    if metadata.get('backend') not in ('headless', 'drm') or not metadata.get('candidate'):
        raise ValueError('metadata requires backend and candidate')
    duration = metadata.get('duration')
    if type(duration) is not int or duration < 1:
        raise ValueError('metadata requires a positive sample duration')
    outputs = metadata.get('outputs', '').split(',')
    if metadata['backend'] == 'drm' and (not all(outputs) or len(set(outputs)) != len(outputs)):
        raise ValueError('DRM output names must be nonempty and unique')
    cells, unavailable = set(), set()
    revisions = ['candidate'] + (['baseline'] if metadata.get('baseline') else [])
    for run in range(1, runs + 1):
        for revision in revisions:
            for workload in workloads:
                prefix = (f'run-{run}', revision, workload)
                if metadata['backend'] == 'drm' and len(outputs) < 2 and workload == 'scene-two-output':
                    unavailable.add(prefix)
                else:
                    cells.update((*prefix, phase) for phase in PHASES)
    return cells, unavailable


def trace_digest(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def validate_config_evidence(cell, identity, metadata):
    _, revision, workload, phase = identity
    root = cell.parents[3]
    records = read_json(root / 'config-validation.json')
    if not isinstance(records, list):
        raise ValueError('config preflight must be an array')
    matches = [record for record in records if isinstance(record, dict) and
               (record.get('revision'), record.get('workload'), record.get('phase')) == (revision, workload, phase)]
    if len(matches) != 1:
        raise ValueError('missing or duplicate exact-binary config preflight')
    record = matches[0]
    if record.get('returncode') != 0 or record.get('binary') != metadata.get(revision):
        raise ValueError('config preflight failed or used a different binary')
    checked = root / 'config-validation' / revision / workload / phase
    digest = record.get('config_sha256')
    if not digest or trace_digest(checked / 'config.toml') != digest or trace_digest(cell.parent / f'{phase}.toml') != digest:
        raise ValueError('measured config differs from validated config')
    if not (checked / 'validation.log').read_text().startswith('config: ok ('):
        raise ValueError('config preflight lacks clean validator output')



def validate_output_snapshots(snapshots, workload, metadata):
    """Require the configured display workload at both measurement boundaries.

    Boundary checks cannot exclude a temporary seat loss between snapshots. They
    do prevent a detached/disabled or reconfigured display being reported as a
    valid cost measurement just because trace export and process sampling worked.
    """
    names = (['HEADLESS-1', 'HEADLESS-2'] if workload == 'scene-two-output' else ['HEADLESS-1']) if metadata['backend'] == 'headless' else metadata['outputs'].split(',')
    required = set(names)
    width, height = (int(value) for value in metadata['resolution'].split('x'))
    observed = []
    for filename, snapshot in snapshots.items():
        outputs = snapshot['outputs']
        colors = snapshot['color'].get('outputs') if isinstance(snapshot['color'], dict) else None
        state = {}
        for label, entries in (('outputs', outputs), ('color.outputs', colors)):
            if not isinstance(entries, list) or any(not isinstance(entry, dict) or not isinstance(entry.get('name'), str) for entry in entries):
                raise ValueError(f'{filename}: invalid {label} inventory')
            indexed = {entry['name']: entry for entry in entries}
            if len(indexed) != len(entries) or not required.issubset(indexed):
                raise ValueError(f'{filename}: missing or duplicate configured output in {label}')
            if {name for name, entry in indexed.items() if entry.get('enabled') is True} != required:
                raise ValueError(f'{filename}: configured output enablement differs in {label}')
            state[label] = indexed
        boundary = {}
        for name in names:
            output = state['outputs'][name]
            modes = output.get('modes')
            current = [mode for mode in modes if isinstance(mode, dict) and mode.get('current') is True] if isinstance(modes, list) else []
            if len(current) != 1 or any(type(current[0].get(key)) is not int for key in ('width', 'height', 'refresh_mhz')):
                raise ValueError(f'{filename}: {name} lacks one valid current mode')
            mode = current[0]
            if (mode['width'], mode['height']) != (width, height) or output.get('scale') != metadata['scale']:
                raise ValueError(f'{filename}: {name} mode/scale differs from configured workload')
            color = state['color.outputs'][name]
            color_keys = ('bit_depth', 'bit_depth_active', 'bit_depth_fallback_reason', 'fallback_reason',
                          'hdr_active', 'hdr_mode', 'hdr_requested', 'primaries', 'render_format',
                          'sdr_white', 'transfer_function')
            if any(key not in color for key in color_keys) or any(key not in output for key in ('position', 'transform', 'adaptive_sync')):
                raise ValueError(f'{filename}: {name} has incomplete output/color state')
            boundary[name] = {'mode': {key: mode[key] for key in ('width', 'height', 'refresh_mhz')},
                              **{key: output[key] for key in ('scale', 'position', 'transform', 'adaptive_sync')},
                              'color': color}
        observed.append(boundary)
    if observed[0] != observed[1]:
        raise ValueError('output mode/scale/color state changed during measurement')


def validate_cell(cell, identity, report, metadata):
    if not isinstance(report, dict):
        raise ValueError('summary must be a JSON object')
    run, revision, workload, phase = identity
    validate_config_evidence(cell, identity, metadata)
    if report.get('phase') != phase or report.get('workload') != workload:
        raise ValueError('summary identity disagrees with its cell directory')
    if 'profiling' in report or 'export_error' in report:
        raise ValueError('cell has an unresolved export failure or deferred timing')
    for name in ('trace.tracy', 'cpu.csv', 'gpu.csv', 'messages.csv'):
        if not (cell / name).is_file() or not (cell / name).stat().st_size:
            raise ValueError(f'missing or empty {name}')
    timing = summarize(cell / 'cpu.csv', cell / 'gpu.csv', cell / 'messages.csv', backend=metadata['backend'])
    if workload != 'idle' and (timing['cpu_render']['count'] == 0 or timing['gpu']['query_zones'] == 0):
        raise ValueError('updating workload has no observed CPU/GPU rendering')
    for key in ('cpu_render', 'gpu', 'outputs', 'event_count'):
        if report.get(key) != timing[key]:
            raise ValueError(f'stored {key} disagrees with current trace exports')
    snapshots = {}
    for name in ('before.json', 'after.json'):
        snapshot = read_json(cell / name)
        snapshots[name] = snapshot
        if not isinstance(snapshot, dict) or any(not isinstance(snapshot.get(key), (dict, list)) or
                isinstance(snapshot.get(key), dict) and 'unavailable' in snapshot[key]
                for key in ('effects', 'effect-frames', 'outputs', 'color', 'windows')):
            raise ValueError(f'incomplete IPC snapshot: {name}')
    validate_output_snapshots(snapshots, workload, metadata)
    samples = read_json(cell / 'process-samples.json')
    if not isinstance(samples, list) or len(samples) < 2:
        raise ValueError('missing process sample interval')
    stamps = [sample['monotonic_ns'] for sample in samples]
    if any(type(t) is not int for t in stamps) or stamps != sorted(stamps):
        raise ValueError('process sample clock is invalid')
    elapsed = (stamps[-1] - stamps[0]) / 1e9
    if elapsed < metadata['duration'] or not math.isclose(report['elapsed_sample_seconds'], elapsed, abs_tol=1e-6):
        raise ValueError('process sample duration is incomplete or disagrees with summary')
    costs = report.get('process_cost', {})
    compositor = [key for key, value in costs.items() if value.get('role') == 'compositor']
    if len(compositor) != 1 or process_summary(samples, int(compositor[0].split(':')[0])) != costs:
        raise ValueError('process cost does not match its raw samples')
    actions = read_json(cell / 'actions.json')
    if not isinstance(actions, list):
        raise ValueError('actions must be an array')
    if workload.startswith('scene-'):
        steps = sorted({action['step'] for action in actions if 'step' in action})
        if not set(range(8)).issubset(steps) or report.get('scene_cycle_complete') is not True or report.get('scene_cycle_steps') != steps:
            raise ValueError('scene cycle did not execute every step')
        if revision == 'candidate' and phase == 'active':
            output_name = 'HEADLESS-1' if metadata['backend'] == 'headless' else metadata['outputs'].split(',')[0]
            coverage = scene_coverage(actions, output_name)
            if report.get('scene_cost_validated') is not True or not coverage['validated'] or report.get('scene_coverage') != coverage:
                raise ValueError('candidate scene admission was not validated for every requested phase; see per-step coverage')
    return timing


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--exporter', type=Path, help='export/re-export all retained traces before collecting')
    args = parser.parse_args(argv)
    errors = []
    metadata = {}
    expected, expected_unavailable = set(), set()
    try:
        metadata = read_json(args.root / 'metadata.json')
        if not isinstance(metadata, dict):
            raise ValueError('metadata must be an object')
        expected, expected_unavailable = expected_cells(metadata)
    except (OSError, ValueError, TypeError, AttributeError) as error:
        errors.append({'path': 'metadata.json', 'error': str(error)})
        if not isinstance(metadata, dict):
            metadata = {}
    failures = []
    if args.exporter:
        for trace in sorted(args.root.glob('run-*/*/*/*/trace.tracy')):
            cell = trace.parent
            try:
                for name, flags in (('cpu', ['-u', '-f', 'Output::render']), ('gpu', ['-g']), ('messages', ['-m'])):
                    with (cell / f'{name}.csv').open('w') as output, (cell / f'{name}-export.log').open('w') as error:
                        subprocess.run([str(args.exporter.resolve()), *flags, str(trace)], stdout=output, stderr=error, check=True, timeout=60)
                path = cell / 'summary.json'
                report = read_json(path) if path.exists() else {}
                report.update(summarize(cell / 'cpu.csv', cell / 'gpu.csv', cell / 'messages.csv', backend=metadata['backend']))
                report.pop('profiling', None)
                report.pop('export_error', None)
                path.write_text(json.dumps(report, indent=2) + '\n')
            except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError) as error:
                failures.append({'cell': str(cell), 'error': str(error)})
                path = cell / 'summary.json'
                try:
                    report = read_json(path) if path.exists() else {}
                    if not isinstance(report, dict):
                        report = {}
                except (OSError, ValueError):
                    report = {}
                previous = {key: report.pop(key) for key in ('cpu_render', 'gpu', 'outputs', 'event_count') if key in report}
                if previous:
                    report['previous_timing_before_failed_reexport'] = previous
                report['profiling'] = 'current export failed; timing unavailable'
                report['export_error'] = str(error)
                path.write_text(json.dumps(report, indent=2) + '\n')
    rows, seen, seen_canonical, traces = [], set(), set(), {}
    for path in sorted(args.root.glob('run-*/*/*/*/summary.json')):
        run, revision, workload, phase, _ = path.relative_to(args.root).parts
        identity = (run, revision, workload, phase)
        seen.add(identity)
        match = re.fullmatch(r'run-(\d+)', run)
        canonical = (int(match[1]) if match else run, revision, workload, phase)
        cell_errors = []
        if canonical in seen_canonical:
            cell_errors.append('duplicate cell identity')
        seen_canonical.add(canonical)
        if identity not in expected:
            cell_errors.append('unexpected cell; it is not part of the declared matrix')
        report = {}
        try:
            report = read_json(path)
            validate_cell(path.parent, identity, report, metadata)
            digest = trace_digest(path.parent / 'trace.tracy')
            if digest in traces:
                raise ValueError(f'duplicate captured trace from {traces[digest]}')
            traces[digest] = str(path.relative_to(args.root))
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            cell_errors.append(str(error))
        if not isinstance(report, dict):
            report = {}
        errors.extend({'path': str(path.relative_to(args.root)), 'error': error} for error in cell_errors)
        elapsed = report.get('elapsed_sample_seconds', 0) if not cell_errors else 0
        costs = report.get('process_cost', {})
        processes = list(costs.values()) if isinstance(costs, dict) and not cell_errors else []
        compositor = [p for p in processes if p['role'] == 'compositor']
        helpers = [p for p in processes if p['role'] != 'compositor']
        timing_available = not cell_errors
        row = {'run': run, 'revision': revision, 'workload': workload, 'phase': phase,
               'compositor_cpu_percent_one_core': sum(p['cpu_seconds_observed'] for p in compositor) * 100 / elapsed if elapsed else None,
               'helper_cpu_percent_one_core': sum(p['cpu_seconds_observed'] for p in helpers) * 100 / elapsed if elapsed else None,
               'compositor_rss_peak_bytes': max((p['rss_peak_bytes'] for p in compositor), default=None),
               'helper_pid_count': len(helpers),
               'cpu_render_p95_ms': report.get('cpu_render', {}).get('p95_ms') if timing_available else None,
               # Retain the old key for readers of existing matrices. Its name
               # never establishes hardware utilization.
               'gpu_busy_wall_union_ms': report.get('gpu', {}).get('busy_union_ms') if timing_available else None,
               'gpu_query_elapsed_union_ms': report.get('gpu', {}).get('busy_union_ms') if timing_available else None,
               'scene_cost_validated': report.get('scene_cost_validated'),
               'scene_coverage': report.get('scene_coverage'),
               'timing_available': timing_available,
               'missed_deadlines': None,
               'valid': not cell_errors, 'validation_errors': cell_errors,
               'source': str(path.relative_to(args.root))}
        rows.append(row)
    for identity in sorted(expected - seen):
        errors.append({'path': '/'.join(identity), 'error': 'missing cell summary (possibly interrupted run)'})
    for trace in args.root.glob('run-*/*/*/*/trace.tracy'):
        if trace.relative_to(args.root).parts[:-1] not in expected:
            errors.append({'path': str(trace.relative_to(args.root)), 'error': 'unexpected captured cell outside declared matrix'})
    unavailable, seen_unavailable = [], set()
    for path in sorted(args.root.glob('run-*/*/*/unavailable.json')):
        identity = path.relative_to(args.root).parts[:-1]
        seen_unavailable.add(identity)
        try:
            entry = read_json(path)
            if identity not in expected_unavailable or not isinstance(entry, dict) or not entry.get('reason') or entry.get('phases') != list(PHASES):
                raise ValueError('unavailable marker is not a declared unavailable physical two-output workload')
            unavailable.append({'path': str(path.relative_to(args.root)), **entry})
        except (OSError, ValueError, TypeError) as error:
            errors.append({'path': str(path.relative_to(args.root)), 'error': str(error)})
    for identity in sorted(expected_unavailable - seen_unavailable):
        errors.append({'path': '/'.join(identity), 'error': 'missing explicit unavailable workload marker'})
    result = {'metadata': metadata, 'cells': rows, 'unavailable': unavailable, 'export_failures': failures,
              'validation': {'complete': not errors and not failures, 'expected_measured_cells': len(expected),
                             'expected_unavailable_cells': len(expected_unavailable) * len(PHASES),
                             'valid_measured_cells': sum(row['valid'] for row in rows), 'errors': errors},
              'notes': ['CPU percent uses one core and process sampling window including capture handshake',
                        'GPU query elapsed union spans timestamp-query intervals, including render_pass_submit waits; it is not hardware busy time, utilization, or per-frame GPU work',
                        'gpu_busy_wall_union_ms is a retained compatibility alias for gpu_query_elapsed_union_ms; raw gpu.busy_union_ms has the same elapsed-span meaning',
                        'Output mode, scale, enablement and color are checked at both sample boundaries; temporary changes between snapshots are not excluded',
                        'No requested presentation deadlines, so no missed-deadline count',
                        'Scene cells with scene_cost_validated=false include unproven admission/fallback and cannot certify successful scene cost']}
    (args.root / 'matrix.json').write_text(json.dumps(result, indent=2) + '\n')
    lines = ['# Scene cost observations', '', 'Per-run observations; raw evidence and limits are in `matrix.json`.', '',
             '| Run | Revision | Workload | Phase | CPU % one core | Helper CPU % | RSS MiB | Render p95 ms | GPU query elapsed union ms | Scene admitted |',
             '| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |']
    def number(value, scale=1):
        return 'unmeasured' if value is None else f'{value / scale:.3f}'
    for row in rows:
        lines.append('| ' + ' | '.join([row['run'], row['revision'], row['workload'], row['phase'],
            number(row['compositor_cpu_percent_one_core']), number(row['helper_cpu_percent_one_core']),
            number(row['compositor_rss_peak_bytes'], 1024*1024), number(row['cpu_render_p95_ms']), number(row['gpu_query_elapsed_union_ms']),
            'n/a' if row['scene_cost_validated'] is None else str(row['scene_cost_validated'])]) + ' |')
    lines += ['', 'GPU query elapsed union includes timestamp intervals around submit waits. It does not measure hardware busy time or utilization.',
              'Output presence, enablement, mode, scale and color must match at both sample boundaries; intervening changes are not excluded.',
              'Missed presentation deadlines are unmeasured. No physical scanout or HDR certification is inferred.',
              f'Valid measured cells: {sum(row["valid"] for row in rows)}/{len(expected)}. Explicit unavailable workloads: {len(unavailable)}.',
              f'Export failures: {len(failures)}. Validation errors: {len(errors)}. Complete: {not errors and not failures}.', '']
    (args.root / 'matrix.md').write_text('\n'.join(lines))
    if failures or errors:
        raise SystemExit(f'{len(failures)} trace exports failed; {len(errors)} validation errors; see matrix.json')


if __name__ == '__main__':
    main()
