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
    if command in ("status", "health"):
        fields.setdefault("state_blocks", [dict(block=i, valid=False, current=False, fresh=False, source="absent",
            target=0, address=0, generation=0, operation_id=0, attempt_known=False, last_attempt_ok=False,
            last_attempt_us=0, last_attempt_target=0, last_attempt_address=0, last_attempt_generation=0,
            last_attempt_operation_id=0, last_attempt_status="OK", last_attempt_detail=0, last_success_us=0,
            observed_earliest_us=0, observed_latest_us=0, delivered_us=0, age_us=None, value=None) for i in range(3)])
        fields["state_blocks"][2].setdefault("current_config_operation_id", 0)
        fields["state_blocks"][2].setdefault("interpretation_current", False)
        for key, value in dict(now_us=20000, stale_after_ms=5000, selected_target=1, selected_address=1,
            selected_generation=9, monitoring="disabled", atomic_snapshot=False, sample_time="drive_internal_age_undocumented",
            communication_known=False, communication_target=0, communication_address=0, communication_generation=0,
            communication_earliest_us=0, communication_latest_us=0, age_source="model_probe", communication_age_us=None).items():
            fields.setdefault(key, value)
        if command == "health":
            fields.setdefault("alarms", "unknown"); fields.setdefault("state", "unknown")
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
                 timer_callbacks=180, sample_gap_limit_us=85,
                 sample_gap_exceeded=False, capture_high_water=7,
                 owner_gap_max_us=5001, capture_gap_max_us=10,
                 work_stack_free_bytes=2500, **fields)


def typed_terminal(request_id, kind):
    """Literal independently reviewed windows/vectors, not firmware helpers."""
    fixtures = (
        ((0, 4), "0103000000044409", "0103084EEA12340001A58103E3"),
    ) if kind == "identity" else (
        ((6, 2), "010300060002240A", "010304000080115BFF"),
        ((8, 2), "01030008000245C9", "010304A5F5000388CC"),
        ((10, 3), "0103000A000325C9", "01030612345678FFFF03E2"),
    ) if kind == "state" else (
        ((0x10, 2), "010300100002C5CE", "01030400010640A9A3"),
        ((0x13, 3), "010300130003F40E", "010306002A00020003D972"),
        ((0x17, 3), "010300170003B5CF", "0103060001000100018CB5"),
        ((0x40, 5), "010300400005841D", "01030AA5F50000000100020011557C"),
        ((0x100, 2), "010301000002C5F7", "010304000210005633"),
    )
    steps = []
    for index, ((first, count), tx, rx) in enumerate(fixtures):
        steps.append(dict(step=index, first=first, count=count, event=0, status="OK", detail=0,
                          frame_error=0, qualified=True, attempted_us=1000 if index == 0 else 1100 + index * 1000, earliest_us=2000 + index * 1000,
                          latest_us=2050 + index * 1000, delivered_us=2100 + index * 1000,
                          tx=tx, rx=rx, received_length=len(rx) // 2, tx_accepted=8,
                          execution_unknown=False, transport_detail=1))
    result = reply(request_id, "read-" + kind, type="read", command_id=request_id,
                   operation_id=request_id + 100, read_kind=kind, state="succeeded", outcome="success",
                   status="OK", detail=0, target=1, address=1, generation=9, started_us=1000,
                   deadline_us=20000, serviced_us=steps[-1]["delivered_us"], completed_steps=len(steps),
                   active_serial=dict(known=True, baud=115200, data_bits=8, parity=1, stop_bits=1), steps=steps)
    if kind == "identity":
        result[kind] = dict(raw_model=0x4EEA, raw_version=0x1234, raw_active_node=1, raw_dip=0xA581,
                            active_node_known=True, active_node=1, model_resolution=2, version_resolution=3,
                            dip_resolution=4, dip_issues=320, model="mapping_unresolved", firmware="mapping_unresolved",
                            dip="mapping_conflict_unresolved")
    elif kind == "state":
        result["decode_config"] = dict(operation_id=90, word_order_known=True, word_order=1, algorithm_known=True, algorithm=2)
        result["state_blocks"] = [
            dict(block=0, config_operation_id=90, raw_alarm=0, alarm_known=True, raw_motion=0x8011, unknown_motion_bits=0x8000,
                 in_position=True, homing_complete=False, running=False, alarm_flag=False, released=True, enabled=False,
                 positive_soft_limit=False, negative_soft_limit=False),
            dict(block=1, config_operation_id=90, raw_inputs=0xA5F5, raw_outputs=3, unknown_input_bits=0xA5F0, unknown_output_bits=0,
                 inputs=[True, False, True, False], outputs=[True, True], levels="logical_valid_not_voltage"),
            dict(block=2, config_operation_id=90, position_words=[0x1234, 0x5678], raw_speed=0xFFFF, pair_known=True,
                 raw_position=0x56781234, position_source=2, word_order_resolution=0, position_source_resolution=0,
                 position_signed_resolution=7, position_scale_resolution=5, speed_signed_resolution=7, speed_unit_resolution=8,
                 physical_units="unresolved", raw_encoder_counts=None)]
    else:
        result[kind] = dict(raw=dict(direction=1, subdivision=1600, custom_node=42, baud=2, format=3,
                                    over_limit_stop=1, soft_limit_enable=1, word_order=1,
                                    input_polarity=0xA5F5, algorithm=2, encoder_resolution=4096),
                            known=dict(direction=True, baud=True, format=True, over_limit_stop=True,
                                       soft_limit_enable=True, word_order=True, algorithm=True),
                            unknown_polarity_bits=0xA5F0, subdivision_resolution=5, encoder_resolution=0,
                            subdivision_issues=4, custom_node_issues=64, over_limit_stop_issues=1024,
                            soft_limit_issues=256, encoder_scale_source=3,
                            stored_serial=dict(baud_code=2, format_code=3, activation="power_cycle_required"),
                            units="command_scale_unresolved_encoder_readback_only",
                            inputs=[dict(function=value, known=True, inverted=bool(5 & (1 << index)),
                                         wiring=0, level_known=False, level=None)
                                    for index, value in enumerate((0, 1, 2, 17))])
    return result


def cached_state(request_id, command="health"):
    item = reply(request_id, command, readiness="unknown", communication="current", alarms="clear", state="observed")
    values = typed_terminal(2, "state")["state_blocks"]
    item["now_us"] = 5000
    if command == "status": item["uptime_ms"] = 5
    item.update(communication_known=True, communication_target=1, communication_address=1, communication_generation=9,
                communication_earliest_us=1000, communication_latest_us=4050, communication_age_us=4000)
    for index, block in enumerate(item["state_blocks"]):
        block.update(valid=True, current=True, fresh=True, source="checked_rtu_register", target=1, address=1, generation=9,
            operation_id=102, attempt_known=True, last_attempt_ok=True, last_attempt_us=1000 + index * 1000,
            last_attempt_target=1, last_attempt_address=1, last_attempt_generation=9, last_attempt_operation_id=102,
            last_success_us=2050 + index * 1000, observed_earliest_us=1000 + index * 1000,
            observed_latest_us=2050 + index * 1000, delivered_us=2100 + index * 1000,
            age_us=4000 - index * 1000, value=values[index])
    item["state_blocks"][2].update(current_config_operation_id=90, interpretation_current=True)
    return item


class TypedSerial:
    """Read operation storage survives inspections until explicit release."""
    def __init__(self, mutate=None, admission_only=False):
        self.retained = {}
        self.mutate = mutate
        self.admission_only = admission_only

    def __call__(self, request_id, command, args):
        if command == "read" or (command == "health" and args and args[0] == "check"):
            kind = "state" if command == "health" else args[0]
            terminal = typed_terminal(request_id, kind)
            if self.mutate:
                self.mutate(terminal)
            self.retained[terminal["operation_id"]] = terminal
            accepted = reply(request_id, "read-" + kind, result="accepted", address=1,
                             operation_id=request_id + 100, read_kind=kind)
            return encoded(accepted) + (b"" if self.admission_only else encoded(terminal))
        if command == "result":
            original = self.retained[int(args[0])]
            return encoded({**original, "type": "reply", "id": request_id, "command": "result"})
        if command == "release":
            self.retained.pop(int(args[0]))
        return Serial.normal(request_id, command, args)


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
    def test_host_preparation_preserves_integer_precision_and_correlation(self):
        value = 9007199254740993
        def handler(request_id, command, args):
            if command == "prepare":
                return encoded(reply(request_id, command, code="OK", detail=0, bus_traffic=False,
                    motion_command=False, wire_motion="unimplemented", configuration_generation=2,
                    target=1, address=1, binding_generation=1,
                    requested=dict(numerator=value, denominator=1, unit="steps", frame=0, relative=True, basis=0, rounding=0),
                    requested_native=dict(integral=value, numerator=0, denominator=1, negative=False),
                    requested_native_approximate=None, effective_native=value, endpoint_known=False, endpoint_native=0,
                    displacement_known=True, displacement_native=value, zero_displacement=False,
                    rounding_error=0, approximation_error_bound=0, exact_arithmetic=True))
            return Serial.normal(request_id, command, args)
        console = self.session(handler)
        result = console.command("prepare", host_args=("relative", str(value), "steps", "native", "actual"))
        self.assertEqual(result["effective_native"], value)
        self.assertEqual(self.port.writes[-1], b"@2 prepare relative 9007199254740993 steps native actual\n")
        self.assertFalse(console.operations)
        result["bus_traffic"] = True
        with self.assertRaises(bench.BenchError):
            bench.Console._check_axis(result, True)
        result["bus_traffic"] = False
        result["requested_native"]["denominator"] = False
        with self.assertRaises(bench.BenchError):
            bench.Console._check_axis(result, True)

    def test_host_arguments_are_bounded_before_transmission(self):
        console = self.session()
        for arguments in (None, (), ["config"], ("config\nrecover",), ("config set",), ("\u00e9",),
                          ("x",) * 9, ("x" * 128,)):
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                console.command("axis", host_args=arguments)
        with self.assertRaises(ValueError):
            console.command("probe", host_args=("config",))
        self.assertEqual(self.port.writes, [b"@1 version\n"])

    def test_host_axis_rejection_remains_local(self):
        def handler(request_id, command, args):
            if command == "axis":
                return encoded(reply(request_id, command, ok=False, code="INVALID_CONFIG", detail=7,
                    bus_traffic=False, motion_command=False))
            return Serial.normal(request_id, command, args)
        console = self.session(handler)
        self.assertFalse(console.command("axis", host_args=("origin", "0"))["ok"])
        self.assertTrue(console.synchronized)
        self.assertFalse(console.operations)

    def test_malformed_exact_fraction_rejections_are_not_replayed(self):
        def handler(request_id, command, args):
            if command in ("axis", "prepare"):
                return encoded(reply(request_id, command, ok=False, code="ILLEGAL_VALUE", detail=1,
                    bus_traffic=False, motion_command=False))
            return Serial.normal(request_id, command, args)
        console = self.session(handler)
        requests = (
            ("prepare", ("relative", "1/2.0", "steps", "native", "actual", "nearest", "1")),
            ("prepare", ("absolute", "1/2.000", "steps", "native", "nearest", "1")),
            ("axis", ("config", "set", "command", "1/2.0")),
        )
        for command, arguments in requests:
            with self.subTest(command=command, arguments=arguments):
                before = len(self.port.writes)
                result = console.command(command, host_args=arguments)
                self.assertFalse(result["ok"])
                self.assertEqual(len(self.port.writes), before + 1)
                self.assertTrue(console.synchronized)
                self.assertFalse(console.operations)

    def test_zero_radian_and_approximate_zero_targets_keep_provenance(self):
        scenarios = [
            (("relative", "0", "rad", "motor", "actual"), 0, True, 0, 0),
            (("absolute", "0", "rad", "motor"), -(2**63), True, 0, 0),
            (("absolute", "0", "rad", "motor"), 2**63 - 1, True, 0, 0),
            (("absolute", "1", "rad", "motor", "nearest", "1", "0.0001"), 0, False, 0.1549, 0.00000001),
        ]
        for arguments, effective, exact, approximate, error in scenarios:
            with self.subTest(arguments=arguments, effective=effective):
                relative = arguments[0] == "relative"
                def handler(request_id, command, args):
                    if command == "prepare":
                        return encoded(reply(request_id, command, code="OK", detail=0, bus_traffic=False,
                            motion_command=False, wire_motion="unimplemented", configuration_generation=2,
                            target=1, address=1, binding_generation=1,
                            requested=dict(numerator=0 if exact else 1, denominator=1, unit="rad", frame=1,
                                relative=relative, basis=0, rounding=0 if exact else 1),
                            requested_native=dict(integral=effective, numerator=0, denominator=1, negative=False) if exact else None,
                            requested_native_approximate=None if exact else approximate,
                            effective_native=effective, endpoint_known=not relative, endpoint_native=0 if relative else effective,
                            displacement_known=relative, displacement_native=0, zero_displacement=relative,
                            rounding_error=0 if exact else -approximate,
                            approximation_error_bound=error, exact_arithmetic=exact))
                    return Serial.normal(request_id, command, args)
                console = self.session(handler)
                result = console.command("prepare", host_args=arguments)
                self.assertEqual(result["effective_native"], effective)
                self.assertEqual(result["exact_arithmetic"], exact)
                self.assertEqual(result["approximation_error_bound"], error)
                self.assertEqual(len(self.port.writes), 2)
                self.assertFalse(console.operations)
                self.assertTrue(console.synchronized)

    def test_exhausted_host_generation_is_queryable_but_never_prepared(self):
        def handler(request_id, command, args):
            if command == "axis":
                return encoded(reply(request_id, command, code="OK", detail=0, bus_traffic=False,
                    motion_command=False, wire_motion="unimplemented", configuration_generation=0,
                    target=1, address=1, binding_generation=1,
                    operator_scales=[dict(numerator=0, denominator=1, source=0) for _ in range(5)]))
            return Serial.normal(request_id, command, args)
        console = self.session(handler)
        result = console.command("axis", host_args=("config",))
        self.assertEqual(result["configuration_generation"], 0)
        self.assertTrue(console.synchronized)
        with self.assertRaisesRegex(bench.BenchError, "configuration generation"):
            bench.Console._check_axis(result, True)

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

    def test_typed_reads_fragmented_and_explicitly_released(self):
        for kind in ("identity", "config", "state"):
            with self.subTest(kind=kind):
                console = self.session(TypedSerial(), fragment=1)
                terminal = console.command("read-" + kind, address=1, timeout_s=0.1)
                self.assertEqual(terminal["state"], "succeeded")
                self.assertEqual(len(terminal["steps"]), len(bench.TYPED_WINDOWS[kind]))
                self.assertEqual(self.port.writes[-2:], [f"@2 read {kind} 1\n".encode(), b"@3 release 102\n"])
                self.assertFalse(console.operations)

    def test_health_check_alias_is_strictly_canonical_state(self):
        console = self.session(TypedSerial(), fragment=1)
        terminal = console.command("health-check", address=1, timeout_s=0.1)
        self.assertEqual(terminal["command"], "read-state")
        self.assertEqual(self.port.writes[-2:], [b"@2 health check 1\n", b"@3 release 102\n"])

    def test_state_decoder_rejects_fabricated_polarity_units_and_unknown_bits(self):
        mutations = (lambda r: r["state_blocks"][0].update(enabled=True),
                     lambda r: r["state_blocks"][0].update(unknown_motion_bits=0),
                     lambda r: r["state_blocks"][1].update(inputs=[False] * 4),
                     lambda r: r["state_blocks"][2].update(raw_encoder_counts=1),
                     lambda r: r["state_blocks"][2].update(raw_position=0x12345678),
                     lambda r: r["state_blocks"][2].update(speed_unit_resolution=0))
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                console = self.session(TypedSerial(mutate))
                self.failed(lambda: console.command("read-state", address=1, timeout_s=0.1), "typed-read state")
                self.assertEqual(self.port.writes[-1], b"@2 read state 1\n")

    def test_state_unknown_configuration_and_alarm_remain_raw(self):
        def mutate(r):
            first = r["steps"][0]
            data = bytes.fromhex("01030400048011")
            first["rx"] = (data + bench.wire_crc(data).to_bytes(2, "little")).hex()
            r["state_blocks"][0].update(raw_alarm=4, alarm_known=False)
            r["decode_config"].update(operation_id=0, word_order_known=False, word_order=0, algorithm_known=False, algorithm=1)
            for value in r["state_blocks"]:
                value["config_operation_id"] = 0
            r["state_blocks"][2].update(pair_known=False, raw_position=0, position_source=0, word_order_resolution=9, position_source_resolution=10)
        console = self.session(TypedSerial(mutate))
        result = console.command("read-state", address=1, timeout_s=0.1)
        self.assertTrue(result["ok"])
        self.assertEqual(result["state_blocks"][0]["raw_alarm"], 4)
        self.assertFalse(result["state_blocks"][2]["pair_known"])

    def test_state_partial_failure_preserves_only_successful_blocks(self):
        def mutate(r):
            r.update(ok=False, state="failed", outcome="transport_error", status="ILLEGAL_VALUE", detail=11, completed_steps=2)
            step = r["steps"][2]
            step.update(event=1, status="ILLEGAL_VALUE", detail=11, qualified=False, earliest_us=0, latest_us=0,
                        rx="", received_length=0, execution_unknown=True)
            r["state_blocks"] = r["state_blocks"][:2]
        console = self.session(TypedSerial(mutate))
        result = console.command("read-state", address=1, timeout_s=0.1)
        self.assertFalse(result["ok"])
        self.assertEqual(len(result["state_blocks"]), 2)

    def test_cached_state_separate_ages_unknown_alarms_and_generation(self):
        item = cached_state(2)
        bench.Console._check_cached_state(item)
        item["state_blocks"][0]["value"].update(raw_alarm=4, alarm_known=False)
        item["alarms"] = "present"
        bench.Console._check_cached_state(item)
        item["state_blocks"][0]["value"].update(raw_alarm=0xBEEF)
        item["alarms"] = "unknown"
        bench.Console._check_cached_state(item)
        item["state_blocks"][0]["value"].update(alarm_flag=True, raw_motion=0x8019)
        item["alarms"] = "present"
        bench.Console._check_cached_state(item)
        item.update(stale_after_ms=2, communication="stale", state="unknown", alarms="unknown")
        for block in item["state_blocks"]:
            block["fresh"] = block["age_us"] <= 2000
        bench.Console._check_cached_state(item)
        item["selected_generation"] = 10
        item["communication"] = "unknown"
        for block in item["state_blocks"]:
            block.update(current=False, fresh=False)
        item["state_blocks"][2].update(current_config_operation_id=0, interpretation_current=False)
        bench.Console._check_cached_state(item)

    def test_cached_state_rejects_rejuvenation_and_readiness_claims(self):
        mutations = (lambda r: r["state_blocks"][0].update(age_us=0),
                     lambda r: r["state_blocks"][1].update(last_success_us=5000),
                     lambda r: r["state_blocks"][2].update(generation=10),
                     lambda r: r.update(readiness="ready"),
                     lambda r: r.update(alarms="present"),
                     lambda r: r.update(communication_generation=10),
                     lambda r: r.update(atomic_snapshot=True))
        for mutate in mutations:
            item = cached_state(2); mutate(item)
            with self.assertRaises(bench.BenchError):
                bench.Console._check_cached_state(item)

    def test_cached_state_rejects_corrupt_raw_decode_schema(self):
        mutations = (
            lambda r: r["state_blocks"][0]["value"].update(enabled=True),
            lambda r: r["state_blocks"][0]["value"].update(unknown_motion_bits=0),
            lambda r: r["state_blocks"][0]["value"].update(block=False),
            lambda r: r["state_blocks"][0]["value"].update(raw_alarm=True),
            lambda r: r["state_blocks"][0]["value"].pop("alarm_flag"),
            lambda r: r["state_blocks"][1]["value"].update(inputs=[1, False, True, False]),
            lambda r: r["state_blocks"][1]["value"].update(unknown_input_bits=0),
            lambda r: r["state_blocks"][2]["value"].update(raw_encoder_counts=42),
            lambda r: r["state_blocks"][2]["value"].update(position_words=[False, 1]),
            lambda r: r["state_blocks"][2]["value"].update(raw_position=123),
            lambda r: r["state_blocks"][2]["value"].update(position_source=True),
            lambda r: r["state_blocks"][2]["value"].update(position_source_resolution=10),
            lambda r: r["state_blocks"][2]["value"].update(speed_unit_resolution=0),
            lambda r: r["state_blocks"][2]["value"].update(config_operation_id=False),
            lambda r: r["state_blocks"][2].update(current_config_operation_id=True),
            lambda r: r["state_blocks"][2].update(interpretation_current=False))
        for command in ("status", "health"):
            for mutate in mutations:
                with self.subTest(command=command, mutate=mutate):
                    item = cached_state(2, command); mutate(item)
                    with self.assertRaises(bench.BenchError):
                        bench.Console._check_cached_state(item)

    def test_corrupt_cached_state_stops_session_without_replay(self):
        def handler(request_id, command, args):
            if command == "status":
                item = cached_state(request_id, command)
                item["state_blocks"][0]["value"].pop("alarm_flag")
                return encoded(item)
            return Serial.normal(request_id, command, args)
        console = self.session(handler)
        self.failed(lambda: console.command("status", timeout_s=0.1), "decoded field differs")
        self.assertEqual(self.port.writes, [b"@1 version\n", b"@2 status\n"])

    def test_cached_feedback_retains_old_configuration_without_promoting_interpretation(self):
        item = cached_state(2)
        item["state_blocks"][2].update(current_config_operation_id=91, interpretation_current=False)
        bench.Console._check_cached_state(item)
        item["state_blocks"][2]["interpretation_current"] = True
        with self.assertRaises(bench.BenchError):
            bench.Console._check_cached_state(item)

    def test_monitor_arguments_are_bounded_before_any_send(self):
        console = self.session()
        for request in (True, (99, 1), (100, 0), (100, 1001), (60001, 1), (True, 1), [100, 1]):
            with self.subTest(request=request), self.assertRaises(ValueError):
                console.begin("monitor", monitor=request)
        self.assertEqual(len(self.port.writes), 1)

    def test_stationary_state_health_campaign_uses_explicit_reads_and_retention(self):
        typed = TypedSerial()
        latest = None
        def handler(request_id, command, args):
            nonlocal latest
            if command == "health" and args and args[0] == "check":
                latest = typed_terminal(request_id, "state")
            if command == "monitor":
                return encoded(reply(request_id, command, enabled=False, interval_ms=0, count=0, remaining=0,
                    operation_id=0, next_due_us=0, admitted=0, rejected=0, cancelled=0))
            if command in ("status", "health") and not args:
                cached = cached_state(request_id, command)
                if latest:
                    for block, step in zip(cached["state_blocks"], latest["steps"]):
                        block["operation_id"] = block["last_attempt_operation_id"] = latest["operation_id"]
                        block["observed_earliest_us"] = step["attempted_us"]
                        block["age_us"] = cached["now_us"] - step["attempted_us"]
                return encoded(cached)
            return typed(request_id, command, args)
        console = self.session(handler)
        bench.campaign(console, "state-health", count=2, interval_s=0.01, timeout_s=0.1, address=1)
        commands = [entry.decode().split()[1:] for entry in self.port.writes]
        self.assertEqual(commands.count(["health", "check", "1"]), 2)
        self.assertEqual(commands.count(["read", "config", "1"]), 1)
        self.assertEqual(len([command for command in commands if command[0] == "release"]), 3)
        self.assertFalse(typed.retained)

    def test_state_health_campaign_rejects_missing_old_or_corrupted_cache(self):
        for fault in ("missing", "old", "corrupt"):
            with self.subTest(fault=fault):
                typed = TypedSerial()
                latest = None
                def handler(request_id, command, args):
                    nonlocal latest
                    if command == "health" and args and args[0] == "check":
                        latest = typed_terminal(request_id, "state")
                    if command == "monitor":
                        return encoded(reply(request_id, command, enabled=False, interval_ms=0, count=0, remaining=0,
                            operation_id=0, next_due_us=0, admitted=0, rejected=0, cancelled=0))
                    if command in ("status", "health") and not args:
                        cached = cached_state(request_id, command)
                        if latest:
                            if fault == "missing":
                                return encoded(reply(request_id, command, uptime_ms=5, readiness="unknown"))
                            for block, step in zip(cached["state_blocks"], latest["steps"]):
                                block["operation_id"] = latest["operation_id"] - (fault == "old")
                                block["observed_earliest_us"] = step["attempted_us"]
                                block["age_us"] = cached["now_us"] - step["attempted_us"]
                            if fault == "corrupt":
                                cached["state_blocks"][2]["value"]["raw_speed"] ^= 1
                        return encoded(cached)
                    return typed(request_id, command, args)
                console = self.session(handler)
                with self.assertRaisesRegex(bench.BenchError, "does not match completed refresh"):
                    bench.campaign(console, "state-health", count=1, interval_s=0, timeout_s=0.1, address=1)
                self.assertEqual(len([entry for entry in self.port.writes if b"health check" in entry]), 1)

    def test_typed_invalid_evidence_never_releases_or_replays(self):
        mutations = (
            lambda r: r.update(read_kind="identity"),
            lambda r: r.update(generation=True),
            lambda r: r.update(target=0),
            lambda r: r.update(address=2),
            lambda r: r.update(completed_steps=4),
            lambda r: r.update(state="active"),
            lambda r: r["config"]["raw"].update(subdivision=1000),
            lambda r: r["config"]["known"].update(direction=False),
            lambda r: r["config"]["inputs"][0].update(level_known=True, level=False),
            lambda r: r["config"].update(encoder_scale_source=4),
            lambda r: r["active_serial"].update(baud=True),
            lambda r: r["steps"][1].update(first=0x17),
            lambda r: r["steps"][1].update(step=0),
            lambda r: r["steps"][1].update(earliest_us=2000),
            lambda r: r["steps"][0].update(tx="010300120002640E"),
            lambda r: r["steps"][0].update(rx=r["steps"][0]["rx"][:-2] + "00"),
            lambda r: r["steps"][0].update(received_length=38),
            lambda r: r["steps"][0].update(tx_accepted=9),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                console = self.session(TypedSerial(mutate))
                self.failed(lambda: console.command("read-config", address=1, timeout_s=0.1), "typed-read")
                self.assertEqual(len(self.port.writes), 2)

    def test_typed_unknown_codes_and_zero_encoder_remain_successful_raw_reads(self):
        def future_codes(r):
            first = bytearray.fromhex(r["steps"][0]["rx"])
            first[3:5] = b"\xFF\xFF"
            first[-2:] = bench.wire_crc(first[:-2]).to_bytes(2, "little")
            r["steps"][0]["rx"] = first.hex()
            r["config"]["raw"]["direction"] = 65535
            r["config"]["known"]["direction"] = False
            encoder = bytearray.fromhex(r["steps"][4]["rx"])
            encoder[5:7] = b"\x00\x00"
            encoder[-2:] = bench.wire_crc(encoder[:-2]).to_bytes(2, "little")
            r["steps"][4]["rx"] = encoder.hex()
            r["config"]["raw"]["encoder_resolution"] = 0
            r["config"].update(encoder_resolution=6, encoder_scale_source=0)

        console = self.session(TypedSerial(future_codes), fragment=7)
        result = console.command("read-config", timeout_s=0.1)
        self.assertTrue(result["ok"])
        self.assertEqual(result["config"]["raw"]["direction"], 65535)
        self.assertEqual(result["config"]["raw"]["encoder_resolution"], 0)

    def test_typed_failed_partial_reply_is_retained_without_decoded_publication(self):
        def crc_failure(r):
            r.update(ok=False, state="failed", outcome="reply_error", status="CRC_ERROR", detail=6,
                     completed_steps=1, config=None)
            r["steps"] = r["steps"][:2]
            r["steps"][1].update(status="CRC_ERROR", detail=6, frame_error=6,
                                 rx=r["steps"][1]["rx"][:-2] + "00")

        console = self.session(TypedSerial(crc_failure), fragment=3)
        handle = console.begin("read-config", timeout_s=0.1)
        terminal = console.wait(handle)
        self.assertFalse(terminal["ok"])
        inspected = console.command("result", operation_id=102, timeout_s=0.1)
        self.assertEqual(inspected["steps"], terminal["steps"])
        self.assertIsNone(inspected["config"])
        console.command("release", operation_id=102, timeout_s=0.1)
        self.assertEqual(sum(line.split()[1] == b"read" for line in self.port.writes), 1)

    def test_typed_failed_control_events_preserve_partial_capture_and_uncertainty(self):
        for event, outcome in ((1, "transport_error"), (2, "cancelled"), (3, "deadline")):
            with self.subTest(event=event):
                def failed(r):
                    r.update(ok=False, state="failed", outcome=outcome, status="ILLEGAL_VALUE", detail=12,
                             completed_steps=0, config=None, serviced_us=25000)
                    r["steps"] = r["steps"][:1]
                    r["steps"][0].update(event=event, status="ILLEGAL_VALUE", detail=12,
                                         qualified=False, earliest_us=0, latest_us=0, delivered_us=25000,
                                         rx="010304", received_length=3, execution_unknown=True,
                                         transport_detail=17)

                console = self.session(TypedSerial(failed))
                terminal = console.command("read-config", timeout_s=0.1)
                self.assertFalse(terminal["ok"])
                self.assertEqual(terminal["steps"][0]["rx"], "010304")
                self.assertTrue(terminal["steps"][0]["execution_unknown"])

    def test_typed_final_on_time_closure_can_be_delivered_after_fixed_deadline(self):
        def delayed(r):
            r["serviced_us"] = r["deadline_us"] + 5000
            r["steps"][-1]["delivered_us"] = r["serviced_us"]

        for kind in ("identity", "config"):
            with self.subTest(kind=kind):
                console = self.session(TypedSerial(delayed))
                terminal = console.command("read-" + kind, timeout_s=0.1)
                self.assertGreater(terminal["serviced_us"], terminal["deadline_us"])
                self.assertTrue(terminal["ok"])

    def test_typed_failure_outcome_must_match_retained_step(self):
        def inconsistent(r):
            r.update(ok=False, state="failed", outcome="cancelled", status="ILLEGAL_VALUE", detail=12,
                     completed_steps=0, config=None)
            r["steps"] = r["steps"][:1]
            r["steps"][0].update(status="CRC_ERROR", detail=6, frame_error=6,
                                 rx=r["steps"][0]["rx"][:-2] + "00")

        console = self.session(TypedSerial(inconsistent))
        self.failed(lambda: console.command("read-config", timeout_s=0.1), "failure outcome")
        self.assertEqual(len(self.port.writes), 2)

    def test_typed_campaign_arguments_select_explicit_kind_without_port_access(self):
        parsed = bench.arguments(["--port", "FAKE", "--log", "unused.jsonl", "typed-read", "--kind", "config"])
        self.assertEqual((parsed.mode, parsed.kind, parsed.count, parsed.interval), ("typed-read", "config", 1, 0))

    def test_typed_retained_result_rejects_changed_target_and_pending_kind(self):
        handler = TypedSerial()
        console = self.session(handler)
        handle = console.begin("read-identity", timeout_s=0.1)
        terminal = console.wait(handle)
        handler.retained[102] = {**terminal, "generation": 10}
        self.failed(lambda: console.command("result", operation_id=102, timeout_s=0.1), "immutable")
        for read_kind in (None, "config", [], True):
            with self.subTest(read_kind=read_kind):
                handler = TypedSerial(admission_only=True)
                console = self.session(handler)
                console.begin("read-identity", timeout_s=0.1)
                self.port.handler = lambda i, command, args: encoded(reply(
                    i, command, result="pending", operation_id=102, command_id=2,
                    recovery=False, capture_read=False, read_kind=read_kind))
                self.failed(lambda: console.command("result", operation_id=102, timeout_s=0.1), "kind")

    def test_typed_campaign_reads_inspects_releases_once_per_kind(self):
        console = self.session(TypedSerial(), fragment=9)
        bench.campaign(console, "typed-read", count=1, interval_s=0, timeout_s=0.1, address=1)
        bus_commands = [line for line in self.port.writes if line.split()[1] == b"read"]
        self.assertEqual(len(bus_commands), 2)
        self.assertIn(b"read identity 1", bus_commands[0])
        self.assertIn(b"read config 1", bus_commands[1])
        for name in (b"result", b"release"):
            self.assertEqual(sum(line.split()[1] == name for line in self.port.writes), 2)
        self.assertFalse(console.operations)
        summary = self.events[-1]
        self.assertTrue(summary["ok"])
        self.assertEqual(summary["reads_passed"], 2)

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
                       {"type": "probe"}, {"read_kind": "config"}):
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
                       {"timing_valid": False}, {"raw_truncated": True}, {"read_kind": "identity"}):
            with self.subTest(change=change):
                console = self.session()

                def malformed(i, cmd, args):
                    accepted, terminal = (json.loads(line) for line in Serial.normal(i, cmd, args).splitlines())
                    return encoded(accepted) + encoded({**terminal, **change})

                self.port.handler = malformed
                self.failed(lambda: console.command("probe", timeout_s=0.1), "result evidence|result kind")

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

    def test_load_requires_complete_capture_diagnostics(self):
        for field in ("timer_callbacks", "sample_gap_limit_us", "capture_high_water", "sample_gap_exceeded"):
            invalid = (None, -1, True, 1.5, "1", 1 << 64) if field != "sample_gap_exceeded" else (None, 0, 1, "false")
            for value in invalid:
                with self.subTest(field=field, value=value):
                    console = self.load_session()
                    normal = self.port.handler

                    def malformed(i, command, args):
                        response = normal(i, command, args)
                        if command == "load":
                            item = json.loads(response)
                            if value is None:
                                del item[field]
                            else:
                                item[field] = value
                            response = encoded(item)
                        return response

                    self.port.handler = malformed
                    self.failed(lambda: console.command("load", timeout_s=0.1), field)

    def test_load_capture_diagnostics_require_consistent_counts_and_mode(self):
        for changed, error in (({"timer_callbacks": 201}, "callbacks exceed"),
                               ({"sample_gap_limit_us": 0}, "limit does not match"),
                               ({"capture_mode": "poll"}, "limit does not match")):
            with self.subTest(changed=changed):
                response = {**load_reply(1), **changed}
                with self.assertRaisesRegex(bench.BenchError, error):
                    bench.check_load_reply(response, (0, 0, 0))
        # Stopping timer capture need not erase its historical callback count.
        bench.check_load_reply({**load_reply(1), "capture_mode": "poll", "sample_gap_limit_us": 0}, None)

    def test_load_capture_gap_stops_campaign_but_keeps_diagnostics_inspectable(self):
        for read_command in ("probe", "capture-read"):
            for failed_snapshot in (1, 2):
                with self.subTest(read_command=read_command, failed_snapshot=failed_snapshot):
                    console = self.load_session()
                    normal = self.port.handler
                    snapshots = 0

                    def faulted(i, command, args):
                        nonlocal snapshots
                        response = normal(i, command, args)
                        if command == "load":
                            snapshots += 1
                            response = encoded({**json.loads(response),
                                                "sample_gap_exceeded": snapshots >= failed_snapshot})
                        return response

                    self.port.handler = faulted
                    with self.assertRaisesRegex(bench.BenchError, "capture sample gap exceeded"):
                        bench.campaign(console, "load", count=3, interval_s=0, timeout_s=0.1,
                                       load=(2000, 5000, 128), read_command=read_command)
                    summary = self.events[-1]
                    self.assertFalse(summary["ok"])
                    self.assertTrue(summary["last_load"]["sample_gap_exceeded"])
                    self.assertEqual(summary["probes_attempted"], failed_snapshot - 1)
                    commands = [line.split()[1] for line in self.port.writes]
                    self.assertEqual(commands.count(read_command.encode()), failed_snapshot - 1)
                    self.assertEqual(commands.count(b"load"), failed_snapshot)
                    self.assertNotIn(b"recover", commands)
                    self.assertNotIn(b"reset", commands)
                    self.assertTrue(console.synchronized)
                    self.assertTrue(console.command("load", timeout_s=0.1)["sample_gap_exceeded"])
                    self.assertTrue(console.command("stats", timeout_s=0.1)["ok"])
                    # Recovery remains a separate explicit operator action.
                    self.assertTrue(console.command("recover", timeout_s=0.1)["ok"])

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
                       {"finished_us": 50000}, {"transport": None}, {"transport": "FRAME"}, {"read_kind": "config"}):
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
