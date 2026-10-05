"""Finite named scenarios, exclusive evidence and cleanup failure boundaries."""
from contextlib import contextmanager, redirect_stdout, redirect_stderr
import copy
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import bench_probe as probe
import bench_scenarios as scenarios
import bench_session as session

SPEC = importlib.util.spec_from_file_location('scenario_serial_fakes', ROOT / 'test/bench_probe_test.py')
wire = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(wire)


class ScenarioConsole:
    instances = []

    def __init__(self, connection=None, on_event=None):
        self.connection = connection
        self.on_event = on_event or (lambda *a, **k: None)
        self.synchronized = True
        self.calls = []
        self.setting = 10
        self.fail_setting = False
        self.fail_restore = False
        self.now = 0
        self.instances.append(self)

    def emit(self, event, **fields):
        self.on_event(event, **fields)

    def clock(self):
        return self.now

    def sleep(self, duration):
        self.now += duration

    def drain_startup(self, duration):
        pass

    def identify(self, **kwargs):
        return self.command('version', **kwargs)

    def command(self, name, **kwargs):
        self.calls.append((name, kwargs))
        result = dict(ok=True, command=name)
        if name == 'read-config':
            result['config'] = dict(raw=[0, 1000, 0])
        if name == 'read-state':
            result['state_blocks'] = [dict(block=0, running=False, raw_alarm=0, alarm_flag=False),
                                      dict(block=2, raw_speed=0)]
        if name in ('tuning', 'control'):
            tokens = kwargs['driver_args']
            if 'set' in tokens:
                value = int(tokens[-1])
                if self.fail_setting or self.fail_restore and value == 10:
                    result.update(ok=False, outcome='uncertain', uncertain=True)
                else:
                    self.setting = value
            fields = probe.TUNING_FIELDS['filters'] if name == 'tuning' else probe.CONTROL_FIELDS
            values = [0] * len(fields)
            values[fields.index('input-filter' if name == 'tuning' else 'lock-delay')] = self.setting
            result['observation'] = dict(raw=values)
        self.emit('fake_command', name=name, arguments=kwargs, result=result)
        return result


class ScenarioArgumentsTest(unittest.TestCase):
    def parse(self, args):
        with redirect_stderr(io.StringIO()):
            return scenarios.arguments(['--plan-only'] + args)

    def test_default_quick_is_read_only_named_plan(self):
        args = self.parse([])
        self.assertEqual(args.scenario, 'quick')
        self.assertEqual(args.count, 10)
        console = ScenarioConsole()
        record = dict(before={'config': {'config': {'raw': [0, 1000, 0]}},
                              'load': dict(zip(probe.LOAD_FIELDS, (0, 0, 0)))})
        with patch.object(probe, 'campaign') as campaign:
            scenarios.run_scenario(console, args, record)
        self.assertEqual(campaign.call_args.args[1], 'stress')
        self.assertIsNone(campaign.call_args.kwargs['load'])
        self.assertEqual(console.calls, [('read-config', dict(address=1, timeout_s=5))])

    def test_crossed_or_unbounded_options_fail_before_open(self):
        examples = [
            ['--load', '1000', '0', '64'], ['--phase', 'forward'],
            ['--native', 'driver'], ['--setting', 'lock-delay', '--value', '10'],
            ['--persistence-kind', 'save'], ['--count', '101'], ['--interval', 'nan'],
            ['--stop-policy', 'direct'],
            ['--timeout', 'inf'], ['--address', '0'], ['--evidence-records', '1'],
            ['--scenario', 'native-read', '--native', 'io', '--native-args', 'set', 'x0-function', 'none'],
            ['--scenario', 'position', '--address', '2'],
            ['--scenario', 'position', '--count', '1'],
            ['--scenario', 'homing-prerequisites', '--interval', '0'],
            ['--scenario', 'velocity'], ['--scenario', 'persistence'],
        ]
        for argv in examples:
            with self.subTest(argv=argv), patch.object(session, 'open_port') as port:
                with self.assertRaises((SystemExit, ValueError)):
                    self.parse(argv)
                port.assert_not_called()

    def test_each_explicit_native_read_reuses_checked_campaign(self):
        for native, tokens in [('driver', ['read']), ('io', ['read']), ('control', ['read']),
                               ('tuning', ['filters', 'read']), ('segment', ['position', '15', 'read'])]:
            args = self.parse(['--scenario', 'native-read', '--native', native, '--native-args'] + tokens)
            console = ScenarioConsole()
            record = dict(before={'config': {'config': {'raw': [0, 1000, 0]}}})
            with patch.object(probe, 'driver_read_campaign') as campaign:
                scenarios.run_scenario(console, args, record)
            self.assertEqual(campaign.call_args.kwargs['command'], native)
            self.assertEqual(campaign.call_args.kwargs['driver_args'], tuple(tokens))

    def test_plan_only_does_not_open_port_or_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'plan'
            with patch.object(scenarios, 'run_recorded') as run, redirect_stdout(io.StringIO()):
                self.assertEqual(scenarios.main(['--out', str(prefix), '--plan-only']), 0)
            run.assert_not_called()
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_changed_configuration_is_failed_without_implicit_repair(self):
        console = ScenarioConsole()
        record = dict(before={'config': {'config': {'raw': [1, 1000, 0]}}})
        args = self.parse(['--scenario', 'native-read', '--native', 'driver'])
        with patch.object(probe, 'driver_read_campaign'):
            with self.assertRaisesRegex(probe.BenchError, 'configuration changed'):
                scenarios.run_scenario(console, args, record)
        self.assertNotIn('workload_verified', record)
        self.assertEqual(console.calls, [('read-config', dict(address=1, timeout_s=5))])

    def test_nonrepeating_scenario_records_one_attempt(self):
        args = self.parse(['--scenario', 'native-read', '--native', 'driver'])
        self.assertEqual(args.count, 1)

    def test_unknown_direct_scenario_never_runs_or_claims_workload(self):
        console = ScenarioConsole()
        args = self.parse([])
        args.scenario = 'typo'
        with self.assertRaises(ValueError):
            scenarios.run_scenario(console, args, {})
        self.assertEqual(console.calls, [])

    def test_angle_primary_failure_and_restore_failure_are_separate(self):
        console = ScenarioConsole()
        args = self.parse(['--scenario', 'angle', '--arguments', '36', 'deg', 'motor', 'positive', 'reject', '60', 'configured'])
        record = {'campaigns': [{'mode': 'move-angle', 'cleanup': 'drive_reported_standstill', 'ok': False}]}
        with patch.object(scenarios.motion, 'profile_snapshot', return_value={'original': [0]*6}), \
                patch.object(probe, 'move_campaign', side_effect=probe.BenchError('original move uncertain')), \
                patch.object(scenarios.motion, 'restore_profile', side_effect=probe.BenchError('restore unknown')):
            with self.assertRaisesRegex(probe.BenchError, 'original move uncertain'):
                scenarios._angle(console, args, record)
        self.assertIn('restore unknown', record['profile_restoration']['error'])

    def test_angle_framing_failure_never_restores(self):
        console = ScenarioConsole()
        console.synchronized = False
        args = self.parse(['--scenario', 'angle', '--arguments', '36', 'deg', 'motor', 'positive', 'reject', '60', 'configured'])
        record = {'campaigns': [{'mode': 'move-angle', 'cleanup': 'unknown', 'ok': False}]}
        with patch.object(scenarios.motion, 'profile_snapshot', return_value={'original': [0]*6}), \
                patch.object(probe, 'move_campaign', side_effect=probe.BenchError('framing failed')), \
                patch.object(scenarios.motion, 'restore_profile') as restore:
            with self.assertRaisesRegex(probe.BenchError, 'framing failed'):
                scenarios._angle(console, args, record)
        restore.assert_not_called()
        self.assertFalse(record['profile_restoration']['known'])

    def test_load_capture_precedes_host_workload_restoration(self):
        args = self.parse(['--scenario', 'load', '--load', '1000', '0', '64'])
        console = ScenarioConsole()
        record = dict(before={'config': {'config': {'raw': [0, 1000, 0]}},
                              'load': dict(zip(probe.LOAD_FIELDS, (0, 0, 0)))})
        with patch.object(probe, 'campaign'):
            scenarios.run_scenario(console, args, record)
        self.assertEqual(console.calls[:2], [('load', dict(timeout_s=5)),
                                            ('load', dict(load=(0, 0, 0), timeout_s=5))])
        self.assertTrue(record['load_cleanup']['restored'])
        self.assertIn('load_window', record)

    def test_interrupted_load_preserves_error_and_restores_only_host_fixture(self):
        args = self.parse(['--scenario', 'load', '--load', '1000', '0', '64'])
        console = ScenarioConsole()
        record = dict(before={'load': dict(zip(probe.LOAD_FIELDS, (0, 0, 0)))})
        with patch.object(probe, 'campaign', side_effect=KeyboardInterrupt()):
            with self.assertRaises(KeyboardInterrupt):
                scenarios.run_scenario(console, args, record)
        self.assertEqual([name for name, _ in console.calls], ['load', 'load'])
        self.assertTrue(record['load_cleanup']['restored'])
        self.assertNotIn('workload_verified', record)

    def test_homing_prerequisite_inventory_does_not_execute_home(self):
        args = self.parse(['--scenario', 'homing-prerequisites'])
        console = ScenarioConsole()
        record = dict(before={'config': {'config': {'raw': [0, 1000, 0]}}})
        scenarios.run_scenario(console, args, record)
        self.assertEqual([name for name, _ in console.calls], ['caps', 'wiring', 'io', 'read-config'])
        self.assertFalse(record['homing']['execution_attempted'])
        self.assertIn('method-specific', record['homing']['readiness'])

    def test_persistence_preview_never_executes_or_claims_durability(self):
        args = self.parse(['--scenario', 'persistence-plan', '--persistence-kind', 'save'])
        console = ScenarioConsole()
        record = dict(before={'config': {'config': {'raw': [0, 1000, 0]}}})
        with patch.object(probe, 'persistence_campaign') as campaign:
            scenarios.run_scenario(console, args, record)
        for key in ('execute', 'verify', 'finish'):
            self.assertIs(campaign.call_args.kwargs[key], False)
        self.assertFalse(record['durability_verified'])


class SettingsScenarioTest(unittest.TestCase):
    def args(self, value=11):
        return scenarios.arguments(['--plan-only', '--scenario', 'settings', '--setting', 'input-filter', '--value', str(value)])

    def test_checked_change_and_exact_original_restore_once(self):
        console = ScenarioConsole()
        record = {}
        scenarios._setting(console, self.args(), record)
        writes = [args['driver_args'] for name, args in console.calls
                  if name == 'tuning' and 'set' in args['driver_args']]
        self.assertEqual(writes, [('filters', 'set', 'input-filter', '11'), ('filters', 'set', 'input-filter', '10')])
        self.assertTrue(record['setting']['restored'])
        self.assertEqual(console.setting, 10)

    def test_filter_effects_and_changed_readback_require_rebuilt_io_evidence_before_restore(self):
        class InvalidatingConsole(ScenarioConsole):
            def __init__(self):
                super().__init__()
                self.io_fresh = False
                self.stationary_fresh = True
                self.observed_filter = 10
                self.invalidations = []
                self.write_preconditions = []

            def invalidate(self, reason):
                self.io_fresh = False
                self.stationary_fresh = False
                self.invalidations.append(reason)

            def command(self, name, **kwargs):
                tokens = kwargs.get('driver_args', ())
                if name == 'tuning' and 'set' in tokens:
                    flags = (self.io_fresh, self.stationary_fresh)
                    self.write_preconditions.append(flags)
                    if not all(flags):
                        self.calls.append((name, kwargs))
                        return dict(ok=False, result='invalid', tx_bytes=0)
                    result = super().command(name, **kwargs)
                    self.invalidate('accepted_filter_tx')
                    return result
                result = super().command(name, **kwargs)
                if name == 'tuning' and 'read' in tokens and self.observed_filter != self.setting:
                    self.observed_filter = self.setting
                    self.invalidate('changed_filter_observation')
                elif name == 'io': self.io_fresh = True
                elif name == 'read-state': self.stationary_fresh = True
                return result

        console = InvalidatingConsole()
        # The full session supplied identity/configuration; I/O still needs an
        # explicit refresh. A changed group read invalidates dependent evidence.
        record = {}
        scenarios._setting(console, self.args(), record)
        self.assertTrue(record['setting']['restored'])
        self.assertEqual(console.setting, 10)
        self.assertEqual(console.write_preconditions, [(True, True)] * 2)
        self.assertEqual(console.invalidations.count('accepted_filter_tx'), 2)
        self.assertEqual(console.invalidations.count('changed_filter_observation'), 2)
        commands = [name for name, _ in console.calls]
        write_indexes = [i for i, (name, args) in enumerate(console.calls)
                         if name == 'tuning' and 'set' in args['driver_args']]
        self.assertEqual(commands[write_indexes[0] + 1:write_indexes[1]],
                         ['tuning', 'read-identity', 'read-config', 'io', 'read-state'])

    def test_noop_or_large_setting_change_never_yields_write(self):
        for value in (10, 12, 0, 65535):
            console = ScenarioConsole()
            with self.assertRaisesRegex(probe.BenchError, 'one native-unit change'):
                scenarios._setting(console, self.args(value), {})
            self.assertFalse(any('set' in args.get('driver_args', ()) for _, args in console.calls))

    def test_uncertain_application_preserves_backup_and_never_restores_or_replays(self):
        console = ScenarioConsole()
        console.fail_setting = True
        record = {}
        with self.assertRaisesRegex(probe.BenchError, 'uncertain'):
            scenarios._setting(console, self.args(), record)
        self.assertFalse(record['setting']['restored'])
        self.assertEqual(len([args for _, args in console.calls if 'set' in args.get('driver_args', ())]), 1)
        self.assertEqual(record['setting']['before']['observation']['raw'][0], 10)

    def test_unknown_restore_remains_failure_and_never_retries(self):
        console = ScenarioConsole()
        console.fail_restore = True
        record = {}
        with self.assertRaisesRegex(probe.BenchError, 'uncertain'):
            scenarios._setting(console, self.args(), record)
        self.assertFalse(record['setting']['restored'])
        self.assertEqual(len([args for _, args in console.calls if 'set' in args.get('driver_args', ())]), 2)


class RecordedSessionTest(unittest.TestCase):
    def setUp(self):
        ScenarioConsole.instances.clear()
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.prefix = Path(self.tmp.name) / 'evidence'
        self.opens = 0
        self.closes = 0

    @contextmanager
    def opener(self, *args):
        self.opens += 1
        try:
            yield object()
        finally:
            self.closes += 1

    def recorded(self, run, **kwargs):
        def execute(console, record):
            run(console, record)
            record['workload_verified'] = True
        with patch.object(session, 'Console', ScenarioConsole):
            return session.run_recorded(port='fake', out=self.prefix, scenario='fake', inputs={},
                                        run=execute, port_opener=self.opener, **kwargs)

    def summary(self):
        return json.loads(self.prefix.with_suffix('.json').read_text())

    def test_one_owner_fixed_snapshots_and_streamed_bounded_campaign_tail(self):
        def run(console, record):
            for index in range(35):
                console.emit('summary', mode='probe', number=index, ok=True)
        result = self.recorded(run)
        self.assertTrue(result['ok'])
        self.assertEqual((self.opens, self.closes), (1, 1))
        self.assertEqual(result['campaigns_seen'], 35)
        self.assertEqual(len(result['campaigns']), 16)
        self.assertEqual(result['campaigns'][0]['number'], 19)
        lines = [json.loads(line) for line in self.prefix.with_suffix('.jsonl').read_text().splitlines()]
        self.assertEqual(len([line for line in lines if line['event'] == 'summary']), 35)

    def test_jsonl_file_bytes_match_evidence_capacity_on_windows(self):
        result = self.recorded(lambda console, record:
                               console.emit('summary', mode='probe', ok=True, text='motor \u00b0'))
        raw = self.prefix.with_suffix('.jsonl').read_bytes()
        self.assertNotIn(b'\r\n', raw)
        self.assertEqual(len(raw), result['evidence']['bytes'])
        self.assertEqual(len(raw.splitlines()), result['evidence']['records'])
        self.assertEqual(self.summary()['evidence'], result['evidence'])

    def test_existing_artifacts_are_not_replaced_and_port_never_opens(self):
        for suffix in ('.json', '.jsonl'):
            with self.subTest(suffix=suffix):
                prefix = Path(self.tmp.name) / suffix[1:]
                existing = prefix.with_suffix(suffix)
                existing.write_text('previous evidence')
                with patch.object(session, 'Console', ScenarioConsole):
                    with self.assertRaises(FileExistsError):
                        session.run_recorded(port='fake', out=prefix, scenario='fake', inputs={},
                            run=lambda *_: None, port_opener=self.opener)
                self.assertEqual(existing.read_text(), 'previous evidence')
        self.assertEqual(self.opens, 0)

    def test_failure_only_collects_cached_diagnostics_and_retains_original_error(self):
        def run(console, record):
            raise probe.BenchError('work failed')
        with self.assertRaisesRegex(probe.BenchError, 'work failed'):
            self.recorded(run)
        result = self.summary()
        self.assertFalse(result['ok'])
        self.assertEqual(result['error'], 'work failed')
        self.assertEqual(set(result['failure_diagnostics']), set(session.CACHED))
        calls = ScenarioConsole.instances[-1].calls
        self.assertEqual([name for name, _ in calls[-len(session.CACHED):]], list(session.CACHED))
        self.assertNotIn('after', result)

    def test_disconnect_skips_all_failure_diagnostics(self):
        def run(console, record):
            console.synchronized = False
            raise probe.BenchError('device disconnected')
        with self.assertRaisesRegex(probe.BenchError, 'device disconnected'):
            self.recorded(run)
        self.assertEqual(self.summary()['failure_diagnostics'], {})
        self.assertIn('framing unavailable', self.summary()['failure_diagnostics_error'])

    def test_failed_campaign_summary_forbids_new_motor_after_reads(self):
        def run(console, record):
            console.emit('summary', mode='probe', ok=False, error='workload did not run')
        with self.assertRaises(probe.BenchError):
            self.recorded(run)
        calls = ScenarioConsole.instances[-1].calls
        self.assertEqual(sum(name.startswith('read-') for name, _ in calls), 3)

    def test_early_failed_campaign_cannot_be_erased_by_summary_tail(self):
        def run(console, record):
            console.emit('summary', mode='probe', ok=False, error='first workload failed')
            for _ in range(16):
                console.emit('summary', mode='diagnostic', ok=True)
        with self.assertRaises(probe.BenchError):
            self.recorded(run)
        self.assertFalse(self.summary()['ok'])

    def test_evidence_exhaustion_is_failure_with_exclusive_summary(self):
        def run(console, record):
            console.emit('summary', ok=True)
        with self.assertRaises(probe.BenchError):
            self.recorded(run, max_records=2)
        result = self.summary()
        self.assertFalse(result['ok'])
        self.assertTrue(result['evidence']['exhausted'])
        self.assertEqual((self.opens, self.closes), (1, 1))

    def test_empty_callback_cannot_claim_requested_workload(self):
        with patch.object(session, 'Console', ScenarioConsole):
            with self.assertRaisesRegex(probe.BenchError, 'workload did not complete'):
                session.run_recorded(port='fake', out=self.prefix, scenario='quick', inputs={},
                    run=lambda *_: None, port_opener=self.opener)
        self.assertFalse(self.summary()['ok'])
        self.assertNotIn('after', self.summary())

    def test_snapshot_preserves_local_transport_configuration_separately(self):
        console = ScenarioConsole()
        result = session.snapshot(console, refresh=True)
        self.assertEqual(result['console_config']['command'], 'config')
        self.assertEqual(result['config']['command'], 'read-config')
        self.assertIn('drv', result)
        self.assertIn('memory', result)
        self.assertIn('load', result)

    def test_discovery_restoration_is_checked_against_actual_cached_host_and_selection(self):
        original_tuple = dict(baud=9600, format='8E1')
        before = dict(host=dict(ok=True, active=original_tuple, active_known=True, blocked=False, configuring=False),
                      wiring=dict(ok=True, target=7, address=7, generation=9))
        for index, mutation in enumerate((None, 'tuple', 'blocked', 'unknown', 'configuring', 'target', 'generation')):
            with self.subTest(mutation=mutation):
                self.prefix = Path(self.tmp.name) / ('restore_' + str(index))
                after = copy.deepcopy(before)
                if mutation == 'tuple': after['host']['active'] = dict(baud=115200, format='8N1')
                if mutation == 'blocked': after['host']['blocked'] = True
                if mutation == 'unknown': after['host']['active_known'] = False
                if mutation == 'configuring': after['host']['configuring'] = True
                if mutation == 'target': after['wiring'].update(target=1, address=1)
                if mutation == 'generation': after['wiring']['generation'] = 10
                def run(console, record):
                    record['scan'] = dict(restored=True, original_tuple=original_tuple,
                                          original_target=[7, 7, 9])
                with patch.object(session, 'snapshot', side_effect=[before, after]):
                    if mutation is None:
                        self.assertTrue(self.recorded(run, refresh=False)['ok'])
                    else:
                        with self.assertRaises(probe.BenchError):
                            self.recorded(run, refresh=False)
                        self.assertFalse(self.summary()['ok'])

    def test_fragmented_serial_records_run_through_real_console(self):
        connection = wire.Serial(fragment=3)
        clock = wire.Clock()
        @contextmanager
        def opener(*args):
            self.opens += 1
            try:
                yield connection
            finally:
                self.closes += 1
        def console(port, on_event):
            return probe.Console(port, on_event=on_event, clock=clock, sleeper=clock.sleep)
        def run(console, record):
            result = console.command('probe', address=1, timeout_s=1)
            self.assertTrue(result['ok'])
            record['workload_verified'] = True
            console.emit('summary', mode='probe', probes_attempted=1, probes_passed=1, ok=True)
        with patch.object(session, 'Console', console), patch.object(session, 'snapshot', return_value={}):
            result = session.run_recorded(port='fake', out=self.prefix, scenario='probe', inputs={},
                run=run, port_opener=opener)
        self.assertTrue(result['ok'])
        self.assertEqual((self.opens, self.closes), (1, 1))
        self.assertEqual([item.decode().split()[1] for item in connection.writes], ['version', 'probe', 'release'])
        lines = [json.loads(line) for line in self.prefix.with_suffix('.jsonl').read_text().splitlines()]
        self.assertTrue(any(item['event'] == 'summary' for item in lines))

    def test_stale_or_partial_serial_response_cannot_pass_or_send_recovery(self):
        for index, payload in enumerate((wire.encoded(wire.reply(999, 'probe', result='accepted', address=1)),
                                         b'{"type":"reply"', b'')):
            with self.subTest(payload=payload):
                prefix = Path(self.tmp.name) / ('bad_' + str(index))
                clock = wire.Clock()
                def handler(request_id, command, args):
                    return payload if command == 'probe' else wire.Serial.normal(request_id, command, args)
                connection = wire.Serial(handler=handler, fragment=7)
                @contextmanager
                def opener(*args):
                    yield connection
                def console(port, on_event):
                    return probe.Console(port, on_event=on_event, clock=clock, sleeper=clock.sleep)
                def run(console, record):
                    console.command('probe', address=1, timeout_s=.2)
                    record['workload_verified'] = True
                with patch.object(session, 'Console', console), patch.object(session, 'snapshot', return_value={}):
                    with self.assertRaises(probe.BenchError):
                        session.run_recorded(port='fake', out=prefix, scenario='probe', inputs={},
                            run=run, port_opener=opener)
                result = json.loads(prefix.with_suffix('.json').read_text())
                self.assertFalse(result['ok'])
                self.assertNotIn('after', result)
                self.assertEqual(result['failure_diagnostics'], {})
                self.assertEqual([item.decode().split()[1] for item in connection.writes], ['version', 'probe'])

    def test_firmware_hash_is_labelled_provenance_and_does_not_attest_board(self):
        image = Path(self.tmp.name) / 'image.bin'
        image.write_bytes(b'firmware')
        result = self.recorded(lambda *_: None, firmware=image)
        self.assertEqual(result['firmware_artifact']['bytes'], 8)
        self.assertIn('does not attest', result['firmware_artifact']['provenance'])
        self.assertEqual(len(result['firmware_artifact']['sha256']), 64)


if __name__ == '__main__':
    unittest.main()
