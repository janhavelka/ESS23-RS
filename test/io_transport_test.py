"""Independent IO frames through the real host correlation/retention parser."""
import copy
import unittest

from bench_probe_test import bench, Clock, Serial, encoded, reply


def sealed(prefix):
    raw = bytes(prefix)
    return raw + bench.wire_crc(raw).to_bytes(2, "little")


def terminal(request_id, update=False, echo=False, unknown=False):
    values = [0, 1, 2, 3, 0, 0, 0, 0, 0]
    if unknown: values = [0x8010, 99, 2, 3, 0, 0x40, 11, 8, 0x80]
    if update:
        frames = [(0x41, 1, True, sealed((1, 6, 0, 0x41, 0, 0))),
                  (0x41, 1, False, sealed((1, 3, 2, 0, 0)))]
    else:
        frames = []
        for (reg, count), words in zip(bench.IO_WINDOWS, (values[:5], values[5:8], values[8:])):
            payload = [1, 3, count * 2]
            for word in words: payload.extend((word >> 8, word & 255))
            frames.append((reg, count, False, sealed(payload)))
    evidence = [[index, reg, count, write, 0, raw.hex(), len(raw), 8, True, not (write and echo), True, False,
                 1100 + index * 100, 1110 + index * 100, 1120 + index * 100, 1, "OK", 0, 0,
                 1000 if index == 0 else 1020 + index * 100]
                for index, (reg, count, write, raw) in enumerate(frames)]
    mask = 1 << 10
    known = sum(1 << (9 + index) for index, value in enumerate(values)
                if (value <= 17 if 1 <= index <= 4 else value in (*range(6), 9, 10) if index in (6, 7)
                    else value <= (15 if index == 0 else 3)))
    return reply(request_id, "io", type="io", command_id=request_id, operation_id=request_id + 100,
                 driver=True, driver_group="io", driver_kind="update" if update else "read",
                 echo_readback_policy=echo, settlement="stored_readback", state="succeeded", outcome="success",
                 status="OK", detail=0, target=1, address=1, generation=9, configuration_generation=3,
                 started_us=1000, deadline_us=20000, stationary_valid_until_us=20000,
                 serviced_us=evidence[-1][14], completed_steps=len(evidence), fields=mask if update else 0,
                 effects=mask if update else 0, uncertain=False, atomic=False, active_settings_known=False,
                 progress_columns=bench.DRIVER_PROGRESS_COLUMNS,
                 progress=[[mask, 0x41, 1, 0, not echo, True, 0, False, 0, "unknown" if echo else "acknowledged"]] if update else [],
                 evidence_columns=bench.DRIVER_EVIDENCE_COLUMNS, evidence=evidence,
                 observation=None if update else dict(raw=values, known_fields=known,
                     unknown_input_polarity_bits=values[0] & ~15,
                     unknown_output_polarity_bits=values[5] & ~3,
                     unknown_custom_output_bits=values[8] & ~3))


class IoTransport(unittest.TestCase):
    def session(self, handler=None):
        self.clock = Clock(); self.port = Serial(handler, fragment=17); self.events = []
        console = bench.Console(self.port, clock=self.clock, sleeper=self.clock.sleep,
                                on_event=lambda event, **data: self.events.append({"event": event, **data}))
        console.identify(timeout_s=0.1)
        return console

    def handler(self, update=False, echo=False, mutate=None):
        retained = {}
        def dispatch(request_id, command, args):
            if command == "io":
                self.assertIn(args[0], ("read", "set"))
                record = terminal(request_id, update, echo)
                if mutate: mutate(record)
                retained[request_id + 100] = record
                return encoded(reply(request_id, "io", result="accepted", address=1, operation_id=request_id + 100)) + encoded(record)
            if command == "result":
                return encoded(dict(retained[int(args[0])], type="reply", id=request_id, command="result"))
            return Serial.normal(request_id, command, args)
        return dispatch

    def test_exact_none_grammar_and_bounds_without_traffic(self):
        console = self.session()
        self.assertEqual(bench.driver_arguments(("set", "x0", "none", "y1", "none"), "io"), {"x0": 0, "y1": 0})
        for args in (None, (), ("set",), ("set", "x4", "none"), ("set", "y2", "none"),
                     ("set", "x0", "0.0"), ("set", "x0", "-1"), ("set", "custom", "none"),
                     ("set", "x0", "0", "x0", "1"), ("set", "x0", "65536"), ("set", "direction", "0")):
            with self.subTest(args=args), self.assertRaises(ValueError): console.begin("io", driver_args=args)
        self.assertEqual(len(self.port.writes), 1)

    def test_read_and_disable_correlation_inspection_release(self):
        for update, echo in ((False, False), (True, False), (True, True)):
            console = self.session(self.handler(update, echo))
            handle = console.begin("io", address=1, driver_args=("set", "x0", "none") if update else ("read",))
            result = console.wait(handle)
            self.assertEqual(result["driver_group"], "io")
            inspected = console.command("result", operation_id=handle.operation_id)
            self.assertEqual(inspected["evidence"], result["evidence"])
            self.assertTrue(console.command("release", operation_id=handle.operation_id)["ok"])
            self.assertIn(b"io", self.port.writes[1])
            if echo: self.assertEqual(result["progress"][0][9], "unknown")

    def test_unknown_raw_is_preserved(self):
        record = terminal(2, unknown=True)
        bench.Console._check_driver(record, 1, ("read",), "io")
        for mutate in (lambda r: r["observation"]["raw"].__setitem__(1, 0),
                       lambda r: r["observation"].update(known_fields=0x3FE00),
                       lambda r: r["observation"].update(unknown_input_polarity_bits=0),
                       lambda r: r["observation"].update(pair_known=False)):
            broken = copy.deepcopy(record); mutate(broken)
            with self.assertRaises(bench.BenchError): bench.Console._check_driver(broken, 1, None, "io")

    def test_echo_settlement_cannot_invent_ack_activation_or_response(self):
        record = terminal(2, True, True)
        bench.Console._check_driver(record, 1, ("set", "x0", "none"), "io")
        mutations = [lambda r: r.update(echo_readback_policy=False), lambda r: r.update(active_settings_known=True),
                     lambda r: r.update(settlement="active"), lambda r: r.update(driver_group="drive"),
                     lambda r: r["progress"][0].__setitem__(4, True),
                     lambda r: r["progress"][0].__setitem__(9, "acknowledged"),
                     lambda r: r["evidence"][1].__setitem__(9, False),
                     lambda r: r["evidence"][0].__setitem__(14, 1250),
                     lambda r: r.update(effects=0), lambda r: r.update(uncertain=True)]
        for mutate in mutations:
            broken = copy.deepcopy(record); mutate(broken)
            with self.assertRaises(bench.BenchError): bench.Console._check_driver(broken, 1, None, "io")

    def test_wrong_command_operation_and_address_poison_session_without_retry(self):
        for mutate in (lambda r: r.update(command="driver"), lambda r: r.update(type="driver"),
                       lambda r: r.update(command_id=999), lambda r: r.update(operation_id=999),
                       lambda r: r.update(address=2), lambda r: r.update(driver_group="drive")):
            console = self.session(self.handler(mutate=mutate))
            with self.assertRaises(bench.BenchError): console.command("io", address=1, driver_args=("read",))
            self.assertFalse(console.synchronized)
            self.assertEqual(len(self.port.writes), 2)
            with self.assertRaises(bench.BenchError): console.command("io", driver_args=("read",))
            self.assertEqual(len(self.port.writes), 2)

    def test_finite_io_campaign_and_argparse(self):
        for update in (False, True):
            console = self.session(self.handler(update, update))
            bench.driver_read_campaign(console, timeout_s=0.1, address=1, command="io",
                                       driver_args=("set", "x0", "none") if update else ("read",))
            self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "io", "result", "release"])
            self.assertEqual(self.events[-1]["mode"], "io-set" if update else "io-read")
        args = bench.arguments(["--port", "fake", "--log", "unused", "io", "set", "x0", "none"])
        self.assertEqual(args.driver_args, ("set", "x0", "none"))


if __name__ == "__main__":
    unittest.main()
