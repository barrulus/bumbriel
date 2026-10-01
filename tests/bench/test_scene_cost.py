#!/usr/bin/env python3
"""Parser/accounting regression tests: python3 -m unittest discover -s tests/bench."""
import csv
import hashlib
import json
import shutil
from pathlib import Path
import tempfile
import tomllib
from types import SimpleNamespace
import unittest
from unittest import mock
import subprocess
import scene_report

from scene_matrix import PHASES, WORKLOADS, Session, config, process_summary, resource_summary, scene_coverage, validate_configs
from scene_trace import summarize, union_ns


class TraceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def write(self, name, header, data):
        path = self.root / name
        with path.open('w', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(header)
            writer.writerows(data)
        return path

    def fixture(self, *, idle=False, missing_gpu=False):
        cpu = self.write('cpu.csv', ['name', 'ns_since_start', 'exec_time_ns'], [] if idle else [['Output::render', 1000000, 1000000]])
        gpu = self.write('gpu.csv', ['Time from start of program', 'GPU execution time'], [] if idle or missing_gpu else [[1100000, 300000], [1200000, 100000]])
        events = []
        if not idle:
            for phase in ('commit_begin', 'commit_end', 'present'):
                events.append([f'umbriel.output|phase={phase}|output=A|commit_seq=2|buffer=true|success=true|observed_monotonic_ns=5000000|presented_ns=5000000|refresh_ns=16666666|flags=0|requested_deadline_ns=0', 1000000])
        return cpu, gpu, self.write('messages.csv', ['MessageName', 'total_ns'], events)

    def test_nested_queries_not_summed(self):
        self.assertEqual(union_ns([(1, 10), (2, 4), (8, 14), (20, 22)]), 15)
        report = summarize(*self.fixture(), backend='drm')
        self.assertEqual(report['gpu']['busy_union_ms'], .3)
        self.assertIsNone(report['outputs']['A']['missed_deadlines'])
        self.assertFalse(report['physical_scanout_validated'])

    def test_gpu_exporter_race_is_failure(self):
        with self.assertRaisesRegex(ValueError, 'empty GPU export'):
            summarize(*self.fixture(missing_gpu=True))

    def test_actual_idle_zero_does_not_mean_export_failure(self):
        self.assertEqual(summarize(*self.fixture(idle=True))['gpu']['query_zones'], 0)

    def test_idle_exporter_no_messages_string(self):
        cpu, gpu, messages = self.fixture(idle=True)
        messages.write_text("There are currently no messages!\n")
        self.assertEqual(summarize(cpu, gpu, messages)["event_count"], 0)

    def test_uncalibrated_gpu_origin_rejected(self):
        cpu, gpu, messages = self.fixture()
        gpu.write_text('Time from start of program,GPU execution time\n999999999999999,100\n')
        with self.assertRaisesRegex(ValueError, 'not aligned'):
            summarize(cpu, gpu, messages)

    def test_malformed_multiline_export_rejected(self):
        cpu, gpu, messages = self.fixture()
        cpu.write_text('name,ns_since_start,exec_time_ns\nOutput::render,0,5\ncontinued text\n')
        with self.assertRaisesRegex(ValueError, 'malformed CSV'):
            summarize(cpu, gpu, messages)

    def test_all_phase_configs_parse_and_baseline_omits_new_schema(self):
        args = SimpleNamespace(helper=Path('/tmp/synthetic'), resolution='1920x1080', scale=1.25, time_mode='advancing')
        for candidate in (True, False):
            for workload in WORKLOADS:
                for phase in PHASES:
                    document = tomllib.loads(config(['A', 'B'], phase, candidate, workload, self.root, args))
                    effects = document['effects']
                    for preset in effects.get('preset', {}).values():
                        if preset.get('kind') == 'window':
                            self.assertNotIn('animated', preset)
                    if not candidate:
                        self.assertNotIn('audio', effects)
                        self.assertEqual(document['include']['files'], [])
                    if phase in ('disabled', 'returned-off'):
                        self.assertNotIn('preset', effects)
                        self.assertEqual(effects['border'], '')
                    if phase == 'declared-unused':
                        self.assertEqual(effects['border'], '')
                        self.assertNotIn('workspace_presentation', document)

    def test_semantic_preflight_preserves_diagnostics_and_rejects_failure(self):
        args = SimpleNamespace(baseline=Path('/baseline'), candidate=Path('/candidate'),
                               workloads=['idle'], backend='headless', outputs='eDP-1',
                               output=self.root, helper=Path('/helper'), resolution='1920x1080',
                               scale=1, time_mode='advancing')
        def reject(args, **kwargs):
            document = tomllib.loads(Path(args[-1]).read_text())
            bad = document['effects']['border'] == 'cost-border'
            return SimpleNamespace(returncode=int(bad), stdout='' if bad else 'config: ok (config.toml)\n',
                                   stderr='unknown key fixture\n' if bad else '')
        with mock.patch('scene_matrix.subprocess.run', side_effect=reject) as checked:
            with self.assertRaisesRegex(RuntimeError, '2 benchmark configs rejected; no compositor started'):
                validate_configs(args)
        self.assertEqual(checked.call_count, 8)
        records = json.loads((self.root / 'config-validation.json').read_text())
        self.assertEqual(sum(record['returncode'] != 0 for record in records), 2)
        failed = self.root / 'config-validation/candidate/idle/active'
        self.assertIn('unknown key', (failed / 'validation.log').read_text())
        self.assertTrue((failed / 'border.glsl').is_file())

    def test_preflight_validator_timeout_is_archived_as_failure(self):
        args = SimpleNamespace(baseline=None, candidate=Path('/candidate'), workloads=['idle'],
                               backend='headless', outputs='eDP-1', output=self.root,
                               helper=Path('/helper'), resolution='1920x1080', scale=1,
                               time_mode='advancing')
        with mock.patch('scene_matrix.subprocess.run', side_effect=subprocess.TimeoutExpired('validator', 30)), \
             mock.patch('builtins.print'), \
             self.assertRaisesRegex(RuntimeError, '4 benchmark configs rejected'):
            validate_configs(args)
        records = json.loads((self.root / 'config-validation.json').read_text())
        self.assertTrue(all(record['returncode'] == -1 for record in records))
        self.assertIn('timed out', (self.root / 'config-validation/candidate/idle/active/validation.log').read_text())

    def test_resources_not_double_counted(self):
        result = resource_summary([{'memory_bytes': 10, 'nested': {'reserved_bytes': 10}}, {'memory_bytes': 0, 'nested': {'reserved_bytes': 0}}])
        self.assertEqual(result['paths']['/memory_bytes'], {'observed_max_bytes': 10, 'last_bytes': 0})

    def test_failed_reexport_cannot_keep_green_stale_timing(self):
        cell = self.root / 'run-1/candidate/idle/active'
        cell.mkdir(parents=True)
        (self.root / 'metadata.json').write_text('{"backend":"headless"}')
        (cell / 'trace.tracy').touch()
        (cell / 'summary.json').write_text(json.dumps({'cpu_render': {'p95_ms': 1}, 'gpu': {'busy_union_ms': 2}}))
        with mock.patch('sys.argv', ['scene_report.py', str(self.root), '--exporter', '/not-executed']), \
             mock.patch('scene_report.subprocess.run', side_effect=subprocess.CalledProcessError(1, 'exporter')), \
             self.assertRaises(SystemExit):
            scene_report.main()
        matrix = json.loads((self.root / 'matrix.json').read_text())
        self.assertFalse(matrix['cells'][0]['timing_available'])
        self.assertIsNone(matrix['cells'][0]['cpu_render_p95_ms'])
        self.assertIsNone(matrix['cells'][0]['gpu_busy_wall_union_ms'])
        self.assertEqual(len(matrix['export_failures']), 1)

    def test_pid_reuse_not_combined(self):
        def item(t, start, ticks):
            return {'monotonic_ns': t, 'processes': {'2': {'start_ticks': start, 'cpu_ticks': ticks, 'rss_bytes': 1, 'command': 'helper'}}}
        result = process_summary([item(1, 10, 5), item(2, 10, 8), item(3, 20, 2)], 1)
        self.assertEqual(len(result), 2)
        self.assertFalse(result["2:10"]["present_at_end"])
        # A PID reused by a different process is not this helper's survival.
        self.assertEqual(result['2:10']['cpu_seconds_observed'], 3 / __import__('os').sysconf('SC_CLK_TCK'))


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)
        self.metadata = {'backend': 'headless', 'candidate': '/candidate', 'baseline': None,
                         'runs': 1, 'duration': 2, 'outputs': 'eDP-1', 'workloads': ['idle'],
                         'resolution': '1920x1080', 'scale': 1.0}

    def write_json(self, path, data):
        path.write_text(json.dumps(data))

    def prepare(self):
        self.write_json(self.root / 'metadata.json', self.metadata)
        expected, unavailable = scene_report.expected_cells(self.metadata)
        validations = {}
        for identity in expected:
            cell = self.root.joinpath(*identity)
            cell.mkdir(parents=True)
            key = identity[1:]
            checked = self.root.joinpath('config-validation', *key)
            checked.mkdir(parents=True, exist_ok=True)
            contents = f'# valid {key}\n'
            (checked / 'config.toml').write_text(contents)
            (checked / 'validation.log').write_text('config: ok (config.toml)\n')
            (cell.parent / f'{identity[3]}.toml').write_text(contents)
            validations[key] = {'revision': identity[1], 'workload': identity[2], 'phase': identity[3],
                                'returncode': 0, 'binary': self.metadata[identity[1]],
                                'config_sha256': hashlib.sha256(contents.encode()).hexdigest()}
            (cell / 'trace.tracy').write_bytes('/'.join(identity).encode())
            (cell / 'cpu.csv').write_text('name,ns_since_start,exec_time_ns\n')
            (cell / 'gpu.csv').write_text('Time from start of program,GPU execution time\n')
            (cell / 'messages.csv').write_text('There are currently no messages!\n')
            if identity[2] != 'idle':
                (cell / 'cpu.csv').write_text('name,ns_since_start,exec_time_ns\nOutput::render,1000000,1000000\n')
                (cell / 'gpu.csv').write_text('Time from start of program,GPU execution time\n1100000,300000\n')
                (cell / 'messages.csv').write_text('MessageName,total_ns\numbriel.output|phase=commit_end|output=HEADLESS-1|commit_seq=1|buffer=true|success=true|observed_monotonic_ns=5000000,1000000\n')
            names = (['HEADLESS-1', 'HEADLESS-2'] if identity[2] == 'scene-two-output' else ['HEADLESS-1']) if self.metadata['backend'] == 'headless' else self.metadata['outputs'].split(',')
            snapshot = {key: {} for key in ('effects', 'effect-frames', 'windows')}
            snapshot['outputs'] = [{'name': name, 'enabled': True, 'scale': self.metadata['scale'],
                                    'position': {'x': index * 1920, 'y': 0}, 'transform': 'normal',
                                    'adaptive_sync': False, 'modes': [{'current': True, 'width': 1920,
                                    'height': 1080, 'refresh_mhz': 60000}]} for index, name in enumerate(names)]
            snapshot['color'] = {'outputs': [{'name': name, 'enabled': True, 'bit_depth': 8,
                                 'bit_depth_active': False, 'bit_depth_fallback_reason': '', 'fallback_reason': '',
                                 'hdr_active': False, 'hdr_mode': 'off', 'hdr_requested': False, 'primaries': 'none',
                                 'render_format': 'XR24', 'sdr_white': 203.0, 'transfer_function': 'none'} for name in names]}
            for name in ('before.json', 'after.json'):
                self.write_json(cell / name, snapshot)
            samples = [{'monotonic_ns': stamp, 'processes': {'2': {'start_ticks': 10, 'cpu_ticks': ticks,
                        'rss_bytes': 4096, 'command': 'compositor'}}} for stamp, ticks in ((1, 0), (2_000_000_001, 1))]
            self.write_json(cell / 'process-samples.json', samples)
            scene = identity[2].startswith('scene-')
            output_name = names[0]
            actions = ([{'step': i, 'state': {'effects': {'owners': [{'type': 'output', 'name': output_name, **{
                kind: {'active': True, 'memory_bytes': 4096} for kind in
                ('workspace_transition', 'workspace_presentation', 'window_presentation')}}]}}} for i in range(8)] if scene else [])
            self.write_json(cell / 'actions.json', actions)
            report = summarize(cell / 'cpu.csv', cell / 'gpu.csv', cell / 'messages.csv', backend=self.metadata['backend'])
            report.update(phase=identity[3], workload=identity[2], elapsed_sample_seconds=2,
                          process_cost=process_summary(samples, 2), scene_cycle_complete=scene,
                          scene_cycle_steps=list(range(8)) if scene else [])
            if scene and identity[1] == 'candidate' and identity[3] == 'active':
                report['scene_cost_validated'] = True
                report['scene_coverage'] = scene_coverage(actions, output_name)
            self.write_json(cell / 'summary.json', report)
        self.write_json(self.root / 'config-validation.json', list(validations.values()))
        for identity in unavailable:
            directory = self.root.joinpath(*identity)
            directory.mkdir(parents=True)
            self.write_json(directory / 'unavailable.json', {'reason': 'one physical output', 'phases': PHASES})

    def run_report(self, valid):
        with mock.patch('sys.argv', ['scene_report.py', str(self.root)]):
            if valid:
                scene_report.main()
            else:
                with self.assertRaises(SystemExit):
                    scene_report.main()
        result = json.loads((self.root / 'matrix.json').read_text())
        self.assertEqual(result['validation']['complete'], valid)
        return result

    def cell(self, phase='active', workload='idle'):
        return self.root / 'run-1' / 'candidate' / workload / phase

    def test_default_one_display_matrix_has_96_measured_and_24_unavailable_cells(self):
        self.metadata.update(backend='drm', baseline='/baseline', runs=3, duration=30, workloads=list(WORKLOADS))
        measured, unavailable = scene_report.expected_cells(self.metadata)
        self.assertEqual(len(measured), 96)
        self.assertEqual(len(unavailable) * len(PHASES), 24)

    def test_complete_phase_matrix_passes_with_actual_idle_zero_exports(self):
        self.prepare()
        result = self.run_report(True)
        self.assertEqual(result['validation']['valid_measured_cells'], 4)

    def test_lost_native_output_invalidates_timing_and_preserves_raw_evidence(self):
        self.metadata.update(backend='drm', outputs='DP-7')
        self.prepare()
        cell = self.cell()
        raw_summary = (cell / 'summary.json').read_bytes()
        raw_trace = (cell / 'trace.tracy').read_bytes()
        after = json.loads((cell / 'after.json').read_text())
        after['outputs'] = []
        after['color']['outputs'] = []
        self.write_json(cell / 'after.json', after)
        result = self.run_report(False)
        row = next(row for row in result['cells'] if row['phase'] == 'active')
        self.assertFalse(row['timing_available'])
        for key in ('cpu_render_p95_ms', 'gpu_busy_wall_union_ms', 'gpu_query_elapsed_union_ms', 'compositor_cpu_percent_one_core'):
            self.assertIsNone(row[key])
        self.assertIn('missing or duplicate configured output', row['validation_errors'][0])
        self.assertEqual((cell / 'summary.json').read_bytes(), raw_summary)
        self.assertEqual((cell / 'trace.tracy').read_bytes(), raw_trace)

    def test_headless_output_count_follows_workload_not_physical_metadata(self):
        self.metadata.update(outputs='DP-7,HDMI-A-3', workloads=['idle', 'scene-two-output'])
        self.prepare()
        self.run_report(True)
        cell = self.cell(workload='scene-two-output')
        after = json.loads((cell / 'after.json').read_text())
        after['outputs'] = after['outputs'][:1]
        self.write_json(cell / 'after.json', after)
        result = self.run_report(False)
        self.assertEqual(result['validation']['valid_measured_cells'], 7)

    def test_output_enablement_mode_scale_and_color_changes_rejected(self):
        self.metadata.update(backend='drm', outputs='DP-7,HDMI-A-3')
        self.prepare()
        self.run_report(True)
        path = self.cell() / 'after.json'
        original = path.read_text()
        mutations = [('outputs', 'enabled', False), ('color', 'enabled', False),
                     ('outputs', 'scale', 1.25), ('color', 'render_format', 'XR30')]
        for category, key, value in mutations:
            with self.subTest(category=category, key=key):
                after = json.loads(original)
                entries = after['outputs'] if category == 'outputs' else after['color']['outputs']
                entries[1][key] = value
                self.write_json(path, after)
                self.assertEqual(self.run_report(False)['validation']['valid_measured_cells'], 3)
        for key, value in (('width', 1280), ('refresh_mhz', 144000)):
            with self.subTest(mode=key):
                after = json.loads(original)
                after['outputs'][1]['modes'][0][key] = value
                self.write_json(path, after)
                self.run_report(False)

    def test_missing_color_output_and_duplicate_inventory_rejected(self):
        self.prepare()
        path = self.cell() / 'before.json'
        original = path.read_text()
        for mutation in ('missing_color', 'duplicate', 'wrong_name'):
            with self.subTest(mutation=mutation):
                before = json.loads(original)
                if mutation == 'missing_color':
                    before['color']['outputs'] = []
                elif mutation == 'duplicate':
                    before['outputs'].append(before['outputs'][0].copy())
                else:
                    before['outputs'][0]['name'] = 'HEADLESS-9'
                self.write_json(path, before)
                self.run_report(False)

    def test_missing_failed_or_changed_config_preflight_rejects_timing(self):
        self.prepare()
        path = self.root / 'config-validation.json'
        records = json.loads(path.read_text())
        self.write_json(path, [record for record in records if record['phase'] != 'active'])
        result = self.run_report(False)
        self.assertFalse(next(row for row in result['cells'] if row['phase'] == 'active')['timing_available'])
        self.write_json(path, records)
        next(record for record in records if record['phase'] == 'active')['returncode'] = 1
        self.write_json(path, records)
        self.run_report(False)
        next(record for record in records if record['phase'] == 'active')['returncode'] = 0
        self.write_json(path, records)
        (self.cell().parent / 'active.toml').write_text('unvalidated mutation')
        self.run_report(False)

    def test_missing_interrupted_cell_fails_and_keeps_other_evidence(self):
        self.prepare()
        (self.cell() / 'summary.json').unlink()
        result = self.run_report(False)
        self.assertEqual(result['validation']['valid_measured_cells'], 3)
        self.assertTrue(any('missing cell' in e['error'] for e in result['validation']['errors']))

    def test_duplicate_run_alias_and_copied_trace_are_rejected(self):
        self.prepare()
        shutil.copytree(self.cell(), self.root / 'run-01/candidate/idle/active')
        shutil.copyfile(self.cell() / 'trace.tracy', self.cell('disabled') / 'trace.tracy')
        result = self.run_report(False)
        errors = [e['error'] for e in result['validation']['errors']]
        self.assertTrue(any('duplicate cell' in error for error in errors))
        self.assertTrue(any('duplicate captured trace' in error for error in errors))

    def test_malformed_or_missing_exports_cannot_reuse_stored_timing(self):
        self.prepare()
        (self.cell() / 'cpu.csv').write_text('broken export\n')
        (self.cell('disabled') / 'gpu.csv').unlink()
        result = self.run_report(False)
        self.assertEqual(result['validation']['valid_measured_cells'], 2)
        self.assertTrue(all(not row['timing_available'] for row in result['cells'] if row['phase'] in ('active', 'disabled')))

    def test_malformed_summary_and_duplicate_json_keys_fail_cleanly(self):
        self.prepare()
        (self.cell() / 'summary.json').write_text('{broken')
        (self.cell('disabled') / 'summary.json').write_text('{"phase":"disabled","phase":"active"}')
        result = self.run_report(False)
        self.assertEqual(result['validation']['valid_measured_cells'], 2)

    def test_scene_flag_and_raw_admission_both_required(self):
        self.metadata['workloads'] = ['scene-single-output']
        self.prepare()
        self.run_report(True)
        cell = self.cell(workload='scene-single-output')
        report = json.loads((cell / 'summary.json').read_text())
        report['scene_cost_validated'] = False
        self.write_json(cell / 'summary.json', report)
        self.run_report(False)
        report['scene_cost_validated'] = True
        self.write_json(cell / 'summary.json', report)
        actions = json.loads((cell / 'actions.json').read_text())
        for action in actions:
            action['state']['effects']['owners'][0]['window_presentation']['active'] = False
        self.write_json(cell / 'actions.json', actions)
        self.run_report(False)

    def test_unavailable_two_output_is_explicit_and_only_physical(self):
        self.metadata.update(backend='drm', workloads=['idle', 'scene-two-output'])
        self.prepare()
        result = self.run_report(True)
        self.assertEqual(result['validation']['expected_unavailable_cells'], 4)
        marker = self.root / 'run-1/candidate/scene-two-output/unavailable.json'
        marker.unlink()
        self.run_report(False)
        self.write_json(marker, {'reason': 'one display', 'phases': PHASES})
        self.metadata['backend'] = 'headless'
        self.write_json(self.root / 'metadata.json', self.metadata)
        self.run_report(False)

    def test_repeated_metadata_workload_cannot_double_count_cells(self):
        self.prepare()
        self.metadata['workloads'] = ['idle', 'idle']
        self.write_json(self.root / 'metadata.json', self.metadata)
        self.run_report(False)

    def test_failed_export_marker_and_partial_scene_cycle_fail(self):
        self.metadata['workloads'] = ['scene-single-output']
        self.prepare()
        cell = self.cell(workload='scene-single-output')
        report = json.loads((cell / 'summary.json').read_text())
        report['export_error'] = 'export process failed'
        self.write_json(cell / 'summary.json', report)
        self.run_report(False)
        report.pop('export_error')
        self.write_json(cell / 'summary.json', report)
        actions = json.loads((cell / 'actions.json').read_text())[:-1]
        self.write_json(cell / 'actions.json', actions)
        self.run_report(False)

    def test_successful_close_does_not_conceal_failed_window_open(self):
        self.metadata['workloads'] = ['scene-single-output']
        self.prepare()
        cell = self.cell(workload='scene-single-output')
        actions = json.loads((cell / 'actions.json').read_text())
        actions[5]['state']['effects']['owners'][0]['window_presentation'] = {
            'active': False, 'memory_bytes': 0, 'fallback': 'unsupported_capability'}
        self.write_json(cell / 'actions.json', actions)
        coverage = scene_coverage(actions, 'HEADLESS-1')
        self.assertEqual(coverage['phases']['window-close']['admitted'], 1)
        self.assertEqual(coverage['phases']['window-open']['fallbacks'], ['unsupported_capability'])
        self.assertFalse(coverage['validated'])
        self.run_report(False)

    def test_other_output_cannot_supply_primary_admission(self):
        actions = [{'step': 5, 'state': {'effects': {'owners': [
            {'type': 'output', 'name': 'HEADLESS-1', 'window_presentation': {'active': False, 'memory_bytes': 0}},
            {'type': 'output', 'name': 'HEADLESS-2', 'window_presentation': {'active': True, 'memory_bytes': 4096}},
        ]}}}]
        coverage = scene_coverage(actions, 'HEADLESS-1')
        self.assertEqual(coverage['phases']['window-open']['admitted'], 0)
        self.assertEqual(coverage['phases']['window-open']['unobserved'], 1)

    def test_updating_workload_cannot_claim_empty_trace_as_success(self):
        self.metadata['workloads'] = ['video-light']
        self.prepare()
        cell = self.cell(workload='video-light')
        (cell / 'cpu.csv').write_text('name,ns_since_start,exec_time_ns\n')
        (cell / 'gpu.csv').write_text('Time from start of program,GPU execution time\n')
        (cell / 'messages.csv').write_text('There are currently no messages!\n')
        report = json.loads((cell / 'summary.json').read_text())
        report.update(summarize(cell / 'cpu.csv', cell / 'gpu.csv', cell / 'messages.csv'))
        self.write_json(cell / 'summary.json', report)
        result = self.run_report(False)
        self.assertTrue(any('no observed CPU/GPU' in error['error'] for error in result['validation']['errors']))

    def test_outputs_use_wayland_cli_but_timed_snapshots_skip_it(self):
        session = object.__new__(Session)
        session.binary = Path('/instrumented-compositor')
        session.env = {'WAYLAND_DISPLAY': 'private-wayland', 'XDG_RUNTIME_DIR': '/private-runtime'}
        with mock.patch('scene_matrix.command', return_value=SimpleNamespace(stdout='[]')) as command:
            self.assertEqual(session.ipc('outputs', '--json'), '[]')
            self.assertEqual(command.call_args.args[0], [session.binary, 'outputs', '--json'])
            self.assertEqual(command.call_args.kwargs['env'], session.env)
        with mock.patch.object(session, 'ipc', return_value='{}') as ipc:
            timed = session.snapshot(include_outputs=False)
            self.assertNotIn('outputs', timed)
            self.assertNotIn(mock.call('outputs', '--json'), ipc.call_args_list)
            session.snapshot()
            self.assertIn(mock.call('outputs', '--json'), ipc.call_args_list)


if __name__ == '__main__':
    unittest.main()
