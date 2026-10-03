"""Exercise host framing and finite campaigns using a fake serial stream only."""

import importlib.util
import io
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bench_probe", ROOT / "scripts/bench_probe.py")
bench = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bench)


def encoded(item):
    return (json.dumps(item) + "\n").encode("ascii")


def reply(request_id, command, **fields):
    return {"type": "reply", "id": request_id, "command": command,
            "profile": "ess_rs", "ok": True, **fields}


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
                                 protocol=1, version="0.test"))
        if command == "probe":
            address = int(args[0]) if args else 1
            return (encoded(reply(request_id, command, result="accepted", address=address))
                    + encoded(reply(request_id, command, type="probe", address=address,
                                    transport="FRAME", codec="OK", detail=0, raw_model=60,
                                    duration_us=12345, tx_bytes=8, rx_bytes=7,
                                    identity="responder_only")))
        if command == "status":
            return encoded(reply(request_id, command, busy=False, recovery_required=False,
                                 phase="DONE", transport="FRAME", last_probe_known=True,
                                 last_probe_ok=True, raw_model=60, age_ms=20, uptime_ms=1000))
        if command == "health":
            return encoded(reply(request_id, command, communication="current",
                                 readiness="unknown", alarms="unknown", state="unknown"))
        if command == "memory":
            return encoded(reply(request_id, command, valid=True, internal_free=150000,
                                 internal_min=145000, psram_free=8000000, psram_min=7990000,
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

    def test_fragmented_probe_and_cached_health(self):
        console = self.session(fragment=1)
        result = console.command("probe", address=17, timeout_s=0.1)
        self.assertEqual(result["raw_model"], 60)
        self.assertEqual(result["identity"], "responder_only")
        self.assertEqual(self.port.writes[-1], b"@2 probe 17\n")
        health = console.command("health", timeout_s=0.1)
        self.assertEqual(health["readiness"], "unknown")
        accepts = [entry for entry in self.events if entry["event"] == "reply"
                   and entry["response"].get("result") == "accepted"]
        self.assertEqual(len(accepts), 1)

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
        for command, address in (("probe\nreset", None), ("recover", None),
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
        self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, result="accepted"))
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
        for tail in (encoded(reply(2, "status")), b'{"type":'):
            with self.subTest(tail=tail):
                console = self.session()
                self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd)) + tail
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
                return (encoded(reply(request_id, command, result="accepted"))
                        + encoded(reply(request_id, command, type="probe", ok=False,
                                        transport="NO_RESPONSE", codec="NOT_RUN")))
            return normal(request_id, command, args)

        self.port.handler = fail_probe
        with self.assertRaisesRegex(bench.BenchError, "probe failed"):
            bench.campaign(console, "stress", count=5, interval_s=0, timeout_s=0.1)
        self.assertEqual(len(self.port.writes), 3)
        self.assertEqual(self.port.writes[-1], b"@3 probe 1\n")

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


if __name__ == "__main__":
    unittest.main()
