"""Exercise host framing and finite campaigns using a fake serial stream only."""

import importlib.util
import io
import json
from contextlib import redirect_stderr
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bench_probe", ROOT / "scripts/bench_probe.py")
bench = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bench)


def encoded(item):
    return (json.dumps(item) + "\n").encode("ascii")


def reply(request_id, command, **fields):
    if command in {"probe", "capture-read", "recover"} and (fields.get("result") == "accepted"
                                           or fields.get("type") in {"probe", "capture_read", "recovery"}):
        fields.setdefault("operation_id", request_id + 100)
        if fields.get("type") in {"probe", "capture_read", "recovery"}:
            fields.setdefault("command_id", request_id)
    return {"type": "reply", "id": request_id, "command": command,
            "profile": "ess_rs", "ok": True, **fields}


def load_reply(request_id, settings=(0, 0, 0), **fields):
    return reply(request_id, "load", **dict(zip(bench.LOAD_FIELDS, settings)),
                 ready=True, capture_mode="timer", elapsed_us=20000,
                 work_us=4000, work_iterations=2, console_lines=2,
                 console_dropped=0, capture_us=800, capture_samples=200,
                 owner_gap_max_us=5001, capture_gap_max_us=10,
                 work_stack_free_bytes=2500, **fields)


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, delay):
        self.now += delay


class Serial:
    def __init__(self, handler=None, fragment=256):
        self.input = bytearray()
        self.writes = []
        self.fragment = fragment
        self.handler = handler or self.normal
        self.short = False

    @staticmethod
    def normal(request_id, command, args):
        if command == "version":
            return encoded(reply(request_id, command, product="MotorControl-RS",
                                 protocol=2, version="0.test", outstanding_capacity=9))
        if command == "probe":
            address = int(args[0]) if args else 1
            return (encoded(reply(request_id, command, result="accepted", address=address))
                    + encoded(reply(request_id, command, type="probe", address=address,
                                    transport="FRAME", codec="OK", detail=0, raw_model=60,
                                    outcome="success", execution_unknown=False,
                                    duration_us=12345, tx_bytes=8, rx_bytes=7,
                                    timing_valid=True, raw_truncated=False,
                                    observed_earliest_us=13300, observed_latest_us=13345,
                                    delivered_us=16000,
                                    identity="responder_only")))
        if command == "capture-read":
            address = int(args[0]) if args else 1
            assert address == 1  # This fixed independent raw-frame fixture is node 1.
            return (encoded(reply(request_id, command, result="accepted", address=address))
                    + encoded(reply(request_id, command, type="capture_read", address=address,
                                    transport="FRAME", codec="OK", detail=0, raw_model=None,
                                    outcome="success", execution_unknown=False,
                                    duration_us=12345, tx_bytes=8, rx_bytes=37,
                                    capture_read=True, register_start=304, register_count=16,
                                    timing_valid=True, raw_truncated=False,
                                    observed_earliest_us=13300, observed_latest_us=13345,
                                    delivered_us=16000, identity="not_requested",
                                    tx_hex="01030130001045F5",
                                    rx_hex="010320" + "00" * 32 + "927A")))
        if command in {"release", "cancel"}:
            return encoded(reply(request_id, command, operation_id=int(args[0]), result="done"))
        if command == "recover":
            return (encoded(reply(request_id, command, result="accepted"))
                    + encoded(reply(request_id, command, type="recovery", recovery=True,
                                    outcome="recovered", transport="NONE", requested_us=1000,
                                    deadline_us=50000, finished_us=1500)))
        if command == "status":
            return encoded(reply(request_id, command, busy=False, recovery_required=False,
                                 phase="DONE", transport="FRAME", last_probe_known=True,
                                 last_probe_ok=True, raw_model=60, age_ms=20, uptime_ms=1000))
        if command == "health":
            return encoded(reply(request_id, command, communication="current",
                                 readiness="unknown", alarms="unknown", state="unknown"))
        if command == "memory":
            return encoded(reply(request_id, command, valid=True, internal_free=150000,
                                 internal_min=145000, internal_largest=130000,
                                 psram_free=8000000, psram_min=7990000, psram_largest=7900000,
                                 stack_free_bytes=2500))
        return encoded(reply(request_id, command, started=10, frames=10, failed=0))

    def read(self, limit):
        count = min(limit, self.fragment, len(self.input))
        data = bytes(self.input[:count])
        del self.input[:count]
        return data

    def write(self, data):
        self.writes.append(data)
        parts = data.decode("ascii").split()
        self.input.extend(self.handler(int(parts[0][1:]), parts[1], parts[2:]))
        return len(data) - 1 if self.short else len(data)


class Framing(unittest.TestCase):
    def session(self, handler=None, fragment=256, identify=True):
        self.clock = Clock()
        self.port = Serial(handler, fragment)
        self.events = []
        self.console = bench.Console(
            self.port, clock=self.clock, sleeper=self.clock.sleep,
            on_event=lambda event, **fields: self.events.append({"event": event, **fields}),
        )
        if identify:
            self.console.identify(timeout_s=0.1)
        return self.console

    def failed(self, action, text):
        with self.assertRaisesRegex(bench.BenchError, text):
            action()
        writes = len(self.port.writes)
        with self.assertRaises(bench.BenchError):
            self.console.command("status", timeout_s=0.1)
        self.assertEqual(len(self.port.writes), writes, "failed session must never send again")

    def load_session(self, *, probe_failure=None):
        settings = (0, 0, 0)

        def handler(request_id, command, args):
            nonlocal settings
            if command == "load":
                if args:
                    settings = tuple(int(value) for value in args)
                return encoded(load_reply(request_id, settings))
            if command == "probe" and probe_failure:
                return (encoded(reply(request_id, command, result="accepted", address=1))
                        + encoded(reply(request_id, command, type="probe", ok=False,
                                        address=1, transport=probe_failure, codec="NOT_CHECKED")))
            return Serial.normal(request_id, command, args)

        return self.session(handler)

    def test_fragmented_probe_and_cached_health(self):
        console = self.session(fragment=1)
        result = console.command("probe", address=17, timeout_s=0.1)
        self.assertEqual(result["raw_model"], 60)
        self.assertEqual(result["identity"], "responder_only")
        self.assertEqual(self.port.writes[-2:], [b"@2 probe 17\n", b"@3 release 102\n"])
        health = console.command("health", timeout_s=0.1)
        self.assertEqual(health["readiness"], "unknown")
        accepts = [entry for entry in self.events if entry["event"] == "reply"
                   and entry["response"].get("result") == "accepted"]
        self.assertEqual(len(accepts), 1)

    def test_capture_read_fragmented_retained_and_released(self):
        console = self.session(fragment=1)
        handle = console.begin("capture-read", address=1, timeout_s=0.1)
        terminal = console.wait(handle)
        self.assertEqual(terminal["rx_bytes"], 37)
        self.assertIsNone(terminal["raw_model"])
        self.port.handler = lambda i, cmd, args: encoded({
            **terminal, "type": "reply", "id": i, "command": cmd, "recovery": False})
        inspected = console.command("result", operation_id=handle.operation_id, timeout_s=0.1)
        self.assertEqual(inspected["rx_hex"], terminal["rx_hex"])
        self.port.handler = Serial.normal
        console.command("release", operation_id=handle.operation_id, timeout_s=0.1)
        self.assertTrue(handle.released)
        self.assertEqual(sum(b"capture-read" in value for value in self.port.writes), 1)

    def test_capture_read_rejects_invalid_window_raw_frames_and_kind(self):
        for change in ({"register_start": 0}, {"register_count": 15}, {"register_count": True},
                       {"capture_read": False}, {"recovery": True}, {"raw_model": 60}, {"identity": "responder_only"},
                       {"rx_bytes": 7}, {"tx_bytes": 7}, {"tx_hex": "01030130001045F4"},
                       {"tx_hex": "010300000001840A"}, {"rx_hex": "010302003CB855"},
                       {"rx_hex": "010320" + "00" * 32 + "927B"}, {"tx_hex": "01 030130001045F5"},
                       {"type": "probe"}):
            with self.subTest(change=change):
                console = self.session()
                def handler(i, cmd, args):
                    accepted, terminal = map(json.loads, Serial.normal(i, cmd, args).splitlines())
                    return encoded(accepted) + encoded({**terminal, **change})
                self.port.handler = handler
                self.failed(lambda: console.command("capture-read", timeout_s=0.1),
                            "inconsistent|result evidence|sequence is invalid")

    def test_capture_read_pending_inspection_cannot_change_kind(self):
        for command in ("probe", "capture-read"):
            with self.subTest(command=command):
                def handler(i, cmd, args):
                    if cmd == command:
                        return Serial.normal(i, cmd, args).splitlines()[0] + b"\n"
                    return encoded(reply(i, cmd, command_id=2, operation_id=102,
                                         result="pending", recovery=False,
                                         capture_read=command != "capture-read"))
                console = self.session()
                self.port.handler = handler
                handle = console.begin(command, timeout_s=0.1)
                self.failed(lambda: console.command("result", operation_id=handle.operation_id,
                                                    timeout_s=0.1), "kind does not match")

    def test_capture_read_and_probe_share_retained_quota(self):
        console = self.session()
        handles = []
        for index in range(bench.MAX_PROBES):
            handle = console.begin("probe" if index % 2 else "capture-read", timeout_s=0.1)
            console.wait(handle)
            handles.append(handle)
        console.command("release", operation_id=handles[0].operation_id, timeout_s=0.1)
        replacement = console.begin("capture-read", timeout_s=0.1)
        console.wait(replacement)
        self.assertEqual(len(console.operations), 8)
        self.failed(lambda: console.begin("capture-read", timeout_s=0.1), "retained result quota")

    def test_capture_read_campaign_and_loaded_campaign_are_finite(self):
        console = self.load_session()
        bench.campaign(console, "capture-read", count=1, interval_s=0, timeout_s=0.1)
        bench.campaign(console, "load", count=3, interval_s=0, timeout_s=0.1,
                       load=(2000, 5000, 128), read_command="capture-read")
        commands = [value.split()[1] for value in self.port.writes]
        self.assertEqual(commands.count(b"capture-read"), 4)
        self.assertEqual(commands.count(b"release"), 4)
        self.assertNotIn(b"probe", commands)
        self.assertEqual(self.events[-1]["read_command"], "capture-read")

    def test_capture_read_failure_stops_without_retry_or_recovery(self):
        console = self.session()
        def handler(i, cmd, args):
            if cmd != "capture-read":
                return Serial.normal(i, cmd, args)
            accepted, terminal = map(json.loads, Serial.normal(i, cmd, args).splitlines())
            terminal.update(ok=False, outcome="transport_error", transport="RX_ERROR", codec="NOT_CHECKED")
            return encoded(accepted) + encoded(terminal)
        self.port.handler = handler
        with self.assertRaisesRegex(bench.BenchError, "probe failed"):
            bench.campaign(console, "capture-read", count=1, interval_s=0, timeout_s=0.1)
        commands = [value.split()[1] for value in self.port.writes]
        self.assertEqual(commands, [b"version", b"stats", b"capture-read", b"release"])

    def test_capture_read_arguments_and_cached_config(self):
        prefix = ["--port", "unused", "--log", "unused.jsonl"]
        self.assertEqual(bench.arguments(prefix + ["capture-read"]).count, 1)
        loaded = bench.arguments(prefix + ["load", "--capture-read", "--count", "3"])
        self.assertTrue(loaded.capture_read)
        self.assertEqual(loaded.count, 3)
        console = self.session()
        self.assertTrue(console.command("config", timeout_s=0.1)["ok"])
        self.assertEqual(self.port.writes[-1], b"@2 config\n")

    def test_wire_crc_matches_preserved_vendor_example(self):
        self.assertEqual(bench.wire_crc(bytes.fromhex("010300230001")), 0xC075)
        self.assertEqual(bench.wire_crc(bytes.fromhex("010302003CB855")), 0)

    def test_startup_requires_complete_bounded_lines(self):
        console = self.session(identify=False)
        self.port.input.extend(b"rst:0x1\n" + encoded({"type": "boot"}))
        console.drain_startup(0.02)
        console.identify(timeout_s=0.1)
        self.assertTrue(console.identified)
        with self.assertRaises(bench.BenchError):
            console.drain_startup(0.02)

    def test_incomplete_startup_is_not_discarded(self):
        console = self.session(identify=False)
        self.port.input.extend(b"half boot line")
        self.failed(lambda: console.drain_startup(0.01), "incomplete line")
        self.assertEqual(self.port.writes, [])

    def test_probe_requires_identified_firmware(self):
        console = self.session(identify=False)
        with self.assertRaises(bench.BenchError):
            console.command("probe")
        self.assertEqual(self.port.writes, [])

    def test_wrong_firmware_stops_before_probe(self):
        console = self.session(lambda i, cmd, _: encoded(reply(i, cmd, product="Other",
                                                            protocol=1)), identify=False)
        self.failed(lambda: console.identify(timeout_s=0.1), "not the supported")
        self.assertEqual(len(self.port.writes), 1)

    def test_only_whitelisted_commands_and_valid_addresses(self):
        console = self.session()
        for command, address in (("probe\nreset", None), ("motor_reset", None),
                                 ("probe", 0), ("probe", 248), ("probe", True),
                                 ("status", 1)):
            with self.assertRaises(ValueError):
                console.command(command, address=address)
        self.assertEqual(len(self.port.writes), 1)

    def test_timeout_does_not_retry(self):
        console = self.session()
        self.port.handler = lambda *_: b""
        self.failed(lambda: console.command("probe", timeout_s=0.02), "deadline expired")
        self.assertEqual(self.port.writes, [b"@1 version\n", b"@2 probe\n"])

    def test_missing_probe_terminal_does_not_retry(self):
        console = self.session()
        self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, result="accepted", address=1))
        self.failed(lambda: console.command("probe", timeout_s=0.02), "deadline expired")
        self.assertEqual(len(self.port.writes), 2)

    def test_short_write_stops(self):
        console = self.session()
        self.port.short = True
        self.failed(lambda: console.command("probe", timeout_s=0.1), "short serial")

    def test_wrong_id_command_and_boolean_id(self):
        for fields in ({"id": 1}, {"id": True}, {"command": "health"}, {"ok": 1}):
            with self.subTest(fields=fields):
                console = self.session()
                self.port.handler = lambda i, cmd, _: encoded({**reply(i, cmd), **fields})
                self.failed(lambda: console.command("status", timeout_s=0.1), "does not match")

    def test_json_duplicate_keys_and_nonfinite_values(self):
        for content in (b'{"type":"reply","id":2,"id":2}\n',
                        b'{"x":NaN}\n', b'{"x":Infinity}\n', b'{broken}\n'):
            with self.subTest(content=content):
                console = self.session()
                self.port.handler = lambda *_: content
                self.failed(lambda: console.command("status", timeout_s=0.1), "malformed JSON")

    def test_wrong_profile_stops(self):
        console = self.session()
        self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, profile="other"))
        self.failed(lambda: console.command("status", timeout_s=0.1), "profile")

    def test_probe_address_matches_request_and_acceptance(self):
        for admission, terminal in ((2, 2), (True, 1), (248, 248), (1, 2), (1, True)):
            with self.subTest(admission=admission, terminal=terminal):
                console = self.session()
                self.port.handler = lambda i, cmd, _: (
                    encoded(reply(i, cmd, result="accepted", address=admission))
                    + encoded(reply(i, cmd, type="probe", address=terminal, ok=False)))
                self.failed(lambda: console.command("probe", address=1, timeout_s=0.1), "address")

    def test_probe_success_requires_consistent_evidence(self):
        for change in ({"transport": "NO_RESPONSE"}, {"codec": "EXCEPTION"},
                       {"outcome": "transport"}, {"execution_unknown": True},
                       {"raw_model": None}, {"raw_model": True}, {"raw_model": 65536},
                       {"tx_bytes": 0}, {"rx_bytes": 5}, {"rx_bytes": 7.0},
                       {"duration_us": None}, {"duration_us": True}, {"duration_us": -1},
                       {"timing_valid": False}, {"raw_truncated": True}):
            with self.subTest(change=change):
                console = self.session()

                def malformed(i, cmd, args):
                    accepted, terminal = (json.loads(line) for line in Serial.normal(i, cmd, args).splitlines())
                    return encoded(accepted) + encoded({**terminal, **change})

                self.port.handler = malformed
                self.failed(lambda: console.command("probe", timeout_s=0.1), "result evidence")

    def test_terminal_arriving_after_deadline_stops(self):
        console = self.session()
        original_read = self.port.read

        def late_read(limit):
            data = original_read(limit)
            if data:
                self.clock.now += 0.2
            return data

        self.port.read = late_read
        self.failed(lambda: console.command("status", timeout_s=0.1), "deadline expired")

    def test_probe_requires_acceptance_first(self):
        console = self.session()
        self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, type="probe"))
        self.failed(lambda: console.command("probe", timeout_s=0.1), "sequence is invalid")

    def test_rejected_probe_is_a_complete_reply(self):
        console = self.session()
        self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, ok=False, result="busy"))
        result = console.command("probe", timeout_s=0.1)
        self.assertFalse(result["ok"])
        self.assertTrue(console.synchronized)

    def test_duplicate_reply_and_partial_trailer(self):
        for tail in (encoded(reply(2, "status", uptime_ms=1000)), b'{"type":'):
            with self.subTest(tail=tail):
                console = self.session()
                self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, uptime_ms=1000)) + tail
                self.failed(lambda: console.command("status", timeout_s=0.1),
                            "duplicate|incomplete trailing")

    def test_pending_response_stops_before_another_send(self):
        console = self.session()
        self.port.input.extend(encoded(reply(1, "version")))
        self.failed(lambda: console.command("probe", timeout_s=0.1), "unsolicited")
        self.assertEqual(len(self.port.writes), 1)

    def test_panic_reset_and_boot_stop_active_session(self):
        for line in (b"Guru Meditation Error\n", b"rst:0x1\n", encoded({"type": "boot"})):
            with self.subTest(line=line):
                console = self.session()
                self.port.handler = lambda *_: line
                self.failed(lambda: console.command("probe", timeout_s=0.1),
                            "fault|does not match")

    def test_invalid_utf8_stops(self):
        console = self.session()
        self.port.handler = lambda *_: b"\xff\n"
        self.failed(lambda: console.command("status", timeout_s=0.1), "UTF-8")

    def test_line_and_total_byte_limits(self):
        for content in (b"x" * (bench.MAX_LINE + 1), b"noise\n" * (bench.MAX_INPUT // 6 + 1)):
            with self.subTest(length=len(content)):
                console = self.session()
                self.port.handler = lambda *_: content
                self.failed(lambda: console.command("status", timeout_s=0.1), "input limit")
                self.assertLessEqual(len(console.buffer), bench.MAX_LINE + 256)

    def test_request_ids_do_not_wrap(self):
        console = self.session()
        console.next_id = 0x100000000
        with self.assertRaisesRegex(bench.BenchError, "IDs exhausted"):
            console.command("probe")
        self.assertEqual(len(self.port.writes), 1)

    def test_device_uptime_regression_stops(self):
        console = self.session()
        console.command("status", timeout_s=0.1)
        self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, uptime_ms=3))
        self.failed(lambda: console.command("status", timeout_s=0.1), "uptime regressed")

    def test_stress_is_exactly_explicit_probes(self):
        console = self.session()
        bench.campaign(console, "stress", count=100, interval_s=0.01, timeout_s=0.1,
                       address=7, sleeper=self.clock.sleep)
        commands = [line.decode("ascii").strip().split()[1:] for line in self.port.writes]
        self.assertEqual(commands.count(["probe", "7"]), 100)
        self.assertEqual(commands.count(["health"]), 100)
        self.assertEqual(commands.count(["memory"]), 100)
        self.assertEqual(sum(parts[0] == "release" for parts in commands), 100)
        self.assertFalse(any(parts[0] in {"recover", "reset"} for parts in commands))
        self.assertEqual(self.events[-1]["event"], "summary")

    def test_watch_never_probes(self):
        console = self.session()
        bench.campaign(console, "watch", count=4, interval_s=0, timeout_s=0.1)
        commands = [line.decode("ascii").strip().split()[1] for line in self.port.writes]
        self.assertEqual(commands, ["version", "stats"] + ["status", "health", "memory"] * 4
                         + ["stats"])

    def test_failed_probe_stops_campaign_without_recovery(self):
        console = self.session()
        normal = self.port.handler

        def fail_probe(request_id, command, args):
            if command == "probe":
                return (encoded(reply(request_id, command, result="accepted", address=1))
                        + encoded(reply(request_id, command, type="probe", ok=False,
                                        address=1,
                                        transport="NO_RESPONSE", codec="NOT_RUN")))
            return normal(request_id, command, args)

        self.port.handler = fail_probe
        with self.assertRaisesRegex(bench.BenchError, "probe failed"):
            bench.campaign(console, "stress", count=5, interval_s=0, timeout_s=0.1)
        self.assertEqual(self.port.writes[-2:], [b"@3 probe 1\n", b"@4 release 103\n"])
        self.assertEqual(len(self.port.writes), 4)

    def test_evidence_contains_memory_and_unknown_readiness(self):
        console = self.session()
        stream = io.StringIO()
        console.emit = bench.Evidence(stream, self.clock)
        bench.campaign(console, "watch", count=1, interval_s=0, timeout_s=0.1)
        records = [json.loads(line) for line in stream.getvalue().splitlines()]
        responses = [row["response"] for row in records if row["event"] == "reply"]
        memory = next(row for row in responses if row["command"] == "memory")
        health = next(row for row in responses if row["command"] == "health")
        self.assertEqual(memory["psram_min"], 7990000)
        self.assertEqual(health["readiness"], "unknown")
        self.assertTrue(all("utc" in row and "elapsed_s" in row for row in records))

    def test_load_campaign_records_configuration_costs_and_memory(self):
        console = self.load_session()
        bench.campaign(console, "load", count=3, interval_s=0, timeout_s=0.1,
                       load=(2000, 5000, 128))
        commands = [line.decode("ascii").strip().split()[1:] for line in self.port.writes]
        expected = [["version"], ["load", "2000", "5000", "128"], ["stats"]]
        for operation in (104, 110, 116):
            expected += [["probe", "1"], ["release", str(operation)], ["status"],
                         ["health"], ["memory"], ["load"]]
        self.assertEqual(commands, expected + [["stats"]])
        summary = self.events[-1]
        self.assertTrue(summary["ok"])
        self.assertEqual(summary["probes_attempted"], 3)
        self.assertEqual(summary["probes_passed"], 3)
        self.assertEqual(summary["probes_failed"], 0)
        self.assertEqual(summary["latency_us"], {"min": 12345, "max": 12345, "mean": 12345})
        self.assertEqual(summary["last_load"]["capture_mode"], "timer")
        self.assertEqual(summary["last_load"]["capture_gap_max_us"], 10)
        self.assertEqual(summary["last_load"]["work_us"], 4000)
        self.assertEqual(summary["last_memory"]["internal_min"], 145000)

    def test_failed_load_probe_collects_cached_evidence_and_stops(self):
        console = self.load_session(probe_failure="TIMING_UNCERTAIN")
        with self.assertRaisesRegex(bench.BenchError, "TIMING_UNCERTAIN"):
            bench.campaign(console, "load", count=100, interval_s=0, timeout_s=0.1,
                           load=(2000, 5000, 128))
        commands = [line.decode("ascii").strip().split()[1] for line in self.port.writes]
        self.assertEqual(commands, ["version", "load", "stats", "probe", "release", "status",
                                    "health", "memory", "load", "stats"])
        summary = self.events[-1]
        self.assertFalse(summary["ok"])
        self.assertEqual(summary["probes_failed"], 1)
        self.assertEqual(summary["probes_passed"], 0)
        self.assertEqual(summary["load_settings"], (2000, 5000, 128))
        self.assertEqual(summary["last_load"]["owner_delay_us"], 5000)
        self.assertIn("TIMING_UNCERTAIN", summary["error"])

    def test_load_cannot_pass_without_exercising_requested_work(self):
        for counters, error in (({"console_lines": 0, "console_dropped": 2}, "no complete lines"),
                                ({"work_iterations": 0, "console_lines": 0}, "no competing task")):
            with self.subTest(counters=counters):
                console = self.load_session()
                normal = self.port.handler

                def inactive(i, command, args):
                    response = normal(i, command, args)
                    if command == "load":
                        response = encoded({**json.loads(response), **counters})
                    return response

                self.port.handler = inactive
                with self.assertRaisesRegex(bench.BenchError, error):
                    bench.campaign(console, "load", count=1, interval_s=0,
                                   timeout_s=0.1, load=(2000, 5000, 128))
                self.assertEqual(self.events[-1]["probes_passed"], 1)
                self.assertFalse(self.events[-1]["ok"])
                self.assertEqual(self.port.writes[-1].split()[1], b"stats")

    def test_load_timeout_never_attempts_diagnostics_or_cleanup(self):
        console = self.load_session()
        normal = self.port.handler
        self.port.handler = lambda i, cmd, args: b"" if cmd == "probe" else normal(i, cmd, args)
        self.failed(lambda: bench.campaign(console, "load", count=100, interval_s=0,
                                           timeout_s=0.02, load=(5000, 20000, 256)), "deadline")
        self.assertEqual([line.decode("ascii").split()[1] for line in self.port.writes],
                         ["version", "load", "stats", "probe"])
        self.assertFalse(self.events[-1]["ok"])
        self.assertEqual(self.events[-1]["load_settings"], (5000, 20000, 256))

    def test_load_rejection_stops_before_any_probe(self):
        console = self.load_session()
        self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, ok=False, result="busy"))
        with self.assertRaisesRegex(bench.BenchError, "load failed"):
            bench.campaign(console, "load", count=2, interval_s=0, timeout_s=0.1,
                           load=(2000, 5000, 128))
        self.assertEqual(len(self.port.writes), 2)
        self.assertEqual(self.events[-1]["probes_attempted"], 0)
        self.assertFalse(self.events[-1]["ok"])

    def test_invalid_load_settings_never_send(self):
        for settings in (None, (), [0, 0, 0], (True, 0, 0), (-1, 0, 0), (5001, 0, 0),
                         (0, 20001, 0), (0, 0, 257), (0, 0, 2.5), (0, 0, "256")):
            with self.subTest(settings=settings):
                console = self.session()
                with self.assertRaises(ValueError):
                    bench.campaign(console, "load", count=1, interval_s=0,
                                   timeout_s=0.1, load=settings)
                self.assertEqual(len(self.port.writes), 1)
        console = self.session()
        with self.assertRaises(ValueError):
            console.command("probe", load=(0, 0, 0))
        with self.assertRaises(ValueError):
            bench.campaign(console, "stress", count=1, interval_s=0,
                           timeout_s=0.1, load=(0, 0, 0))
        self.assertEqual(len(self.port.writes), 1)

    def test_invalid_campaign_count_never_sends(self):
        for count in (True, 1.0, "1", None, 0, 1000001):
            with self.subTest(count=count):
                console = self.session()
                with self.assertRaises(ValueError):
                    bench.campaign(console, "stress", count=count, interval_s=0, timeout_s=0.1)
                self.assertEqual(len(self.port.writes), 1)

    def test_load_reply_requires_matching_settings_and_counters(self):
        for change in ({"workload_us": 1000}, {"owner_delay_us": True}, {"console_bytes": 257},
                       {"ready": False}, {"capture_mode": "driver"}, {"capture_mode": []},
                       {"capture_us": None}, {"capture_samples": True}, {"work_us": -1},
                       {"owner_gap_max_us": 2**64}, {"work_stack_free_bytes": 2.5}):
            with self.subTest(change=change):
                console = self.session()
                self.port.handler = lambda i, cmd, _: encoded({**load_reply(i), **change})
                self.failed(lambda: console.command("load", load=(0, 0, 0), timeout_s=0.1),
                            "load|capture")

    def test_load_configuration_change_stops_before_next_probe(self):
        console = self.load_session()
        normal = self.port.handler

        def changed(request_id, command, args):
            if command == "load" and not args:
                return encoded(load_reply(request_id, (0, 0, 0)))
            return normal(request_id, command, args)

        self.port.handler = changed
        with self.assertRaisesRegex(bench.BenchError, "configuration does not match"):
            bench.campaign(console, "load", count=100, interval_s=0, timeout_s=0.1,
                           load=(2000, 5000, 128))
        commands = [line.decode("ascii").split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("probe"), 1)
        self.assertEqual(commands[-1], "load")
        self.assertFalse(self.events[-1]["ok"])

    def test_load_text_can_follow_terminal_in_fragments(self):
        for trailer in (b"#", b"# load ", b"# load xxxxx"):
            with self.subTest(trailer=trailer):
                console = self.session()
                self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd)) + trailer
                console.command("stats", timeout_s=0.1)
                self.assertTrue(console.synchronized)
                self.assertEqual(console.buffer, trailer)
                self.port.input.extend(b"xxxx\n")
                self.port.handler = Serial.normal
                console.command("status", timeout_s=0.1)
                self.assertEqual(len(self.port.writes), 3)
                self.assertTrue(any(event.get("text", "").startswith("#") for event in self.events))

    def test_partial_load_line_waits_without_sending(self):
        console = self.session()
        self.console.buffer.extend(b"# load ")
        original_read = self.port.read
        polls = 0

        def delayed_tail(limit):
            nonlocal polls
            polls += 1
            if polls == 3:
                self.assertEqual(len(self.port.writes), 1)
                self.port.input.extend(b"xxxx\n")
            return original_read(limit)

        self.port.read = delayed_tail
        console.command("status", timeout_s=0.1)
        self.assertEqual(len(self.port.writes), 2)
        self.assertGreater(self.clock.now, 0)

    def test_incomplete_load_line_deadline_stops_before_send(self):
        console = self.session()
        console.buffer.extend(b"# load ")
        self.failed(lambda: console.command("probe", timeout_s=0.02), "deadline")
        self.assertEqual(len(self.port.writes), 1)

    def test_startup_retains_only_a_partial_fixture_line(self):
        console = self.session(identify=False)
        self.port.input.extend(b"boot text\n# load ")
        console.drain_startup(0.01)
        self.assertEqual(console.buffer, b"# load ")
        self.port.input.extend(b"xxxx\n")
        console.identify(timeout_s=0.1)
        self.assertTrue(console.identified)
        self.assertEqual(self.port.writes, [b"@1 version\n"])

    def test_load_text_does_not_hide_a_watchdog(self):
        console = self.load_session()
        self.port.input.extend(b"# load watchdog timeout\n")
        self.failed(lambda: console.command("probe", timeout_s=0.1), "watchdog fault")
        self.assertEqual(len(self.port.writes), 1)

    def test_memory_requires_valid_measurements(self):
        for change in ({"valid": False}, {"internal_free": True}, {"psram_min": None},
                       {"internal_largest": -1}, {"stack_free_bytes": 2**64}):
            with self.subTest(change=change):
                console = self.session()

                def malformed(request_id, command, args):
                    return encoded({**json.loads(Serial.normal(request_id, command, args)), **change})

                self.port.handler = malformed
                self.failed(lambda: console.command("memory", timeout_s=0.1), "memory")

    def test_load_arguments_are_explicit_and_bounded(self):
        prefix = ["--port", "unused", "--log", "unused.jsonl", "load"]
        args = bench.arguments(prefix + ["--work-us", "5000", "--owner-delay-us", "20000",
                                         "--console-bytes", "256", "--count", "2"])
        self.assertEqual(args.load, (5000, 20000, 256))
        self.assertEqual(args.count, 2)
        for options in (["--work-us", "5001"], ["--owner-delay-us", "20001"],
                        ["--console-bytes", "257"], ["--console-bytes", "-1"]):
            with self.subTest(options=options), redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as exit_status:
                    bench.arguments(prefix + options)
                self.assertEqual(exit_status.exception.code, 2)

    def test_interleaved_local_reply_and_probe_terminal(self):
        for terminal_first in (False, True):
            with self.subTest(terminal_first=terminal_first):
                probe_id = None

                def handler(i, command, args):
                    nonlocal probe_id
                    if command == "probe":
                        probe_id = i
                        return encoded(reply(i, command, result="accepted", address=17))
                    if command == "status":
                        terminal = Serial.normal(probe_id, "probe", ["17"]).splitlines()[1] + b"\n"
                        status = Serial.normal(i, command, args)
                        return terminal + status if terminal_first else status + terminal
                    return Serial.normal(i, command, args)

                console = self.session(handler)
                handle = console.begin("probe", address=17, timeout_s=0.1)
                self.assertTrue(handle.accepted)
                self.assertIsNone(handle.terminal)
                status = console.command("status", timeout_s=0.1)
                self.assertEqual(status["id"], 3)
                terminal = console.wait(handle)
                self.assertEqual((terminal["id"], terminal["command_id"], terminal["operation_id"]),
                                 (2, 2, 102))
                self.assertEqual(terminal["address"], 17)
                self.assertEqual([line.split()[1] for line in self.port.writes],
                                 [b"version", b"probe", b"status"])
                console.command("release", operation_id=102, timeout_s=0.1)
                self.assertTrue(handle.released)
                self.assertFalse(console.operations)

    def test_two_operations_complete_out_of_command_order(self):
        probes = []

        def handler(i, command, args):
            if command == "probe":
                probes.append((i, args))
                return Serial.normal(i, command, args).splitlines()[0] + b"\n"
            if command == "drv":
                terminals = [Serial.normal(p, "probe", a).splitlines()[1] + b"\n"
                             for p, a in reversed(probes)]
                return terminals[0] + Serial.normal(i, command, args) + terminals[1]
            return Serial.normal(i, command, args)

        console = self.session(handler, fragment=17)
        first = console.begin("probe", address=7, timeout_s=0.1)
        second = console.begin("probe", address=8, timeout_s=0.1)
        console.command("drv", timeout_s=0.1)
        self.assertEqual(console.wait(first)["address"], 7)
        self.assertEqual(console.wait(second)["address"], 8)
        self.assertEqual(set(console.operations), {102, 103})
        self.assertEqual(sum(line.split()[1] == b"probe" for line in self.port.writes), 2)

    def test_result_inspection_keeps_original_operation_context(self):
        pending = True

        def handler(i, command, args):
            if command == "probe":
                return encoded(reply(i, command, result="accepted", address=1))
            if command == "result":
                if pending:
                    return encoded(reply(i, command, command_id=2, operation_id=102,
                                         result="pending", recovery=False))
                item = json.loads(Serial.normal(2, "probe", ["1"]).splitlines()[1])
                return encoded({**item, "type": "reply", "id": i, "command": "result"})
            return Serial.normal(i, command, args)

        console = self.session(handler)
        handle = console.begin("probe", timeout_s=0.1)
        report = console.command("result", operation_id=102, timeout_s=0.1)
        self.assertEqual(report["result"], "pending")
        self.assertIsNone(handle.terminal)
        self.port.input.extend(Serial.normal(2, "probe", ["1"]).splitlines()[1] + b"\n")
        terminal = console.wait(handle)
        pending = False
        for _ in range(3):
            view = console.command("result", operation_id=102, timeout_s=0.1)
            self.assertEqual(view["command_id"], terminal["command_id"])
            self.assertEqual(view["observed_latest_us"], terminal["observed_latest_us"])
        self.assertFalse(handle.released)
        console.command("release", operation_id=102, timeout_s=0.1)
        self.assertTrue(handle.released)
        self.assertEqual(sum(line.split()[1] == b"probe" for line in self.port.writes), 1)

    def test_result_inspection_rejects_wrong_known_operation_kind(self):
        for command in ("probe", "recover"):
            for pending in (False, True):
                with self.subTest(command=command, pending=pending):
                    def handler(i, cmd, args):
                        if cmd == command:
                            return Serial.normal(i, cmd, args).splitlines()[0] + b"\n"
                        if cmd == "result":
                            if pending:
                                return encoded(reply(i, cmd, command_id=2, operation_id=102,
                                                     result="pending", recovery=command == "probe"))
                            wrong = "recover" if command == "probe" else "probe"
                            item = json.loads(Serial.normal(2, wrong, ["1"]).splitlines()[1])
                            return encoded({**item, "type": "reply", "id": i, "command": cmd})
                        return Serial.normal(i, cmd, args)

                    console = self.session(handler)
                    handle = console.begin(command, timeout_s=0.1)
                    self.failed(lambda: console.command("result", operation_id=handle.operation_id,
                                                        timeout_s=0.1), "kind does not match")
                    self.assertEqual(sum(line.split()[1] == command.encode() for line in self.port.writes), 1)

    def test_result_inspection_rejects_terminal_regression_to_pending(self):
        for command in ("probe", "recover"):
            with self.subTest(command=command):
                console = self.session()
                handle = console.begin(command, timeout_s=0.1)
                terminal = console.wait(handle)
                self.port.handler = lambda i, cmd, args: encoded(reply(
                    i, cmd, command_id=handle.id, operation_id=handle.operation_id,
                    result="pending", recovery=command == "recover"))
                self.failed(lambda: console.command("result", operation_id=handle.operation_id,
                                                    timeout_s=0.1), "regressed to pending")
                self.assertIs(handle.terminal, terminal)

    def test_result_inspection_rejects_changed_retained_terminal_evidence(self):
        for command, fields in (
                ("probe", {"raw_model": 61}),
                ("probe", {"observed_earliest_us": 13301, "delivered_us": 16001}),
                ("probe", {"ok": False, "outcome": "cancelled", "transport": "CANCELLED"}),
                ("recover", {"finished_us": 1501}),
                ("recover", {"ok": False, "outcome": "expired", "finished_us": 50000})):
            with self.subTest(command=command, fields=fields):
                console = self.session()
                handle = console.begin(command, timeout_s=0.1)
                terminal = console.wait(handle)
                self.port.handler = lambda i, cmd, args: encoded({
                    **terminal, **fields, "type": "reply", "id": i, "command": cmd})
                self.failed(lambda: console.command("result", operation_id=handle.operation_id,
                                                    timeout_s=0.1), "changed immutable retained terminal")
                self.assertIs(handle.terminal, terminal)

    def test_recovery_inspection_preserves_terminal_with_new_query_routing(self):
        console = self.session(fragment=7)
        handle = console.begin("recover", timeout_s=0.1)
        terminal = console.wait(handle)
        self.port.handler = lambda i, cmd, args: encoded({
            **terminal, "type": "reply", "id": i, "command": cmd})
        for _ in range(3):
            inspected = console.command("result", operation_id=handle.operation_id, timeout_s=0.1)
            self.assertNotEqual(inspected["id"], terminal["id"])
            self.assertEqual(inspected["finished_us"], terminal["finished_us"])
        self.assertTrue(console.synchronized)
        self.assertEqual(sum(line.split()[1] == b"recover" for line in self.port.writes), 1)

    def test_cancel_is_explicit_local_work_with_original_terminal(self):
        def handler(i, command, args):
            if command == "probe":
                return encoded(reply(i, command, result="accepted", address=1))
            if command == "cancel":
                return (Serial.normal(i, command, args)
                        + encoded(reply(2, "probe", type="probe", address=1, ok=False,
                                        transport="CANCELLED", codec="NOT_CHECKED")))
            return Serial.normal(i, command, args)

        console = self.session(handler)
        handle = console.begin("probe", timeout_s=0.1)
        self.assertTrue(console.command("cancel", operation_id=102, timeout_s=0.1)["ok"])
        terminal = console.wait(handle)
        self.assertEqual((terminal["id"], terminal["operation_id"], terminal["transport"]),
                         (2, 102, "CANCELLED"))
        self.assertFalse(terminal["ok"])
        console.command("release", operation_id=102, timeout_s=0.1)
        self.assertEqual([line.split()[1] for line in self.port.writes],
                         [b"version", b"probe", b"cancel", b"release"])

    def test_recovery_explicitly_waits_and_releases_distinct_result(self):
        console = self.session(fragment=1)
        terminal = console.command("recover", timeout_s=0.1)
        self.assertEqual((terminal["type"], terminal["command"], terminal["outcome"]),
                         ("recovery", "recover", "recovered"))
        self.assertEqual(terminal["operation_id"], 102)
        self.assertEqual(self.port.writes, [b"@1 version\n", b"@2 recover\n", b"@3 release 102\n"])
        self.assertFalse(console.operations)

    def test_recovery_can_settle_interrupted_probe_before_its_own_terminal(self):
        def handler(i, command, args):
            if command == "probe":
                return encoded(reply(i, command, result="accepted", address=1))
            if command == "recover":
                return (encoded(reply(2, "probe", type="probe", address=1, ok=False,
                                      transport="CANCELLED", codec="NOT_CHECKED"))
                        + Serial.normal(i, command, args))
            return Serial.normal(i, command, args)

        console = self.session(handler)
        probe = console.begin("probe", timeout_s=0.1)
        recovery = console.begin("recover", timeout_s=0.1)
        self.assertEqual(console.wait(probe)["operation_id"], 102)
        self.assertEqual(console.wait(recovery)["operation_id"], 103)
        self.assertEqual(set(console.operations), {102, 103})
        console.command("release", operation_id=102, timeout_s=0.1)
        console.command("release", operation_id=103, timeout_s=0.1)
        self.assertFalse(console.operations)

    def test_wrong_terminal_operation_or_original_command_id_stops(self):
        for fields in ({"operation_id": 103}, {"operation_id": True}, {"operation_id": None},
                       {"operation_id": 0}, {"command_id": 3}, {"command_id": True}):
            with self.subTest(fields=fields):
                console = self.session()

                def malformed(i, command, args):
                    accepted, terminal = [json.loads(line) for line in Serial.normal(i, command, args).splitlines()]
                    return encoded(accepted) + encoded({**terminal, **fields})

                self.port.handler = malformed
                self.failed(lambda: console.command("probe", timeout_s=0.1), "does not match acceptance")
                self.assertEqual(len(self.port.writes), 2)

    def test_missing_or_invalid_admission_operation_id_stops(self):
        for operation in (None, True, 0, -1, 0x100000000):
            with self.subTest(operation=operation):
                console = self.session()
                self.port.handler = lambda i, command, args: encoded(reply(
                    i, command, result="accepted", address=1, operation_id=operation))
                self.failed(lambda: console.begin("probe", timeout_s=0.1), "operation ID")
                self.assertEqual(len(self.port.writes), 2)

    def test_duplicate_terminal_never_causes_release_or_retry(self):
        console = self.session()

        def duplicated(i, command, args):
            accepted, terminal = Serial.normal(i, command, args).splitlines()
            return accepted + b"\n" + terminal + b"\n" + terminal + b"\n"

        self.port.handler = duplicated
        self.failed(lambda: console.command("probe", timeout_s=0.1), "duplicate|incomplete trailing")
        self.assertEqual(len(self.port.writes), 2)

    def test_delayed_duplicate_detected_before_new_send(self):
        console = self.session()
        terminal = console.command("probe", timeout_s=0.1)
        self.port.input.extend(encoded(terminal))
        before = len(self.port.writes)
        self.failed(lambda: console.command("status", timeout_s=0.1), "unsolicited")
        self.assertEqual(len(self.port.writes), before)

    def test_interleaved_bad_operation_poisoning_prevents_further_commands(self):
        def handler(i, command, args):
            if command == "probe":
                return encoded(reply(i, command, result="accepted", address=1))
            if command == "status":
                terminal = json.loads(Serial.normal(2, "probe", ["1"]).splitlines()[1])
                return encoded({**terminal, "operation_id": 999}) + Serial.normal(i, command, args)
            return Serial.normal(i, command, args)

        console = self.session(handler)
        console.begin("probe", timeout_s=0.1)
        self.failed(lambda: console.command("status", timeout_s=0.1), "does not match acceptance")
        self.assertEqual([line.split()[1] for line in self.port.writes], [b"version", b"probe", b"status"])

    def test_result_release_and_cancel_require_strict_operation_echo(self):
        for command in ("result", "release", "cancel"):
            with self.subTest(command=command):
                console = self.session()
                self.port.handler = lambda i, name, args: encoded(reply(i, name, operation_id=999))
                self.failed(lambda: console.command(command, operation_id=17, timeout_s=0.1),
                            "operation ID does not match")

    def test_release_rejection_poisons_convenience_command(self):
        console = self.session()

        def reject_release(i, command, args):
            if command == "release":
                return encoded(reply(i, command, ok=False, result="busy", operation_id=int(args[0])))
            return Serial.normal(i, command, args)

        self.port.handler = reject_release
        self.failed(lambda: console.command("probe", timeout_s=0.1), "release was rejected")
        self.assertEqual([line.split()[1] for line in self.port.writes], [b"version", b"probe", b"release"])

    def test_recovery_terminal_requires_checked_outcome_and_deadlines(self):
        for fields in ({"outcome": "unknown"}, {"outcome": []}, {"ok": False}, {"recovery": False},
                       {"finished_us": None}, {"deadline_us": True}, {"requested_us": 50000},
                       {"finished_us": 50000}, {"transport": None}, {"transport": "FRAME"}):
            with self.subTest(fields=fields):
                console = self.session()

                def malformed(i, command, args):
                    accepted, terminal = [json.loads(line) for line in Serial.normal(i, command, args).splitlines()]
                    return encoded(accepted) + encoded({**terminal, **fields})

                self.port.handler = malformed
                self.failed(lambda: console.command("recover", timeout_s=0.1), "recovery")
                self.assertEqual(len(self.port.writes), 2)

    def test_probe_observation_bounds_are_separate_from_delivery(self):
        for fields in ({"observed_latest_us": None}, {"delivered_us": True},
                       {"observed_earliest_us": 14000}, {"delivered_us": 12000}):
            with self.subTest(fields=fields):
                console = self.session()

                def malformed(i, command, args):
                    accepted, terminal = [json.loads(line) for line in Serial.normal(i, command, args).splitlines()]
                    return encoded(accepted) + encoded({**terminal, **fields})

                self.port.handler = malformed
                self.failed(lambda: console.command("probe", timeout_s=0.1), "probe")

    def test_original_host_deadline_is_not_renewed_by_local_commands(self):
        def handler(i, command, args):
            if command == "probe":
                return encoded(reply(i, command, result="accepted", address=1))
            return Serial.normal(i, command, args)

        console = self.session(handler)
        handle = console.begin("probe", timeout_s=0.02)
        self.clock.sleep(0.01)
        console.command("status", timeout_s=0.1)
        self.assertEqual(handle.deadline, 0.02)
        self.clock.sleep(0.01)
        self.failed(lambda: console.command("status", timeout_s=0.1), "deadline expired")
        self.assertEqual(len(self.port.writes), 3)

    def test_host_handles_have_a_fixed_limit_and_wait_frees_capacity(self):
        console = self.session()
        handles = [console.begin("stats", timeout_s=0.1) for _ in range(bench.MAX_COMMANDS)]
        before = len(self.port.writes)
        with self.assertRaisesRegex(bench.BenchError, "outstanding command limit"):
            console.begin("stats", timeout_s=0.1)
        self.assertTrue(console.synchronized)
        self.assertEqual(len(self.port.writes), before)
        console.wait(handles[0])
        console.command("stats", timeout_s=0.1)
        self.assertEqual(len(console.pending), bench.MAX_COMMANDS - 1)

    def test_retained_probe_quota_requires_release_and_does_not_replay(self):
        console = self.session()
        handles = []
        for _ in range(bench.MAX_PROBES):
            handle = console.begin("probe", timeout_s=0.1)
            console.wait(handle)
            handles.append(handle)
        self.assertEqual(len(console.operations), bench.MAX_PROBES)
        recovery = console.begin("recover", timeout_s=0.1)
        console.wait(recovery)
        self.assertEqual(len(console.operations), bench.MAX_OPERATIONS)
        self.port.handler = lambda i, command, args: encoded(reply(i, command, ok=False, result="results_full"))
        rejection = console.command("probe", timeout_s=0.1)
        self.assertFalse(rejection["ok"])
        self.assertEqual(len(console.operations), bench.MAX_OPERATIONS)
        self.port.handler = Serial.normal
        console.command("release", operation_id=handles[0].operation_id, timeout_s=0.1)
        replacement = console.begin("probe", timeout_s=0.1)
        self.assertGreater(replacement.operation_id, recovery.operation_id)
        console.wait(replacement, release=True)

    def test_operation_ids_never_reuse_after_explicit_release(self):
        console = self.session()
        console.command("probe", timeout_s=0.1)

        def reused(i, command, args):
            acceptance = json.loads(Serial.normal(i, command, args).splitlines()[0])
            return encoded({**acceptance, "operation_id": 102})

        self.port.handler = reused
        self.failed(lambda: console.begin("probe", timeout_s=0.1), "not monotonic")

    def test_invalid_control_arguments_never_send(self):
        console = self.session()
        for command in ("result", "release", "cancel"):
            for operation_id in (None, True, 0, -1, 0x100000000, "1"):
                with self.subTest(command=command, operation_id=operation_id):
                    with self.assertRaises(ValueError):
                        console.command(command, operation_id=operation_id)
        with self.assertRaises(ValueError):
            console.command("probe", operation_id=17)
        self.assertEqual(len(self.port.writes), 1)

    def test_protocol_one_cannot_hide_missing_operation_correlation(self):
        console = self.session(identify=False)
        self.port.handler = lambda i, command, args: encoded(reply(i, command, product="MotorControl-RS",
                                                                  protocol=1, outstanding_capacity=9))
        self.failed(lambda: console.identify(timeout_s=0.1), "not the supported")

    def test_begin_rejects_admission_received_after_its_original_deadline(self):
        console = self.session()
        self.port.handler = lambda i, command, args: encoded(reply(i, command, result="accepted", address=1))
        original_read = self.port.read

        def late_read(limit):
            data = original_read(limit)
            if data:
                self.clock.sleep(0.2)
            return data

        self.port.read = late_read
        self.failed(lambda: console.begin("probe", timeout_s=0.1), "deadline expired")
        self.assertEqual(len(self.port.writes), 2)


if __name__ == "__main__":
    unittest.main()
