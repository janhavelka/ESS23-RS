"""Exercise host framing and finite campaigns using a fake serial stream only."""

import copy
import importlib.util
import io
import json
import math
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
            observed_earliest_us=0, observed_latest_us=0, delivered_us=0, invalidated_us=0, age_us=None, value=None) for i in range(3)])
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


def action_terminal(request_id, command, policy=None):
    tx = {"enable": "0106002D001299CE", "motor-release": "0106002D0011D9CF",
          "alarm-clear": "0106002D0021D9DB", "position-clear": "0106002D0031D817", "stop": "0106002701003851" if policy == "normal" else "01060027020038A1"}[command]
    if command == "position-clear":
        prefix = bytes((1, 6, 0, 0x2D, 0, 0x31))
        tx = (prefix + bench.wire_crc(prefix).to_bytes(2, "little")).hex()
    motion = 17 if command == "motor-release" else 1
    raw = bytes((1, 3, 4, 0, 0, 0, 0 if command == "position-clear" else motion))
    rx = (raw + bench.wire_crc(raw).to_bytes(2, "little")).hex()
    empty = dict(step=0, event=0, raw_hex="", received_length=0, tx_accepted=0, tx_complete=False,
        response_confirmed=False, qualified=False, execution_unknown=False, earliest_us=0,
        latest_us=0, delivered_us=0, transport_detail=0, status="OK", detail=0, frame_error=0)
    write = dict(empty, raw_hex=tx, received_length=8, tx_accepted=8, tx_complete=True,
        response_confirmed=True, qualified=True, earliest_us=1100, latest_us=1200, delivered_us=1250)
    observation = dict(write, step=1, raw_hex=rx, received_length=9, earliest_us=2000, latest_us=2200, delivered_us=2300)
    result = reply(request_id, command, type="action", command_id=request_id, operation_id=request_id + 100,
        action_kind=bench.ACTION_KINDS[command], stop_policy=policy, read_kind=None, capture_read=False,
        recovery=False, target=1, address=1, generation=9, state="succeeded", outcome="observed",
        execution="acknowledged", completion="observed", observation_known=True, interrupted_by_stop=False, raw_alarm=0,
        raw_motion=motion, started_us=1000, deadline_us=10000, serviced_us=2300, polls=1,
        write_evidence=write, last_observation=observation, failure_evidence=empty)
    if command == "position-clear":
        result.update(device_position=0, position_clear_qualified=True, raw_position=0, raw_motion=None, raw_alarm=None)
    return result


class ActionSerial:
    def __init__(self, mutate=None):
        self.retained = {}
        self.mutate = mutate

    def __call__(self, request_id, command, args):
        if command in bench.ACTION_COMMANDS:
            terminal = action_terminal(request_id, command, args[0] if command == "stop" else None)
            if self.mutate: self.mutate(terminal)
            self.retained[request_id + 100] = terminal
            return encoded(reply(request_id, command, result="accepted", operation_id=request_id + 100, address=1)) + encoded(terminal)
        if command == "result":
            return encoded(dict(self.retained[int(args[0])], type="reply", id=request_id, command="result"))
        if command == "release": self.retained.pop(int(args[0]), None)
        return Serial.normal(request_id, command, args)


def move_terminal(request_id):
    empty = dict(step=0, event=0, raw_hex="", received_length=0, tx_accepted=0, tx_complete=False,
        response_confirmed=False, qualified=False, execution_unknown=False, earliest_us=0,
        latest_us=0, delivered_us=0, transport_detail=0, status="OK", detail=0, frame_error=0)
    def frame(step, prefix, tx_size, first, last, delivered):
        raw = bytes(prefix)
        raw += bench.wire_crc(raw).to_bytes(2, "little")
        return dict(empty, step=step, raw_hex=raw.hex(), received_length=len(raw), tx_accepted=tx_size,
            tx_complete=True, response_confirmed=True, qualified=True, earliest_us=first,
            latest_us=last, delivered_us=delivered)
    return reply(request_id, "move-relative", type="move", command_id=request_id, operation_id=request_id + 100,
        move_kind="relative", action_kind=None, read_kind=None, capture_read=False, recovery=False,
        target=1, address=1, generation=9, configuration_generation=3, state="succeeded", outcome="observed",
        status="OK", detail=0, setup_execution="acknowledged", execution="acknowledged", completion="observed",
        staging_applied=True, uncertain=False, running_observed=True, observation_known=True, interrupted_by_stop=False,
        raw_alarm=0, raw_motion=1, started_us=1000, deadline_us=10000, serviced_us=4300, polls=2,
        native_rpm=60, ramp="configured", staging_words=[100, 100, 60, 0, 1000],
        requested=dict(numerator=1000, denominator=1, unit="steps", frame=0, relative=True, wrapped=False, angle_path=2, half_turn_tie=0, basis=0, rounding=0,
            approximate=False, rational_radians=True, radians=None, maximum_quantization_error=0, maximum_approximation_error=None),
        effective_native=1000, displacement_native=1000, endpoint_known=False, endpoint_native=0,
        zero_displacement=False, exact_arithmetic=True, rounding_error=0, approximation_error_bound=0, requested_native_approximate=None,
        prerequisites=dict(target=1, generation=9, configuration_generation=3, observed_us=900, maximum_age_us=10000,
            raw_alarm=0, raw_motion=1, command_units_verified=True, relative_basis_verified=True,
            negative_encoding_verified=False, configured_ramp_verified=True, serial_inputs_permit=True,
            readiness_qualified=True, word_order_known=True, word_order=0, start_speed_known=True, start_speed=10),
        reference=dict(target=0, generation=0, configuration_generation=0, native_known=False, native_position=0, basis=0, source=0, observed_us=0, maximum_age_us=0),
        staging_evidence=frame(0, [1, 16, 0, 0x21, 0, 5], 19, 1100, 1200, 1250),
        trigger_evidence=frame(1, [1, 6, 0, 0x27, 0, 1], 8, 2000, 2100, 2150),
        activity_evidence=frame(2, [1, 3, 4, 0, 0, 0, 4], 8, 3000, 3200, 3300),
        last_observation=frame(3, [1, 3, 4, 0, 0, 0, 1], 8, 4000, 4200, 4300), failure_evidence=empty)


class MoveSerial:
    def __init__(self, mutate=None, admission_only=False):
        self.retained = {}
        self.mutate = mutate
        self.admission_only = admission_only

    def __call__(self, request_id, command, args):
        if command == "move":
            terminal = move_terminal(request_id)
            if self.mutate: self.mutate(terminal)
            self.retained[request_id + 100] = terminal
            accepted = reply(request_id, "move-relative", result="accepted", operation_id=request_id + 100, address=1)
            return encoded(accepted) + (b"" if self.admission_only else encoded(terminal))
        if command in bench.ACTION_COMMANDS:
            terminal = action_terminal(request_id, command, args[0] if command == "stop" else None)
            self.retained[request_id + 100] = terminal
            return encoded(reply(request_id, command, result="accepted", operation_id=request_id + 100, address=1)) + encoded(terminal)
        if command == "read":
            terminal = typed_terminal(request_id, args[0])
            self.retained[request_id + 100] = terminal
            return encoded(reply(request_id, "read-" + args[0], result="accepted", operation_id=request_id + 100, address=1, read_kind=args[0])) + encoded(terminal)
        if command == "result":
            return encoded(dict(self.retained[int(args[0])], type="reply", id=request_id, command="result"))
        if command == "release": self.retained.pop(int(args[0]), None)
        return Serial.normal(request_id, command, args)


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, delay):
        self.now += delay


def velocity_terminal(request_id):
    original = move_terminal(request_id)
    empty = original["failure_evidence"]
    def frame(step, prefix, tx_size, timestamp):
        raw = bytes(prefix); raw += bench.wire_crc(raw).to_bytes(2, "little")
        return dict(empty, step=step, raw_hex=raw.hex(), received_length=len(raw), tx_accepted=tx_size,
                    tx_complete=True, response_confirmed=True, qualified=True,
                    earliest_us=timestamp, latest_us=timestamp + 50, delivered_us=timestamp + 100)
    return reply(request_id, "velocity", type="velocity", command_id=request_id, operation_id=request_id + 100,
        velocity=True, target=1, address=1, generation=9, configuration_generation=3, state="succeeded", outcome="observed",
        status="OK", detail=0, setup_execution="acknowledged", execution="acknowledged", completion="observed",
        staging_applied=True, uncertain=False, needs_stop=False, running_observed=True, service_missed=False,
        observation_known=True, interrupted_by_stop=False, raw_alarm=0, raw_motion=4,
        started_us=1000, deadline_us=1000000, stop_due_us=501000, serviced_us=503100, polls=1, phase=3,
        native_rpm=60, ramp="configured", staging_words=[60, 100, 100], stop_policy="normal",
        requested=dict(numerator=60, denominator=1, position_unit=3, time_unit=1, frame=0, duration_us=500000, rounding=0,
            approximate=False, maximum_quantization_error_rpm=0, maximum_approximation_error_rpm=0),
        exact_arithmetic=True, rounding_error=0, approximation_error_bound=0, requested_rpm_approximate=0,
        stop_execution="acknowledged", stop_completion="observed", stop_outcome="observed",
        staging_evidence=frame(0, [1, 16, 0, 0x1D, 0, 3], 15, 1100),
        trigger_evidence=frame(1, [1, 6, 0, 0x27, 0, 2], 8, 2000),
        activity_evidence=frame(2, [1, 3, 4, 0, 0, 0, 4], 8, 3000), failure_evidence=dict(empty),
        last_observation={key: value for key, value in frame(2, [1, 3, 4, 0, 0, 0, 4], 8, 3000).items()
                          if key in ("step", "raw_hex", "earliest_us", "latest_us", "delivered_us")},
        stop_write_evidence=frame(0, [1, 6, 0, 0x27, 1, 0], 8, 501000),
        stop_observation=frame(1, [1, 3, 4, 0, 0, 0, 1], 8, 503000), stop_failure_evidence=dict(empty))


class VelocitySerial(MoveSerial):
    def __call__(self, request_id, command, args):
        if command == "velocity":
            terminal = velocity_terminal(request_id)
            if self.mutate: self.mutate(terminal)
            self.retained[request_id + 100] = terminal
            accepted = reply(request_id, command, result="accepted", operation_id=request_id + 100, address=1)
            return encoded(accepted) + (b"" if self.admission_only else encoded(terminal))
        return super().__call__(request_id, command, args)


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
                                 protocol=2, version="0.test", outstanding_capacity=10))
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
    MOVE_ARGS = ("1000", "steps", "native", "60", "configured")
    VELOCITY_ARGS = ("60", "rpm", "native", "500", "configured", "normal")

    def test_velocity_strict_correlation_retention_and_single_cleanup(self):
        console = self.session(VelocitySerial(), fragment=19)
        events = []; console.emit = lambda event, **data: events.append((event, data))
        bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS, cleanup_stop="normal", timeout_s=3, address=1)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("velocity"), 1); self.assertEqual(commands.count("stop"), 1)
        self.assertNotIn("recover", commands); self.assertFalse(console.operations)
        self.assertEqual(events[-1][1]["cleanup"], "drive_reported_nonrunning")
        self.assertEqual(events[-1][1]["physical_observation"], "not_supplied")
        self.assertEqual(events[-1][1]["velocities_attempted"], 1)

    def test_velocity_rejection_and_poison_never_replay(self):
        def rejected(i, command, args):
            if command == "velocity": return encoded(reply(i, command, ok=False, result="timing_unqualified"))
            return Serial.normal(i, command, args)
        console = self.session(rejected)
        with self.assertRaises(bench.BenchError):
            bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS, cleanup_stop="direct", timeout_s=3, address=1)
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "velocity"])
        console = self.session(VelocitySerial(lambda item: item.update(command_id=99)))
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaises(bench.BenchError):
            bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS, cleanup_stop="normal", timeout_s=3, address=1)
        self.assertFalse(console.synchronized)
        self.assertEqual(events[-1][1]["cleanup"], "unknown")
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "velocity"])

    def test_velocity_bad_evidence_has_no_success_shape(self):
        for mutate in (lambda t: t.update(native_rpm=0), lambda t: t.update(stop_due_us=500999),
                lambda t: t.update(running_observed=False), lambda t: t.update(needs_stop=True),
                lambda t: t.update(service_missed=True), lambda t: t.update(stop_completion="not_observed"),
                lambda t: t["trigger_evidence"].update(raw_hex="010600270001F806"),
                lambda t: t["activity_evidence"].update(earliest_us=1000),
                lambda t: t["stop_write_evidence"].update(response_confirmed=False),
                lambda t: t["stop_observation"].update(raw_hex=t["activity_evidence"]["raw_hex"]),
                lambda t: t.update(ok=False, state="failed"), lambda t: t.update(stop_outcome="transport_error"),
                lambda t: t.update(raw_motion=65535), lambda t: t.update(raw_alarm=65535),
                lambda t: t.pop("last_observation"), lambda t: t["last_observation"].update(raw_hex=""),
                lambda t: t["last_observation"].update(latest_us=501001),
                lambda t: t["last_observation"].update(step=1),
                lambda t: t["last_observation"].update(earliest_us=1000, latest_us=1000),
                lambda t: t["last_observation"].update(earliest_us=2100, latest_us=2200, delivered_us=2300)):
            terminal = velocity_terminal(2); mutate(terminal)
            with self.assertRaises(bench.BenchError): bench.Console._check_velocity(terminal, 1, self.VELOCITY_ARGS)

    def test_velocity_failed_start_preserves_uncertainty_after_checked_stop(self):
        t = velocity_terminal(2)
        t.update(ok=False, state="failed", outcome="transport_error", status="FRAME_ERROR", detail=19, execution="unknown", uncertain=True)
        t.update(running_observed=False, observation_known=False, raw_alarm=None, raw_motion=None)
        t["activity_evidence"] = dict(t["stop_failure_evidence"])
        t["last_observation"] = {key: t["stop_failure_evidence"][key] for key in ("step", "raw_hex", "earliest_us", "latest_us", "delivered_us")}
        e = t["trigger_evidence"]
        e.update(event=1, raw_hex="", received_length=0, response_confirmed=False, qualified=False,
                 execution_unknown=True, earliest_us=0, latest_us=0, status="FRAME_ERROR", detail=19)
        t["failure_evidence"] = dict(e)
        bench.Console._check_velocity(t, 1, self.VELOCITY_ARGS)
        self.assertFalse(t["needs_stop"]); self.assertTrue(t["uncertain"])

    def test_velocity_parser_is_exact_bounded_and_requires_cleanup_policy(self):
        for args in (("nan", *self.VELOCITY_ARGS[1:]), ("1/0", *self.VELOCITY_ARGS[1:]),
                (*self.VELOCITY_ARGS[:3], "0", *self.VELOCITY_ARGS[4:]),
                (*self.VELOCITY_ARGS[:3], "1001", *self.VELOCITY_ARGS[4:]),
                (*self.VELOCITY_ARGS, "round", "nearest", "-1"),
                (*self.VELOCITY_ARGS, "round", "nearest", "1", "approx", "1")):
            with self.assertRaises(ValueError): bench.velocity_arguments(args)
        with self.assertRaises(ValueError):
            bench.velocity_campaign(self.session(VelocitySerial()), velocity_args=self.VELOCITY_ARGS,
                                    cleanup_stop="normal", timeout_s=0.5, address=1)
        parsed = bench.arguments(["--port", "FAKE", "--log", "unused", "velocity", "60", "rpm", "native", "500", "configured", "normal", "--cleanup-stop", "direct"])
        self.assertEqual(parsed.velocity_args, self.VELOCITY_ARGS); self.assertEqual(parsed.cleanup_stop, "direct")

    def test_velocity_late_qualified_failure_is_retained_not_mistaken_for_success(self):
        t = velocity_terminal(2)
        t.update(ok=False, state="failed", outcome="deadline", status="ILLEGAL_VALUE", detail=19,
                 execution="unknown", uncertain=True, running_observed=False, observation_known=False,
                 raw_alarm=None, raw_motion=None, serviced_us=1000100, stop_execution="not_transmitted",
                 stop_completion="not_observed", stop_outcome="deadline", needs_stop=True, completion="not_observed")
        empty = dict(t["stop_failure_evidence"])
        t["activity_evidence"] = dict(empty); t["stop_write_evidence"] = dict(empty); t["stop_observation"] = dict(empty)
        t["last_observation"] = {key: empty[key] for key in ("step", "raw_hex", "earliest_us", "latest_us", "delivered_us")}
        t["trigger_evidence"].update(earliest_us=501001, latest_us=501050, delivered_us=501100,
                                     status="ILLEGAL_VALUE", detail=19)
        t["failure_evidence"] = dict(t["trigger_evidence"])
        t["stop_failure_evidence"].update(event=3, delivered_us=1000100, status="ILLEGAL_VALUE", detail=19)
        bench.Console._check_velocity(t, 1, self.VELOCITY_ARGS)

    def test_velocity_host_timeout_is_finite_and_retains_unknown_stop(self):
        console = self.session(VelocitySerial(admission_only=True))
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaises(bench.BenchError):
            bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS, cleanup_stop="normal", timeout_s=1, address=1)
        self.assertLess(self.clock.now, 1.1)
        self.assertFalse(console.synchronized); self.assertEqual(events[-1][1]["cleanup"], "unknown")
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "velocity"])

    def test_velocity_late_start_ack_is_known_execution_with_failed_duration(self):
        t = velocity_terminal(2)
        t.update(ok=False, state="failed", outcome="deadline", status="ILLEGAL_VALUE", detail=19,
                 uncertain=True, running_observed=False, observation_known=False, raw_alarm=None, raw_motion=None)
        t["trigger_evidence"].update(earliest_us=501001, latest_us=501050, delivered_us=501100)
        t["failure_evidence"] = dict(t["trigger_evidence"])
        t["activity_evidence"] = dict(t["stop_failure_evidence"])
        t["last_observation"] = {key: t["stop_failure_evidence"][key] for key in ("step", "raw_hex", "earliest_us", "latest_us", "delivered_us")}
        t["stop_write_evidence"].update(earliest_us=501200, latest_us=501250, delivered_us=501300)
        bench.Console._check_velocity(t, 1, self.VELOCITY_ARGS)

    def test_absolute_and_angle_preserve_coordinates_and_start_flags(self):
        for kind in ("absolute", "angle"):
            def converted(t):
                t.update(command="move-" + kind, move_kind=kind, endpoint_known=True,
                         endpoint_native=1000, displacement_native=250)
                t["requested"].update(relative=False, wrapped=kind == "angle")
                if kind == "angle": t["requested"].update(unit="deg", frame=1)
                t["reference"].update(target=1, generation=9, configuration_generation=3,
                    native_known=True, native_position=750, source=3, observed_us=900, maximum_age_us=10000)
                raw = bytes((1, 6, 0, 0x27, 0, 5))
                t["trigger_evidence"]["raw_hex"] = (raw + bench.wire_crc(raw).to_bytes(2, "little")).hex()
            arguments = self.MOVE_ARGS if kind == "absolute" else ("1000", "deg", "motor", "shortest", "reject", "60", "configured")
            responder = MoveSerial(converted)
            def handler(i, command, args):
                raw = responder(i, command, args)
                if command == "move":
                    raw = raw.replace(b"move-relative", ("move-" + kind).encode())
                return raw
            console = self.session(handler)
            result = console.command("move-" + kind, address=1, move_args=arguments)
            self.assertEqual(result["effective_native"], 1000)
            self.assertEqual(result["displacement_native"], 250)
            self.assertIn(("move " + kind).encode(), self.port.writes[1])
            for mutate in (lambda t: t["reference"].update(configuration_generation=4),
                           lambda t: t["reference"].update(observed_us=1001),
                           lambda t: t["reference"].update(native_known=False),
                           lambda t: t.update(endpoint_native=1001),
                           lambda t: t["requested"].update(wrapped=kind != "angle")):
                bad = copy.deepcopy(result); mutate(bad)
                with self.assertRaises(bench.BenchError): bench.Console._check_move(bad, 1, arguments, kind)
            wrong = copy.deepcopy(result)
            wrong["trigger_evidence"]["raw_hex"] = move_terminal(2)["trigger_evidence"]["raw_hex"]
            with self.assertRaises(bench.BenchError): bench.Console._check_move(wrong, 1, arguments, kind)

    def test_rounded_radian_move_keeps_public_api_provenance(self):
        item = move_terminal(2)
        item["requested"].update(numerator=1, unit="rad", frame=1, rounding=1, approximate=True,
            maximum_quantization_error=0.5, maximum_approximation_error=math.nextafter(1e-9, 0))
        item.update(exact_arithmetic=False, requested_native_approximate=1000.125,
                    rounding_error=-0.125, approximation_error_bound=1e-11)
        arguments = ("1", "rad", "motor", "60", "configured", "round", "nearest", "0.5", "approx", "0.000000001")
        bench.Console._check_move(item, 1, arguments)
        with self.assertRaises(bench.BenchError):
            bench.Console._check_move(item, 1, arguments[:-1] + ("0.000000002",))
        for malformed in (("1", "deg", "motor", "60", "configured", "round", "nearest", "1", "approx", "1"),
                          ("1", "turn", "motor", "shortest", "wrong", "60", "configured")):
            with self.assertRaises(ValueError): bench.move_arguments("angle" if "shortest" in malformed else "relative", malformed)

    def test_clear_position_is_separate_zero_only_observation(self):
        console = self.session(ActionSerial())
        result = console.command("position-clear", address=1)
        self.assertEqual(result["raw_position"], 0)
        self.assertIsNone(result["raw_motion"])
        self.assertEqual(self.port.writes[1], b"@2 position-clear 1\n")
        # Original function manual p70: auxiliary action 49 (0x31), not 0x22.
        self.assertEqual(bytes.fromhex(result["write_evidence"]["raw_hex"]), bytes.fromhex("0106002D0031D817"))
        for mutate in (lambda t: t.update(device_position=1), lambda t: t.update(position_clear_qualified=False),
                       lambda t: t.update(raw_position=1), lambda t: t.update(raw_motion=1),
                       lambda t: t.update(action_kind="clear_alarm")):
            bad = copy.deepcopy(result); mutate(bad)
            with self.assertRaises(bench.BenchError): bench.Console._check_action(bad, "position-clear", 1, None)
        wrong = copy.deepcopy(result)
        raw = bytes((1, 6, 0, 0x2D, 0, 0x22))
        wrong["write_evidence"]["raw_hex"] = (raw + bench.wire_crc(raw).to_bytes(2, "little")).hex()
        with self.assertRaises(bench.BenchError): bench.Console._check_action(wrong, "position-clear", 1, None)

    def test_zero_radians_absolute_retains_allowance_without_approximating(self):
        item = move_terminal(2)
        item.update(command="move-absolute", move_kind="absolute", effective_native=0, endpoint_native=0,
                    endpoint_known=True, displacement_native=-750)
        item["staging_words"][3:] = [0, 0]
        item["requested"].update(relative=False, numerator=0, unit="rad", frame=1, approximate=True,
                                 rounding=1, maximum_quantization_error=1, maximum_approximation_error=0.5)
        item["reference"].update(target=1, generation=9, configuration_generation=3,
            native_known=True, native_position=750, source=3, observed_us=900, maximum_age_us=10000)
        raw = bytes((1, 6, 0, 0x27, 0, 5))
        item["trigger_evidence"]["raw_hex"] = (raw + bench.wire_crc(raw).to_bytes(2, "little")).hex()
        args = ("0", "rad", "motor", "60", "configured", "round", "nearest", "1", "approx", "0.5")
        bench.Console._check_move(item, 1, args, "absolute")
        item["requested"]["numerator"] = 1
        with self.assertRaises(bench.BenchError): bench.Console._check_move(item, 1, None, "absolute")

    def test_finite_coordinate_modes_keep_explicit_policy_arguments(self):
        common = ["--port", "COM13", "--log", "unused.jsonl"]
        absolute = bench.arguments(common + ["move-absolute", "720", "deg", "motor", "60", "configured", "--cleanup-stop", "normal"])
        self.assertEqual(absolute.move_args, ("720", "deg", "motor", "60", "configured"))
        angle = bench.arguments(common + ["move-angle", "1", "rad", "motor", "shortest", "negative", "60", "configured",
            "--round", "nearest", "--maximum-error", "1", "--approximation-error", "0.001", "--cleanup-stop", "direct"])
        self.assertEqual(angle.move_args, ("1", "rad", "motor", "shortest", "negative", "60", "configured", "round", "nearest", "1", "approx", "0.001"))

    def test_unknown_move_inspections_preserve_api_rounding_and_radian_inputs(self):
        for rounding in (1, 2, 3, 4):
            item = move_terminal(2)
            item["requested"].update(numerator=2001 if rounding != 4 else 1999, denominator=2,
                rounding=rounding, maximum_quantization_error=0.5)
            item["rounding_error"] = -0.5 if rounding != 4 else 0.5
            def handler(request_id, command, args):
                return encoded(dict(item, type="reply", id=request_id, command="result")) if command == "result" else Serial.normal(request_id, command, args)
            console = self.session(handler)
            self.assertEqual(console.command("result", operation_id=102)["requested"]["rounding"], rounding)
        for rational in (True, False):
            item = move_terminal(2)
            item["requested"].update(numerator=1, denominator=1, unit="rad", frame=1, rounding=1,
                approximate=True, rational_radians=rational, radians=None if rational else 1,
                maximum_quantization_error=0.5, maximum_approximation_error=1e-9)
            item.update(exact_arithmetic=False, requested_native_approximate=1000.125,
                rounding_error=-0.125, approximation_error_bound=1e-11)
            bench.Console._check_move(item, 1, None)
            referenced = copy.deepcopy(item)
            referenced.update(endpoint_known=True, endpoint_native=51000, requested_native_approximate=51000.125)
            bench.Console._check_move(referenced, 1, None)
            for mutate in (lambda t: t["requested"].update(maximum_approximation_error=1e-12),
                           lambda t: t["requested"].update(maximum_quantization_error=0.1),
                           lambda t: t["requested"].update(approximate=False),
                           lambda t: t.update(requested_native_approximate=None)):
                bad = copy.deepcopy(item); mutate(bad)
                with self.assertRaises(bench.BenchError): bench.Console._check_move(bad, 1, None)
        item["requested"]["radians"] = None
        with self.assertRaises(bench.BenchError): bench.Console._check_move(item, 1, None)
        with self.assertRaises(bench.BenchError): bench.Console._check_move(item, 1, self.MOVE_ARGS)

    def test_move_retained_inspection_and_exact_request_correlation(self):
        console = self.session(MoveSerial())
        handle = console.begin("move-relative", address=1, move_args=self.MOVE_ARGS)
        result = console.wait(handle)
        self.assertTrue(result["ok"] and result["running_observed"])
        self.assertEqual(result["effective_native"], 1000)
        inspected = console.command("result", operation_id=handle.operation_id)
        self.assertEqual(inspected["staging_evidence"], result["staging_evidence"])
        console.command("release", operation_id=handle.operation_id)
        self.assertFalse(console.operations)
        self.assertEqual(self.port.writes[1], b"@2 move relative 1000 steps native 60 configured 1\n")

    def test_move_trigger_closure_bound_includes_staging_delivery(self):
        def inclusive(t):
            t["trigger_evidence"]["earliest_us"] = t["staging_evidence"]["delivered_us"]
        console = self.session(MoveSerial(inclusive))
        result = console.command("move-relative", address=1, move_args=self.MOVE_ARGS)
        self.assertTrue(result["ok"])
        self.assertTrue(console.synchronized)
        self.assertEqual(len(self.port.writes), 3)  # Identify, move once, local release.

    def test_move_cancel_same_tick_as_staging_retains_cleanup_access(self):
        def cancelled(t):
            delivered = t["staging_evidence"]["delivered_us"]
            empty = copy.deepcopy(t["failure_evidence"])
            evidence = dict(empty, step=1, event=2, delivered_us=delivered,
                status="ILLEGAL_VALUE", detail=18)
            t.update(ok=False, state="failed", outcome="cancelled", status="ILLEGAL_VALUE", detail=18,
                execution="not_transmitted", completion="not_observed", uncertain=True, polls=0,
                serviced_us=delivered, running_observed=False, observation_known=False, raw_alarm=None, raw_motion=None)
            t["trigger_evidence"] = evidence
            t["failure_evidence"] = copy.deepcopy(evidence)
            t["activity_evidence"] = copy.deepcopy(empty)
            t["last_observation"] = empty
        console = self.session(MoveSerial(cancelled))
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaisesRegex(bench.BenchError, "move rejected or failed: cancelled"):
            bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="normal", timeout_s=3, address=1)
        self.assertTrue(console.synchronized)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("move"), 1)
        self.assertEqual(commands.count("stop"), 1)
        self.assertEqual(events[-1][1]["cleanup"], "drive_reported_nonrunning")
        self.assertTrue(events[-1][1]["move_result"]["uncertain"])

    def test_move_write_readiness_deadlines_and_delayed_delivery(self):
        def failure(step, age, delivered=None, local=False):
            item = move_terminal(2)
            item["prerequisites"]["maximum_age_us"] = age
            empty = copy.deepcopy(item["failure_evidence"])
            evidence = copy.deepcopy(item["staging_evidence" if step == 0 else "trigger_evidence"])
            if local:
                evidence = dict(empty, step=step, event=3, status="ILLEGAL_VALUE", detail=6)
            if delivered is not None: evidence["delivered_us"] = delivered
            item.update(ok=False, state="failed", outcome="deadline", status="ILLEGAL_VALUE", detail=6,
                completion="not_observed", serviced_us=evidence["delivered_us"], polls=0,
                running_observed=False, observation_known=False, raw_alarm=None, raw_motion=None,
                execution="acknowledged" if step == 1 and not local else "not_transmitted",
                setup_execution="not_transmitted" if step == 0 and local else "acknowledged",
                staging_applied=not (step == 0 and local), uncertain=not (step == 0 and local))
            item["staging_evidence" if step == 0 else "trigger_evidence"] = evidence
            item["failure_evidence"] = copy.deepcopy(evidence)
            if step == 0: item["trigger_evidence"] = copy.deepcopy(empty)
            item["activity_evidence"] = copy.deepcopy(empty)
            item["last_observation"] = empty
            return item
        # Local readiness expiry may precede the overall deadline; late closure
        # and delayed delivery still preserve known acknowledgements independently.
        for item in (failure(0, 250, 1150, True), failure(1, 400, 1300, True),
                     failure(0, 250), failure(0, 300), failure(1, 500)):
            with self.subTest(item=item):
                bench.Console._check_move(item, 1, self.MOVE_ARGS)
        timely = move_terminal(2)
        timely["prerequisites"]["maximum_age_us"] = 1200  # Trigger closure equals cap2100; delivery2150.
        bench.Console._check_move(timely, 1, self.MOVE_ARGS)
        saturated = copy.deepcopy(timely)
        saturated["prerequisites"]["maximum_age_us"] = 2**64 - 1
        bench.Console._check_move(saturated, 1, self.MOVE_ARGS)
        for malformed in (failure(0, 250, 1149, True), failure(1, 400, 1299, True)):
            with self.assertRaises(bench.BenchError): bench.Console._check_move(malformed, 1, self.MOVE_ARGS)
        late = copy.deepcopy(timely); late["prerequisites"]["maximum_age_us"] -= 1
        with self.assertRaises(bench.BenchError): bench.Console._check_move(late, 1, self.MOVE_ARGS)
        expired_stage = copy.deepcopy(timely)
        expired_stage["staging_evidence"].update(delivered_us=2100)
        with self.assertRaises(bench.BenchError): bench.Console._check_move(expired_stage, 1, self.MOVE_ARGS)
        admission_at_expiry = copy.deepcopy(timely)
        admission_at_expiry["prerequisites"]["maximum_age_us"] = 100
        with self.assertRaises(bench.BenchError): bench.Console._check_move(admission_at_expiry, 1, self.MOVE_ARGS)
        unsent_after_stale_stage = failure(1, 350, 1300, True)
        with self.assertRaises(bench.BenchError): bench.Console._check_move(unsent_after_stale_stage, 1, self.MOVE_ARGS)

    def test_move_campaign_interrupt_before_acceptance_is_unknown_without_replay(self):
        def interrupt(request_id, command, arguments):
            if command == "move": raise KeyboardInterrupt()
            return Serial.normal(request_id, command, arguments)
        console = self.session(interrupt)
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaises(KeyboardInterrupt):
            bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="normal", timeout_s=3, address=1)
        self.assertFalse(console.synchronized)
        self.assertEqual(len(self.port.writes), 2)
        self.assertEqual(events[-1][1]["cleanup"], "unknown")
        self.assertIn("framing", events[-1][1]["cleanup_error"])

    def test_move_campaign_interrupt_during_wait_is_unknown_without_replay(self):
        console = self.session(MoveSerial(admission_only=True))
        read = self.port.read
        def interrupt(limit):
            if not self.port.input and len(self.port.writes) == 2: raise KeyboardInterrupt()
            return read(limit)
        self.port.read = interrupt
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaises(KeyboardInterrupt):
            bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="normal", timeout_s=3, address=1)
        self.assertFalse(console.synchronized)
        self.assertTrue(console.operations[102].accepted)
        self.assertEqual(len(self.port.writes), 2)
        self.assertEqual(events[-1][1]["cleanup"], "unknown")
        self.assertIn("framing", events[-1][1]["cleanup_error"])

    def test_move_corrupted_staging_trigger_completion_and_provenance_fail_without_replay(self):
        changes = [lambda t: t.update(configuration_generation=4),
            lambda t: t.update(effective_native=999), lambda t: t["requested"].update(numerator=999),
            lambda t: t.update(native_rpm=61), lambda t: t["prerequisites"].update(configured_ramp_verified=False),
            lambda t: t["prerequisites"].update(observed_us=1001), lambda t: t["prerequisites"].update(word_order=1),
            lambda t: t["staging_evidence"].update(tx_accepted=8), lambda t: t["staging_evidence"].update(response_confirmed=False),
            lambda t: t["trigger_evidence"].update(earliest_us=1200),
            lambda t: t["trigger_evidence"].update(raw_hex=t["staging_evidence"]["raw_hex"]),
            lambda t: t.update(running_observed=False), lambda t: t["activity_evidence"].update(earliest_us=2100),
            lambda t: t["activity_evidence"].update(raw_hex=t["last_observation"]["raw_hex"]),
            lambda t: t["last_observation"].update(raw_hex=t["activity_evidence"]["raw_hex"]),
            lambda t: t["last_observation"].update(earliest_us=3300), lambda t: t.update(uncertain=True),
            lambda t: t.update(command_id=99), lambda t: t.update(operation_id=99)]
        for change in changes:
            with self.subTest(change=change):
                console = self.session(MoveSerial(change))
                with self.assertRaises(bench.BenchError): console.command("move-relative", address=1, move_args=self.MOVE_ARGS)
                self.assertEqual(len(self.port.writes), 2)
                self.assertFalse(console.synchronized)

    def test_move_activity_requires_fault_free_correlated_observation(self):
        def frame(entry, alarm, motion):
            raw = bytes((1, 3, 4, alarm >> 8, alarm & 255, motion >> 8, motion & 255))
            entry["raw_hex"] = (raw + bench.wire_crc(raw).to_bytes(2, "little")).hex()
        for alarm, motion in ((1, 4), (0, 12), (0, 20), (0, 36), (0, 68)):
            item = move_terminal(2); frame(item["activity_evidence"], alarm, motion)
            with self.subTest(alarm=alarm, motion=motion):
                with self.assertRaises(bench.BenchError): bench.Console._check_move(item, 1, self.MOVE_ARGS)
        future = move_terminal(2); future["activity_evidence"]["step"] = 64
        with self.assertRaises(bench.BenchError): bench.Console._check_move(future, 1, self.MOVE_ARGS)
        missing = move_terminal(2)
        missing.update(ok=False, state="failed", outcome="cancelled", status="ILLEGAL_VALUE", detail=18,
            completion="not_observed", uncertain=True, observation_known=False, raw_alarm=None, raw_motion=None, polls=1)
        missing["last_observation"] = copy.deepcopy(missing["failure_evidence"])
        missing["failure_evidence"].update(step=3, event=2, delivered_us=4300, status="ILLEGAL_VALUE", detail=18)
        with self.assertRaises(bench.BenchError): bench.Console._check_move(missing, 1, self.MOVE_ARGS)
        # A bounded operation can exhaust its single poll on the first RUNNING
        # report. Activity and last observation then retain exactly one event.
        same = move_terminal(2)
        same.update(ok=False, state="failed", outcome="observation_limit", status="ILLEGAL_VALUE", detail=21,
            completion="not_observed", uncertain=True, raw_motion=4, polls=1, serviced_us=3300)
        same["last_observation"] = copy.deepcopy(same["activity_evidence"])
        same["failure_evidence"] = copy.deepcopy(same["activity_evidence"])
        bench.Console._check_move(same, 1, self.MOVE_ARGS)
        changed = copy.deepcopy(same); changed["activity_evidence"]["earliest_us"] -= 1
        with self.assertRaises(bench.BenchError): bench.Console._check_move(changed, 1, self.MOVE_ARGS)
        # A local cancellation may share the latest RUNNING delivery timestamp.
        cancelled = move_terminal(2)
        frame(cancelled["last_observation"], 0, 4)
        cancelled.update(ok=False, state="failed", outcome="cancelled", status="ILLEGAL_VALUE", detail=18,
            completion="not_observed", uncertain=True, raw_motion=4)
        cancelled["failure_evidence"].update(step=4, event=2, delivered_us=4300, status="ILLEGAL_VALUE", detail=18)
        bench.Console._check_move(cancelled, 1, self.MOVE_ARGS)

    def test_move_staging_timeout_retains_uncertainty_and_never_starts_or_replays(self):
        def timeout(t):
            t.update(ok=False, state="failed", outcome="transport_error", status="ILLEGAL_VALUE", detail=17,
                setup_execution="unknown", execution="not_transmitted", completion="not_observed", staging_applied=False,
                uncertain=True, running_observed=False, observation_known=False, raw_alarm=None, raw_motion=None, polls=0, serviced_us=1500)
            empty = t["failure_evidence"]
            t["trigger_evidence"] = copy.deepcopy(empty)
            t["activity_evidence"] = copy.deepcopy(empty)
            t["last_observation"] = copy.deepcopy(empty)
            t["staging_evidence"] = dict(empty, event=1, tx_accepted=5, execution_unknown=True,
                delivered_us=1500, status="ILLEGAL_VALUE", detail=17)
            t["failure_evidence"] = copy.deepcopy(t["staging_evidence"])
        console = self.session(MoveSerial(timeout))
        result = console.command("move-relative", address=1, move_args=self.MOVE_ARGS)
        self.assertFalse(result["ok"])
        self.assertTrue(result["uncertain"])
        self.assertEqual(len(self.port.writes), 3)
        self.assertTrue(console.synchronized)

    def test_move_arguments_are_bounded_before_transmission(self):
        console = self.session()
        before = len(self.port.writes)
        for arguments in (None, (), ("1", "steps", "native", "60", "default"),
            ("1", "steps", "unknown", "60", "configured"), ("1 2", "steps", "native", "60", "configured"),
            ("1" * 128, "steps", "native", "60", "configured")):
            with self.subTest(arguments=arguments):
                with self.assertRaises(ValueError): console.begin("move-relative", address=1, move_args=arguments)
                self.assertEqual(len(self.port.writes), before)
                self.assertTrue(console.synchronized)

    def test_move_campaign_one_attempt_stop_cleanup_and_local_release(self):
        console = self.session(MoveSerial())
        events = []; console.emit = lambda event, **data: events.append((event, data))
        bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="normal", timeout_s=3, address=1)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("move"), 1)
        self.assertEqual(commands.count("stop"), 1)
        self.assertEqual(commands.count("read"), 1)
        self.assertNotIn("motor-release", commands)
        self.assertNotIn("recover", commands)
        self.assertFalse(console.operations)
        summary = events[-1][1]
        self.assertEqual(summary["cleanup"], "drive_reported_nonrunning")
        self.assertEqual(summary["physical_observation"], "not_supplied")
        self.assertTrue(summary["ok"])

    def test_move_campaign_rejection_sends_no_cleanup_motor_command(self):
        def rejected(i, command, args):
            if command == "move": return encoded(reply(i, "move-relative", ok=False, result="timing_unqualified"))
            return Serial.normal(i, command, args)
        console = self.session(rejected)
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaises(bench.BenchError):
            bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="direct", timeout_s=3, address=1)
        self.assertEqual(len(self.port.writes), 2)
        self.assertEqual(events[-1][1]["cleanup"], "not_required")
        self.assertFalse(events[-1][1]["ok"])

    def test_move_campaign_poisoned_reply_retains_unknown_cleanup(self):
        console = self.session(MoveSerial(lambda t: t.update(running_observed=False)))
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaises(bench.BenchError):
            bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="normal", timeout_s=3, address=1)
        self.assertEqual(len(self.port.writes), 2)
        self.assertEqual(events[-1][1]["cleanup"], "unknown")
        self.assertIn("framing", events[-1][1]["cleanup_error"])
        self.assertFalse(events[-1][1]["ok"])

    def test_action_terminal_and_retained_round_trip(self):
        for command, policy in (("enable", None), ("motor-release", None), ("alarm-clear", None),
                                ("stop", "normal"), ("stop", "direct")):
            with self.subTest(command=command, policy=policy):
                console = self.session(ActionSerial())
                handle = console.begin(command, address=1, stop_policy=policy)
                result = console.wait(handle)
                self.assertTrue(result["ok"])
                inspected = console.command("result", operation_id=handle.operation_id)
                self.assertEqual(inspected["write_evidence"], result["write_evidence"])
                console.command("release", operation_id=handle.operation_id)
                self.assertFalse(console.operations)

    def test_action_acknowledgement_requires_confirmed_source_and_exact_request(self):
        changes = [lambda t: t.pop("interrupted_by_stop"),
                   lambda t: t.update(interrupted_by_stop=1),
                   lambda t: t["write_evidence"].update(response_confirmed=False),
                   lambda t: t["write_evidence"].update(tx_complete=False),
                   lambda t: t["write_evidence"].update(raw_hex="0106002D0011D9CF"),
                   lambda t: t.update(raw_motion=17),
                   lambda t: t.update(action_kind="release"),
                   lambda t: t.update(command_id=999),
                   lambda t: t["last_observation"].update(raw_hex="010304000000010000")]
        for change in changes:
            with self.subTest(change=change):
                console = self.session(ActionSerial(change))
                with self.assertRaises(bench.BenchError): console.command("enable", address=1)
                self.assertEqual(len(self.port.writes), 2)

    def test_completed_action_can_retain_accepted_stop_interruption(self):
        console = self.session(ActionSerial(lambda t: t.update(interrupted_by_stop=True)))
        handle = console.begin("enable", address=1)
        result = console.wait(handle)
        self.assertTrue(result["ok"] and result["interrupted_by_stop"])
        inspected = console.command("result", operation_id=handle.operation_id)
        self.assertTrue(inspected["interrupted_by_stop"])
        console.command("release", operation_id=handle.operation_id)

    def test_action_rejects_impossible_cross_transaction_evidence(self):
        def failure_on_success(t):
            t["failure_evidence"] = copy.deepcopy(t["write_evidence"])
            t["failure_evidence"].update(status="CRC_ERROR", frame_error=6)

        changes = {
            "observation before acknowledgement": lambda t: t["last_observation"].update(
                earliest_us=1001, latest_us=1010, delivered_us=1020),
            "closure after deadline": lambda t: t.update(deadline_us=2100),
            "poll count differs from token": lambda t: t.update(polls=64),
            "successful truncated observation": lambda t: t["last_observation"].update(received_length=100),
            "success retains failure": failure_on_success,
            "success serviced after final event": lambda t: t.update(serviced_us=2400),
            "unbounded detail": lambda t: t["write_evidence"].update(transport_detail=2**31),
        }
        for name, change in changes.items():
            with self.subTest(name=name):
                console = self.session(ActionSerial(change))
                with self.assertRaises(bench.BenchError):
                    console.command("stop", address=1, stop_policy="normal")
                self.assertEqual(len(self.port.writes), 2)  # No retry or result release after bad evidence.

    def test_action_accepts_ontime_observation_delivered_after_deadline(self):
        def late(t):
            t.update(serviced_us=11000)
            t["last_observation"].update(delivered_us=11000)
        console = self.session(ActionSerial(late))
        result = console.command("enable", address=1)
        self.assertTrue(result["ok"])
        self.assertLess(result["last_observation"]["latest_us"], result["deadline_us"])

    def test_action_exception_classification_preserves_unknown_codes(self):
        for code in (0, 1, 7, 8, 255):
            def exception(t):
                raw = bytes((1, 0x86, code))
                t.update(ok=False, state="failed", outcome="reply_error",
                    execution="rejected" if 1 <= code <= 7 else "unknown", completion="not_observed",
                    observation_known=False, raw_alarm=None, raw_motion=None, polls=0, serviced_us=1250,
                    last_observation=copy.deepcopy(t["failure_evidence"]))
                t["write_evidence"].update(raw_hex=(raw + bench.wire_crc(raw).to_bytes(2, "little")).hex(),
                    received_length=5, status="EXCEPTION", detail=code, frame_error=10)
                t["failure_evidence"] = copy.deepcopy(t["write_evidence"])
            with self.subTest(code=code):
                console = self.session(ActionSerial(exception))
                result = console.command("enable", address=1)
                self.assertEqual(result["write_evidence"]["detail"], code)
                if code not in range(1, 8):
                    result["execution"] = "rejected"
                    with self.assertRaises(bench.BenchError): bench.Console._check_action(result, "enable", 1, None)
                else:
                    result["write_evidence"]["detail"] = code + 1
                    with self.assertRaises(bench.BenchError): bench.Console._check_action(result, "enable", 1, None)

    def test_action_retains_failed_truncated_and_unqualified_frames(self):
        for qualified in (False, True):
            def failure(t):
                empty = copy.deepcopy(t["failure_evidence"])
                t.update(ok=False, state="failed", outcome="reply_error" if qualified else "timing_unqualified",
                    completion="not_observed", observation_known=False, raw_alarm=None, raw_motion=None, polls=0,
                    last_observation=empty)
                t["failure_evidence"] = dict(step=1, event=0, raw_hex="010304000000010000", received_length=100,
                    tx_accepted=8, tx_complete=True, response_confirmed=True, qualified=qualified,
                    execution_unknown=True, earliest_us=2000 if qualified else 0, latest_us=2200 if qualified else 0,
                    delivered_us=2300, transport_detail=0, status="FRAME_ERROR", detail=2, frame_error=2)
            with self.subTest(qualified=qualified):
                console = self.session(ActionSerial(failure))
                result = console.command("enable", address=1)
                self.assertEqual(result["execution"], "acknowledged")
                self.assertEqual(result["failure_evidence"]["received_length"], 100)
                result["outcome"] = "cancelled"
                with self.assertRaises(bench.BenchError): bench.Console._check_action(result, "enable", 1, None)

    def test_action_retains_prior_observation_on_later_cancellation(self):
        def cancelled(t):
            # RUNNING remains set, so this checked stop observation was incomplete.
            raw = bytes((1, 3, 4, 0, 0, 0, 5))
            t["last_observation"]["raw_hex"] = (raw + bench.wire_crc(raw).to_bytes(2, "little")).hex()
            t.update(ok=False, state="failed", outcome="cancelled", completion="not_observed",
                     raw_motion=5, serviced_us=2500)
            t["failure_evidence"].update(step=2, event=2, delivered_us=2500, status="ILLEGAL_VALUE", detail=13)
        console = self.session(ActionSerial(cancelled))
        result = console.command("stop", address=1, stop_policy="normal")
        self.assertEqual(result["polls"], 1)
        self.assertTrue(result["observation_known"])
        self.assertEqual(result["execution"], "acknowledged")
        for change in (lambda t: t["failure_evidence"].update(step=3),
                       lambda t: t["failure_evidence"].update(response_confirmed=True),
                       lambda t: t["failure_evidence"].update(delivered_us=2000)):
            corrupt = copy.deepcopy(result)
            change(corrupt)
            with self.assertRaises(bench.BenchError): bench.Console._check_action(corrupt, "stop", 1, "normal")

    def test_action_partial_and_full_tx_failures_remain_unknown_without_replay(self):
        for accepted in (1, 8):
            def failure(t):
                empty = copy.deepcopy(t["failure_evidence"])
                t.update(ok=False, state="failed", outcome="transport_error", execution="unknown",
                    completion="not_observed", observation_known=False, raw_alarm=None, raw_motion=None, polls=0,
                    last_observation=empty, serviced_us=1250)
                t["write_evidence"].update(event=1, raw_hex="", received_length=0, tx_accepted=accepted,
                    tx_complete=accepted == 8, qualified=False, response_confirmed=False, execution_unknown=True,
                    earliest_us=0, latest_us=0, status="ILLEGAL_VALUE", detail=12)
                t["failure_evidence"] = copy.deepcopy(t["write_evidence"])
            console = self.session(ActionSerial(failure))
            result = console.command("enable", address=1)
            self.assertEqual(result["execution"], "unknown")
            self.assertEqual(len(self.port.writes), 3)  # identify, one write request, local result release
            self.assertFalse(console.operations)

    def test_retained_action_kind_and_stop_policy_cannot_change(self):
        fixture = ActionSerial()
        console = self.session(fixture)
        handle = console.begin("stop", address=1, stop_policy="normal")
        console.wait(handle)
        fixture.retained[handle.operation_id] = dict(fixture.retained[handle.operation_id], stop_policy="direct")
        with self.assertRaisesRegex(bench.BenchError, "policy"):
            console.command("result", operation_id=handle.operation_id)

    def test_actions_share_ordinary_quota_but_stop_has_reserved_result(self):
        fixture = ActionSerial()
        console = self.session(fixture)
        for _ in range(bench.MAX_PROBES):
            handle = console.begin("enable", address=1)
            console.wait(handle)
        recovery = console.begin("recover"); console.wait(recovery)
        stop = console.begin("stop", address=1, stop_policy="direct"); console.wait(stop)
        self.assertEqual(len(console.operations), bench.MAX_OPERATIONS)
        with self.assertRaisesRegex(bench.BenchError, "retained result limit"):
            console.begin("enable", address=1)

    def test_actions_are_explicit_single_attempts_when_timing_unqualified(self):
        for command in bench.ACTION_COMMANDS:
            with self.subTest(command=command):
                def rejected(i, name, args):
                    if name in bench.ACTION_COMMANDS:
                        return encoded(reply(i, name, ok=False, result="timing_unqualified", address=1))
                    return Serial.normal(i, name, args)
                console = self.session(rejected)
                policy = "normal" if command == "stop" else None
                result = console.command(command, address=1, stop_policy=policy)
                self.assertFalse(result["ok"])
                self.assertEqual(result["result"], "timing_unqualified")
                self.assertFalse(console.operations)
                expected = f"@2 {command}" + (" normal" if policy else "") + " 1\n"
                self.assertEqual(self.port.writes, [b"@1 version\n", expected.encode()])

    def test_stop_policy_rejected_before_serial_and_actions_not_campaigns(self):
        console = self.session()
        for command, policy in (("stop", None), ("stop", "release"), ("enable", "normal")):
            with self.assertRaises(ValueError):
                console.command(command, stop_policy=policy)
        for command in bench.ACTION_COMMANDS:
            with self.assertRaises(ValueError):
                bench.campaign(console, command, count=1, interval_s=0, timeout_s=1, address=1)
        self.assertEqual(self.port.writes, [b"@1 version\n"])
        for policy in ("normal", "direct"):
            args = bench.arguments(["--port", "fake", "--log", "fake.jsonl", "stop", policy])
            self.assertEqual(args.stop_policy, policy)
            self.assertEqual(args.count, 1)

    def test_host_preparation_preserves_integer_precision_and_correlation(self):
        value = 9007199254740993
        def handler(request_id, command, args):
            if command == "prepare":
                return encoded(reply(request_id, command, code="OK", detail=0, bus_traffic=False,
                    motion_command=False, wire_motion="not_requested", configuration_generation=2,
                    target=1, address=1, binding_generation=1,
                    requested=dict(numerator=value, denominator=1, unit="steps", frame=0, relative=True, wrapped=False, angle_path=2, half_turn_tie=0, basis=0, rounding=0),
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
                            motion_command=False, wire_motion="not_requested", configuration_generation=2,
                            target=1, address=1, binding_generation=1,
                            requested=dict(numerator=0 if exact else 1, denominator=1, unit="rad", frame=1,
                                relative=relative, wrapped=False, angle_path=2, half_turn_tie=0, basis=0, rounding=0 if exact else 1),
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
                    motion_command=False, wire_motion="not_requested", configuration_generation=0,
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

    def test_action_invalidation_retains_raw_but_invalidates_current_cache(self):
        item = cached_state(2)
        original = copy.deepcopy(item["state_blocks"][0]["value"])
        item["state_blocks"][0].update(invalidated_us=2100, current=False, fresh=False)
        item.update(alarms="unknown", state="unknown")
        bench.Console._check_cached_state(item)
        self.assertEqual(item["state_blocks"][0]["value"], original)
        for invalid in (None, True, -1, 5001):
            bad = copy.deepcopy(item); bad["state_blocks"][0]["invalidated_us"] = invalid
            with self.assertRaises(bench.BenchError): bench.Console._check_cached_state(bad)
        item["state_blocks"][0].update(current=True, fresh=True)
        with self.assertRaises(bench.BenchError): bench.Console._check_cached_state(item)
        item["state_blocks"][0].update(invalidated_us=999)
        item.update(alarms="clear", state="observed")
        bench.Console._check_cached_state(item)

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
        self.assertEqual(len(console.operations), bench.MAX_PROBES + 1)
        self.port.handler = lambda i, command, args: encoded(reply(i, command, ok=False, result="results_full"))
        rejection = console.command("probe", timeout_s=0.1)
        self.assertFalse(rejection["ok"])
        self.assertEqual(len(console.operations), bench.MAX_PROBES + 1)
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
                                                                  protocol=1, outstanding_capacity=10))
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
