"""Bounded functional campaign orchestration; wire validation has separate tests."""
from collections import deque
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import bench_motion as campaign
import bench_repeat as repeat


class FakeConsole:
    def __init__(self, *, states=(), move_ok=True, accepted=True,
                 interrupted=True, stop_ok=True, wait_error=None, synchronized=True,
                 raw_position=0, speeds=()):
        self.calls = []
        self.states = deque(states)
        self.raw_position = raw_position
        self.speeds = deque(speeds)
        self.move_ok = move_ok
        self.accepted = accepted
        self.interrupted = interrupted
        self.stop_ok = stop_ok
        self.wait_error = wait_error
        self.synchronized = synchronized
        self.released = False
        self.stop_sent = False
        self.handle = None
        self.debug_mode = "off"
        self.events = []

    def emit(self, event, **fields):
        self.events.append((event, fields))

    def identify(self):
        self.calls.append(("version", {}))
        return dict(ok=True)

    def clock(self):
        return campaign.time.monotonic()

    def sleep(self, duration):
        campaign.time.sleep(duration)

    def command(self, name, **kwargs):
        self.calls.append((name, kwargs))
        if name == "debug":
            args=kwargs.get("host_args",())
            if args: self.debug_mode=args[0]
            return dict(ok=True, mode=self.debug_mode)
        if name == "host":
            return dict(ok=True, active=dict(baud=115200, format="8N1"), blocked=False)
        if name == "load":
            return dict(ok=True, workload_us=0, owner_delay_us=0, console_bytes=0)
        if name == "motion-profile":
            return dict(ok=True, pending=False, session_ok=True, error="none",
                        restored=True, original=[30, 100, 100, 60, 0, 0],
                        current=[30, 100, 100, 60, 0, 0])
        if name == "read-state":
            running = self.states.popleft() if self.states else False
            speed = self.speeds.popleft() if self.speeds else (60 if running else 0)
            return dict(ok=True, state_blocks=[dict(block=0, raw_alarm=0, alarm_flag=False,
                        running=running, released=self.released), dict(block=2, raw_position=self.raw_position,
                        raw_speed=speed, position_source=0, position_source_resolution=10,
                        position_signed_resolution=7, position_scale_resolution=5)])
        if name == "stop":
            self.stop_sent = True
            return dict(ok=self.stop_ok, outcome="observed" if self.stop_ok else "unknown")
        if name == "motor-release":
            self.released = True
        if name == "enable":
            self.released = False
        return dict(ok=True)

    def begin(self, name, **kwargs):
        self.calls.append((name, kwargs))
        self.handle = SimpleNamespace(accepted=self.accepted, operation_id=1, terminal=None)
        return self.handle

    def wait(self, handle, **kwargs):
        self.calls.append(("wait", kwargs))
        if self.wait_error is not None:
            raise self.wait_error
        handle.terminal = dict(ok=self.move_ok, interrupted_by_stop=self.stop_sent and self.interrupted,
                               execution=3, outcome="observed" if self.move_ok else "uncertain")
        return handle.terminal

    def commands(self, name):
        return [kwargs for command, kwargs in self.calls if command == name]


class FunctionalCampaignTest(unittest.TestCase):
    def test_unknown_phase_fails_before_any_io(self):
        console = FakeConsole()
        with self.assertRaisesRegex(ValueError, 'unknown finite experiment'):
            campaign.run_phase(console, 'typo', {})
        self.assertEqual(console.calls, [])

    def test_actions_refuse_initially_released_drive_without_state_changes(self):
        console = FakeConsole()
        console.released = True
        with self.assertRaisesRegex(campaign.BenchError, 'initially enabled stationary'):
            campaign.run_phase(console, 'actions', {})
        self.assertTrue(console.released)
        for command in ('stop', 'enable', 'motor-release'):
            self.assertEqual(console.commands(command), [])

    def test_modifying_phases_restore_in_same_connection_after_stop(self):
        for phase in campaign.MOTION_PHASES:
            with self.subTest(phase=phase):
                console = FakeConsole(states=(False, False, True) if phase.startswith('stop-') else (),
                                      move_ok=not phase.startswith('stop-'))
                record = {}
                campaign.run_phase(console, phase, record)
                self.assertEqual([args['host_args'] for args in console.commands('motion-profile')],
                                 [('read',), ('restore',)])
                self.assertEqual(record['cleanup'], 'stopped_and_restored')
                restored_at = next(i for i, (name, args) in enumerate(console.calls)
                                   if name == 'motion-profile' and args['host_args'] == ('restore',))
                self.assertTrue(any(name == 'stop' for name, _ in console.calls[:restored_at]))
                self.assertTrue(record['restoration']['restored'])

    def test_uncertain_move_stays_failed_after_successful_restoration(self):
        console = FakeConsole(move_ok=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'finite move did not'):
            campaign.run_phase(console, 'forward', record)
        self.assertEqual(record['cleanup'], 'stopped_and_restored')
        self.assertFalse(record['move']['ok'])
        self.assertEqual(len(console.commands('move-relative')), 1)

    def test_unknown_stop_never_restores_or_replays(self):
        console = FakeConsole(stop_ok=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'stop refused/failed'):
            campaign.run_phase(console, 'forward', record)
        self.assertEqual([args['host_args'] for args in console.commands('motion-profile')], [('read',)])
        self.assertEqual(record['cleanup'], 'unknown')
        self.assertEqual(len(console.commands('stop')), 1)

    def test_lost_admission_retains_unknown_without_commands_after_framing_failure(self):
        class LostAdmission(FakeConsole):
            next_id = 42
            def begin(self, name, **kwargs):
                handle = super().begin(name, **kwargs)
                handle.accepted = False
                self.pending = {42: handle}
                self.synchronized = False
                raise campaign.BenchError('admission line lost')
        console = LostAdmission()
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'admission line lost'):
            campaign.run_phase(console, 'forward', record)
        self.assertEqual(record['cleanup'], 'unknown')
        self.assertEqual([args['host_args'] for args in console.commands('motion-profile')], [('read',)])
        self.assertEqual(console.commands('stop'), [])
        self.assertNotIn('ending_stats', record)

    def test_snapshot_pending_inspection_budget_cannot_replay_restore(self):
        class Pending(FakeConsole):
            def command(self, name, **kwargs):
                result = super().command(name, **kwargs)
                if name == 'motion-profile':
                    result['pending'] = True
                return result
        console = Pending()
        with patch.object(campaign.time, 'sleep'):
            with self.assertRaisesRegex(campaign.BenchError, 'observation bound exhausted'):
                campaign.restore_profile(console)
        self.assertEqual([args['host_args'] for args in console.commands('motion-profile')],
                         [('restore',)] + [('inspect',)] * 100)

    def test_snapshot_invalid_action_or_deadline_does_no_io(self):
        for action, timeout in [('forget', 5), ('restore', 0), ('read', float('nan')), ('read', 61)]:
            console = FakeConsole()
            with self.assertRaises(ValueError):
                campaign.profile_snapshot(console, action, timeout)
            self.assertEqual(console.calls, [])

    def test_uncertain_restore_is_failed_and_attempted_once(self):
        class UnknownRestore(FakeConsole):
            def command(self, name, **kwargs):
                result = super().command(name, **kwargs)
                if name == 'motion-profile' and kwargs.get('host_args') == ('restore',):
                    result.update(session_ok=False, restored=False, error='transaction',
                                  restore_unsettled=True)
                return result
        console = UnknownRestore()
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'restoration failed: transaction'):
            campaign.run_phase(console, 'forward', record)
        self.assertEqual([args['host_args'] for args in console.commands('motion-profile')],
                         [('read',), ('restore',)])
        self.assertEqual(record['cleanup'], 'drive_reported_stopped')
        self.assertIn('transaction', record['failure_cleanup_error'])
        self.assertEqual(len(console.commands('move-relative')), 1)

    def test_restore_mismatch_is_not_cleanup_success(self):
        class Mismatch(FakeConsole):
            def command(self, name, **kwargs):
                result = super().command(name, **kwargs)
                if name == 'motion-profile' and kwargs.get('host_args') == ('restore',):
                    result['current'] = [30, 100, 100, 60, 0, 250]
                return result
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'exact motion profile restoration'):
            campaign.run_phase(Mismatch(), 'forward', record)
        self.assertNotEqual(record['cleanup'], 'stopped_and_restored')

    def test_polled_events_stream_incrementally_with_bounded_tail(self):
        console = FakeConsole(move_ok=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'dynamic stop NOT RUN'):
            campaign.run_phase(console, 'stop-normal', record)
        streamed = [fields for event, fields in console.events if event == 'phase_command']
        self.assertGreater(len(streamed), campaign.EVENT_TAIL)
        self.assertEqual(record['event_count'], len(streamed))
        self.assertEqual(len(record['events']), campaign.EVENT_TAIL)
        self.assertEqual(record['events'], [{k: v for k, v in event.items() if k != 'phase'}
                                          for event in streamed[-campaign.EVENT_TAIL:]])

    def test_selected_debug_observes_regular_phase_and_restores_mode(self):
        console=FakeConsole(); console.debug_mode="raw"
        record=dict(debug_mode="decoded")
        campaign.run_phase(console,"inspect",record)
        self.assertEqual(console.debug_mode,"raw")
        self.assertEqual([v["host_args"] for v in console.commands("debug")],[(),("decoded",),(),("raw",)])
        self.assertEqual(record["debug"]["cleanup"],"restored")
        self.assertEqual(len(console.commands("read-identity")),1)
        self.assertFalse(console.commands("stop"))
        ordinary=FakeConsole(); campaign.run_phase(ordinary,"inspect",{})
        self.assertFalse(ordinary.commands("debug"))
        self.assertEqual([name for name,_ in console.calls if name!="debug"],[name for name,_ in ordinary.calls])

    def test_debug_cleanup_failure_preserves_failed_motion(self):
        console=FakeConsole(move_ok=False)
        original=console.command
        def command(name,**kwargs):
            if name=="debug" and console.handle is not None:
                raise campaign.BenchError("display query failed")
            return original(name,**kwargs)
        console.command=command
        record=dict(debug_mode="decoded")
        with self.assertRaises(campaign.BenchError) as raised:
            campaign.run_phase(console,"forward",record)
        self.assertNotIn("display query",str(raised.exception))
        self.assertEqual(record["debug"]["cleanup"],"failed")
        self.assertEqual(record["debug"]["cleanup_error"],"display query failed")
        self.assertEqual(len(console.commands("move-relative")),1)

    def test_absolute_experiments_require_recorded_raw_fixture_window_before_motion_write(self):
        for phase in ("absolute", "return"):
            for position in (-1, 251, 298, 0xffffffff):
                with self.subTest(phase=phase, position=position):
                    console = FakeConsole(raw_position=position)
                    record = {}
                    with self.assertRaisesRegex(campaign.BenchError, "recorded raw feedback fixture window"):
                        campaign.run_phase(console, phase, record)
                    self.assertEqual(console.commands("move-relative"), [])
                    self.assertEqual(console.commands("move-absolute"), [])
                    self.assertEqual(console.commands("stop"), [])
                    self.assertNotIn("move_admission", record)
                    self.assertIn("recorded raw feedback fixture window", record["phase_error"])

    def test_positive_native_displacement_does_not_require_zero_origin(self):
        for position in (0, 99, 250, 298, 0xffffffff):
            with self.subTest(position=position):
                console = FakeConsole(raw_position=position)
                record = {}
                campaign.run_phase(console, "forward", record)
                self.assertEqual(len(console.commands("move-relative")), 1)
                self.assertEqual(record["state_before"][2]["raw_position"], position)

    def test_relative_dynamic_stops_need_no_raw_feedback_origin(self):
        for phase in ("stop-normal", "stop-fast"):
            for position in (298, 0xffffffff):
                with self.subTest(phase=phase, position=position):
                    console = FakeConsole(raw_position=position, states=(False, False, True), move_ok=False)
                    record = {}
                    campaign.run_phase(console, phase, record)
                    self.assertEqual(console.commands("move-relative")[0]["move_args"],
                                     ("250", "steps", "native", "60", "configured"))
                    self.assertEqual(len(console.commands("move-relative")), 1)
                    self.assertEqual([c["stop_policy"] for c in console.commands("stop")], [phase[5:]])
                    self.assertTrue(record["move"]["interrupted_by_stop"])
                    self.assertEqual(record["state_before"][2]["position_source"], 0)

    def test_delayed_zero_speed_is_read_only_and_all_reports_are_retained(self):
        console = FakeConsole(raw_position=99, speeds=(0, 0, 23, 10, 0, 0))
        record = {}
        with patch.object(campaign.time, "sleep") as sleep:
            campaign.run_phase(console, "forward", record)
        self.assertEqual(sleep.call_args_list, [unittest.mock.call(.05)] * 2)
        speeds = [event["result"]["state_blocks"][1]["raw_speed"] for event in record["events"]
                  if event["command"] == "read-state"]
        self.assertEqual(speeds, [0, 0, 23, 10, 0, 0])
        self.assertEqual(record["state_after_move"][2]["raw_position"], 99)
        self.assertEqual(record["state_after_move"][2]["raw_speed"], 0)
        self.assertEqual(record["move"]["execution"], 3)
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual(len(console.commands("stop")), 1)

    def test_never_zero_speed_fails_at_bound_and_sends_only_one_cleanup_stop(self):
        console = FakeConsole(raw_position=99, speeds=[0, 0] + [23] * 10 + [0])
        record = {}
        with patch.object(campaign.time, "sleep"):
            with self.assertRaisesRegex(campaign.BenchError, "zero-speed/non-running observation bound exhausted"):
                campaign.run_phase(console, "forward", record)
        self.assertEqual(len(console.commands("read-state")), 13)
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual([c["stop_policy"] for c in console.commands("stop")], ["fast"])
        self.assertIn("failure_cleanup_state", record)
        self.assertNotIn("state_after_move", record)
        self.assertTrue(record["move"]["ok"])  # ARRIVED remains distinct from zero speed.
        self.assertIn("zero-speed/non-running observation bound exhausted", record["phase_error"])

    def test_nonzero_speed_after_dynamic_stop_never_repeats_the_stop(self):
        console = FakeConsole(states=(False, False, True), speeds=[0, 0, 60] + [23] * 10,
                              move_ok=False)
        record = {}
        with patch.object(campaign.time, "sleep"):
            with self.assertRaisesRegex(campaign.BenchError, "zero-speed/non-running observation bound exhausted"):
                campaign.run_phase(console, "stop-normal", record)
        self.assertEqual(len(console.commands("read-state")), 13)
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual([c["stop_policy"] for c in console.commands("stop")], ["normal"])
        self.assertNotIn("failure_cleanup_stop", record)
        self.assertTrue(record["move"]["interrupted_by_stop"])

    def test_forward_is_one_finite_move_then_one_stop(self):
        console = FakeConsole()
        record = {}
        campaign.run_phase(console, "forward", record)
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual(console.commands("move-relative")[0]["move_args"],
                         ("100", "steps", "native", "60", "configured"))
        self.assertEqual([c["stop_policy"] for c in console.commands("stop")], ["fast"])
        self.assertEqual(record["move"]["execution"], 3)  # Never promote UNKNOWN to acknowledged.
        self.assertNotIn("failure_cleanup_stop", record)

    def test_nonzero_absolute_uses_regular_operation_once(self):
        console = FakeConsole()
        record = {}
        campaign.run_phase(console, "absolute", record)
        self.assertEqual(console.commands("move-absolute")[0]["move_args"],
                         ("100", "steps", "native", "60", "configured"))
        self.assertEqual(len(console.commands("move-absolute")), 1)
        self.assertEqual(len(console.commands("stop")), 1)

    def test_dynamic_stop_requires_running_before_stop(self):
        for phase in ("stop-normal", "stop-fast"):
            with self.subTest(phase=phase):
                console = FakeConsole(states=[False, False, True], move_ok=False)
                record = {}
                campaign.run_phase(console, phase, record)
                self.assertTrue(record["activity_before_stop"][0]["running"])
                self.assertTrue(record["move"]["interrupted_by_stop"])
                self.assertEqual([c["stop_policy"] for c in console.commands("stop")], [phase[5:]])
                self.assertEqual(console.commands("move-relative")[0]["move_args"][0], "250")

    def test_missed_running_is_failed_test_with_one_cleanup_stop(self):
        console = FakeConsole(move_ok=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, "dynamic stop NOT RUN"):
            campaign.run_phase(console, "stop-normal", record)
        self.assertIsNone(record["activity_before_stop"])
        self.assertEqual(len(console.commands("read-state")), 35)  # Admission + 32 polls + cleanup.
        self.assertEqual([c["stop_policy"] for c in console.commands("stop")], ["fast"])
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertIn("failure_cleanup_state", record)

    def test_failed_stop_is_never_repeated(self):
        console = FakeConsole(states=[False, False, True], stop_ok=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, "stop refused/failed"):
            campaign.run_phase(console, "stop-normal", record)
        self.assertEqual([c["stop_policy"] for c in console.commands("stop")], ["normal"])
        self.assertNotIn("failure_cleanup_stop", record)
        self.assertFalse(next(e for e in record["events"] if e["command"] == "stop")["result"]["ok"])

    def test_uncertain_move_failure_retained_and_never_replayed(self):
        console = FakeConsole(move_ok=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, "finite move did not"):
            campaign.run_phase(console, "forward", record)
        self.assertFalse(record["move"]["ok"])
        self.assertEqual(record["move"]["execution"], 3)
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual(len(console.commands("stop")), 1)

    def test_rejected_move_does_not_need_cleanup_write(self):
        console = FakeConsole(accepted=False, move_ok=False)
        with self.assertRaisesRegex(campaign.BenchError, "refused before transmission"):
            campaign.run_phase(console, "forward", {})
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual(console.commands("stop"), [])

    def test_unsynchronized_frame_failure_sends_no_cleanup_or_diagnostics(self):
        console = FakeConsole(wait_error=campaign.BenchError("malformed terminal frame"), synchronized=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, "malformed terminal frame"):
            campaign.run_phase(console, "forward", record)
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual(console.commands("stop"), [])
        self.assertNotIn("ending_stats", record)

    def test_synchronized_interrupt_gets_one_cleanup_stop_without_replaying_move(self):
        console = FakeConsole(wait_error=KeyboardInterrupt())
        record = {}
        with self.assertRaises(KeyboardInterrupt):
            campaign.run_phase(console, "forward", record)
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual(len(console.commands("stop")), 1)
        self.assertIn("failure_cleanup_state", record)

    def test_failed_cleanup_is_retained_without_retry(self):
        console = FakeConsole(move_ok=False, stop_ok=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, "finite move did not"):
            campaign.run_phase(console, "forward", record)
        self.assertIn("stop refused/failed", record["failure_cleanup_error"])
        self.assertEqual(len(console.commands("stop")), 1)

    def test_naturally_completed_move_does_not_count_as_interrupted_stop(self):
        console = FakeConsole(states=[False, False, True], move_ok=True, interrupted=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, "did not retain the interrupted operation"):
            campaign.run_phase(console, "stop-normal", record)
        self.assertEqual(len(console.commands("stop")), 1)
        self.assertFalse(record["move"]["interrupted_by_stop"])

    def test_ending_diagnostic_failure_cannot_report_success(self):
        class BrokenDiagnostics(FakeConsole):
            def command(self, name, **kwargs):
                if name == "memory":
                    raise campaign.BenchError("memory diagnostics missing")
                return super().command(name, **kwargs)
        console = BrokenDiagnostics()
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, "memory diagnostics missing"):
            campaign.run_phase(console, "forward", record)
        self.assertIn("memory diagnostics missing", record["ending_error"])
        self.assertTrue(record["move"]["ok"])
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual(len(console.commands("stop")), 1)

    def test_original_failure_survives_secondary_diagnostic_error_in_record(self):
        class BrokenDiagnostics(FakeConsole):
            def command(self, name, **kwargs):
                if name == "memory":
                    raise campaign.BenchError("memory diagnostics missing")
                return super().command(name, **kwargs)
        record = {}
        with self.assertRaises(campaign.BenchError):
            campaign.run_phase(BrokenDiagnostics(move_ok=False), "forward", record)
        self.assertIn("finite move did not", record["phase_error"])
        self.assertIn("memory diagnostics missing", record["ending_error"])


class RepeatConsole(FakeConsole):
    def __init__(self, *, fail_move=False, lose_framing=False, **kwargs):
        super().__init__(**kwargs)
        self.now = 0
        self.fail_move = fail_move
        self.lose_framing = lose_framing
        self.moves = 0
        self.sleeps = []

    def clock(self):
        return self.now

    def sleep(self, duration):
        self.sleeps.append(duration)
        self.now += duration

    def command(self, name, **kwargs):
        if name != 'move-relative':
            return super().command(name, **kwargs)
        self.calls.append((name, kwargs))
        self.moves += 1
        if self.lose_framing:
            self.synchronized = False
            raise campaign.BenchError('original admission lost')
        if self.fail_move:
            raise campaign.BenchError('original move uncertain')
        offset, count = ((0, 5), (0, 0), (0, 0), (2, 1), (3, 2), (0, 5))[self.moves - 1]
        return dict(ok=True, completion='observed', uncertain=False, started_us=100,
                    trigger_evidence=dict(delivered_us=600, latest_us=550),
                    setup_offset=offset, setup_count=count, operation_id=self.moves)


class RepeatCampaignTest(unittest.TestCase):
    def test_nonatomic_arrival_speed_requires_bounded_read_wait_before_next_move(self):
        # Reproduce the observed transition: ARRIVED/non-running with speed4,
        # then a later zero-speed sample. No extra motion/stop is sent to settle it.
        console = RepeatConsole(speeds=(0, 0, 4, 0, 0, 0, 0, 0))
        record = {}
        repeat.campaign(console, record, 1)
        self.assertEqual(console.moves, 6)
        calls = [name for name, _ in console.calls]
        move_indexes = [i for i, name in enumerate(calls) if name == 'move-relative']
        self.assertEqual(calls[move_indexes[1] + 1:move_indexes[2]], ['read-state', 'read-state'])
        self.assertEqual(console.sleeps, [.05])
        self.assertEqual(len(console.commands('stop')), 1)
        self.assertEqual([x['host_args'] for x in console.commands('motion-profile')], [('read',), ('restore',)])
        self.assertEqual(record['cleanup'], 'stopped_and_restored')
        self.assertEqual(len(console.commands('probe')), 10)
        self.assertTrue(record['workload_verified'])

    def test_standstill_budget_exhaustion_never_replays_motion(self):
        console = RepeatConsole(speeds=(0,) + (4,) * 10)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'observation bound exhausted'):
            repeat.campaign(console, record, 1)
        self.assertEqual(console.moves, 1)
        self.assertEqual(len(console.commands('read-state')), 12)  # Initial + ten bounded reads + cleanup.
        self.assertEqual(console.sleeps, [.05] * 9)
        self.assertEqual(len(console.commands('stop')), 1)
        self.assertEqual(record['cleanup'], 'stopped_and_restored')
        self.assertEqual(console.commands('probe'), [])
        self.assertIn('observation bound exhausted', record['failure'])

    def test_uncertain_motion_gets_one_cleanup_and_remains_failed(self):
        console = RepeatConsole(fail_move=True)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'original move uncertain'):
            repeat.campaign(console, record, 1)
        self.assertEqual(console.moves, 1)
        self.assertEqual(len(console.commands('stop')), 1)
        self.assertEqual(record['cleanup'], 'stopped_and_restored')
        self.assertNotIn('workload_verified', record)
        self.assertEqual(record['failure'], 'original move uncertain')

    def test_failed_cleanup_preserves_primary_failure_and_never_retries_stop(self):
        console = RepeatConsole(fail_move=True, stop_ok=False)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'stop refused/failed'):
            repeat.campaign(console, record, 1)
        self.assertEqual(record['failure'], 'original move uncertain')
        self.assertEqual(record['cleanup'], 'unknown')
        self.assertEqual(console.moves, 1)
        self.assertEqual(len(console.commands('stop')), 1)
        self.assertEqual([x['host_args'] for x in console.commands('motion-profile')], [('read',)])

    def test_lost_framing_retains_primary_error_and_sends_no_cleanup_writes(self):
        console = RepeatConsole(lose_framing=True)
        record = {}
        with self.assertRaisesRegex(campaign.BenchError, 'framing unavailable'):
            repeat.campaign(console, record, 1)
        self.assertEqual(record['failure'], 'original admission lost')
        self.assertEqual(record['cleanup'], 'unknown')
        self.assertEqual(console.moves, 1)
        self.assertEqual(console.commands('stop'), [])
        self.assertEqual([x['host_args'] for x in console.commands('motion-profile')], [('read',)])


if __name__ == "__main__":
    unittest.main()
