"""Bounded functional campaign orchestration; wire validation has separate tests."""
from collections import deque
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import bench_motion as campaign


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

    def identify(self):
        self.calls.append(("version", {}))
        return dict(ok=True)

    def command(self, name, **kwargs):
        self.calls.append((name, kwargs))
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
                        running=running, released=self.released), dict(block=2, raw_position=self.raw_position, raw_speed=speed)])
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
    def test_experiments_require_bounded_feedback_before_any_motion_write(self):
        for phase in ("forward", "return", "stop-normal", "stop-direct"):
            for position in (-1, 251):
                with self.subTest(phase=phase, position=position):
                    console = FakeConsole(raw_position=position)
                    record = {}
                    with self.assertRaisesRegex(campaign.BenchError, "bounded native position report"):
                        campaign.run_phase(console, phase, record)
                    self.assertEqual(console.commands("move-relative"), [])
                    self.assertEqual(console.commands("move-absolute"), [])
                    self.assertEqual(console.commands("stop"), [])
                    self.assertNotIn("move_admission", record)
                    self.assertIn("bounded native position report", record["phase_error"])

    def test_positive_native_displacement_does_not_require_zero_origin(self):
        for position in (0, 99, 250):
            with self.subTest(position=position):
                console = FakeConsole(raw_position=position)
                record = {}
                campaign.run_phase(console, "forward", record)
                self.assertEqual(len(console.commands("move-relative")), 1)
                self.assertEqual(record["state_before"][2]["raw_position"], position)

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
            with self.assertRaisesRegex(campaign.BenchError, "zero-speed observation bound exhausted"):
                campaign.run_phase(console, "forward", record)
        self.assertEqual(len(console.commands("read-state")), 13)
        self.assertEqual(len(console.commands("move-relative")), 1)
        self.assertEqual([c["stop_policy"] for c in console.commands("stop")], ["direct"])
        self.assertIn("failure_cleanup_state", record)
        self.assertNotIn("state_after_move", record)
        self.assertTrue(record["move"]["ok"])  # ARRIVED remains distinct from zero speed.
        self.assertIn("zero-speed observation bound exhausted", record["phase_error"])

    def test_nonzero_speed_after_dynamic_stop_never_repeats_the_stop(self):
        console = FakeConsole(states=(False, False, True), speeds=[0, 0, 60] + [23] * 10,
                              move_ok=False)
        record = {}
        with patch.object(campaign.time, "sleep"):
            with self.assertRaisesRegex(campaign.BenchError, "zero-speed observation bound exhausted"):
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
        self.assertEqual([c["stop_policy"] for c in console.commands("stop")], ["direct"])
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
        for phase in ("stop-normal", "stop-direct"):
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
        self.assertEqual([c["stop_policy"] for c in console.commands("stop")], ["direct"])
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


if __name__ == "__main__":
    unittest.main()
