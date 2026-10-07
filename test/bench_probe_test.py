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
    if command == "debug":
        fields.setdefault("missed", 0)
        fields.setdefault("skipped", 0)
        fields.setdefault("owner", dict(phase="IDLE", busy=False, recovery_required=False, pending=0, retained=0))
        fields.setdefault("capture", dict(mode="timer", faults=0, max_poll_gap_us=12))
        fields.setdefault("memory", dict(valid=True, stack_free_bytes=2000, internal_free=10000, psram_free=100000))
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
                 cpu_valid=False, cpu0_busy_pct=None, cpu1_busy_pct=None,
                 console_dropped=0, capture_us=800, capture_samples=200,
                 timer_callbacks=180, sample_gap_limit_us=85,
                 sample_gap_exceeded=False, capture_high_water=7,
                 owner_gap_max_us=5001, capture_gap_max_us=10,
                 work_stack_free_bytes=2500, **fields)


def driver_terminal(request_id, update=False):
    """Independent signed/pair read vectors and one direction update."""
    frames = [(0x10, 1, True, bytes.fromhex("01060010000149CF")),
              (0x10, 1, False, bytes.fromhex("01030200017984"))] if update else [
        (0x10, 2, False, bytes.fromhex("01030400000640E862")),
        (0x17, 3, False, bytes.fromhex("0103060000000000002175")),
        (0x37, 4, False, bytes.fromhex("010308000100020003000469FA")),
        (0x50, 2, False, bytes.fromhex("01030400000001FA33"))]
    # Seal literal register payloads independently; the production builder is absent.
    evidence = []
    for index, (reg, count, write, raw) in enumerate(frames):
        raw = raw[:-2] + bench.wire_crc(raw[:-2]).to_bytes(2, "little")
        evidence.append([index, reg, count, write, 0, raw.hex(), len(raw), 8, True, True, True, False,
                         1100 + index * 100, 1110 + index * 100, 1120 + index * 100, 1, "OK", 0, 0, 1000 if index == 0 else 1020 + index * 100])
    return reply(request_id, "driver", type="driver", command_id=request_id, operation_id=request_id + 100,
                 driver=True, driver_kind="update" if update else "read", state="succeeded", outcome="success",
                 status="OK", detail=0, target=1, address=1, generation=9, configuration_generation=3,
                 started_us=1000, deadline_us=20000, stationary_valid_until_us=20000, serviced_us=evidence[-1][14], completed_steps=len(evidence),
                 fields=1 if update else 0, effects=1 if update else 0, uncertain=False, atomic=False, active_settings_known=False,
                 progress_columns=bench.DRIVER_PROGRESS_COLUMNS,
                 progress=[[1, 0x10, 0, 1, True, True, 1, False, 0, "acknowledged"]] if update else [],
                 evidence_columns=bench.DRIVER_EVIDENCE_COLUMNS, evidence=evidence,
                 observation=None if update else dict(raw=[0, 1600, 0, 0, 0, 1, 0], known_fields=127,
                     positive_words=[1, 2], negative_words=[3, 4], pair_known=True,
                     positive_bits=65538, negative_bits=196612, signed_limits="unresolved", native_scale="unresolved"))


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
        if command == "read" or (command == "read" and args and args[0] == "state"):
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


def traffic_record(sequence=1, kind="TX", mode="decoded"):
    def frame(prefix):
        return (prefix + bench.wire_crc(prefix).to_bytes(2, "little")).hex()
    request = frame(bytes((1,3,0,0,0,1)))
    response = frame(bytes((1,3,2,0x4e,0xea)))
    rx = kind == "RX"
    return dict(type="traffic", profile="ess_rs", mode=mode, sequence=sequence, transaction=1,
        kind=kind, at_us=200 if rx else 100, start_us=150 if rx else 0, end_us=190 if rx else 0,
        uncertainty_us=2 if rx else 0, length=7 if rx else 8, code=0, complete=True,
        raw_hex=response if rx else request, expected_request_hex=request if rx and mode=="decoded" else "",
        expected_request_sequence=1 if rx and mode=="decoded" else 0,
        decode_status="OK" if mode=="decoded" else None, decode_detail=0, frame_error=0,
        decoded=dict(address=1,function=3,register_start=0,register_count=1,exception=False,exception_code=0,
            words=[20202] if rx else [],function_name="read_registers",register_names=["DRIVER_MODEL"]) if mode=="decoded" else None)


def motion_profile_reply(request_id, action="inspect", restored=False, pending=False):
    words = [30, 50, 50, 60, 0, 5000]
    def frame(prefix):
        return (prefix + bench.wire_crc(prefix).to_bytes(2, "little")).hex()
    read_tx = frame(bytes((1, 3, 0, 0x20, 0, 6)))
    write_tx = frame(bytes((1, 16, 0, 0x21, 0, 5, 10)) + b"".join(x.to_bytes(2, "big") for x in words[1:]))
    data = reply(request_id, "motion-profile", request=action, result="accepted",
        pending=pending, saved=True, restored=restored and not pending, session_ok=not pending,
        phase=(2 if pending else 3) if restored else 1, address=1, configuration_generation=3, serial_generation=1,
        original=list(words), current=list(words), tx_hex=write_tx if restored and pending else read_tx,
        rx_hex="" if pending else frame(bytes((1, 3, 12)) + b"".join(x.to_bytes(2, "big") for x in words)),
        tx_accepted=0 if pending else 8, tx_complete=not pending, closure_qualified=not pending,
        closure_earliest_us=0 if pending else 3000, closure_latest_us=0 if pending else 3200,
        delivered_us=0 if pending else 3300, deadline_us=10000, execution_unknown=False, error="none",
        write_tx_hex="", write_reply_hex="", write_tx_accepted=0, write_tx_complete=False,
        write_closure_qualified=False, write_closure_earliest_us=0, write_closure_latest_us=0,
        write_delivered_us=0, write_execution_unknown=False, restore_unsettled=False,
        write_deadline_us=10000 if restored else 0, write_configuration_generation=3 if restored else 0,
        write_serial_generation=1 if restored else 0, write_binding_generation=9 if restored else 0)
    if restored and not pending:
        data.update(write_tx_hex=write_tx, write_reply_hex=frame(bytes((1, 16, 0, 0x21, 0, 5))),
            write_tx_accepted=19, write_tx_complete=True, write_closure_qualified=True,
            write_closure_earliest_us=1100, write_closure_latest_us=1200, write_delivered_us=1300)
    if action == "forget":
        for key in ("pending", "saved", "restored", "session_ok", "tx_complete", "closure_qualified",
                    "execution_unknown", "write_tx_complete", "write_closure_qualified", "write_execution_unknown", "restore_unsettled"):
            data[key] = False
        for key in ("phase", "address", "configuration_generation", "serial_generation", "tx_accepted", "deadline_us",
                    "closure_earliest_us", "closure_latest_us", "delivered_us", "write_tx_accepted",
                    "write_closure_earliest_us", "write_closure_latest_us", "write_delivered_us", "write_deadline_us",
                    "write_configuration_generation", "write_serial_generation", "write_binding_generation"):
            data[key] = 0
        for key in ("tx_hex", "rx_hex", "write_tx_hex", "write_reply_hex"):
            data[key] = ""
        data["original"] = [0] * 6
        data["current"] = [0] * 6
    return data


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
            if args[0] == "state":
                # Campaign cleanup must prove zero speed separately from the
                # non-running flag; the generic raw-decoder vector is 0xFFFF.
                prefix = bytes.fromhex(terminal["steps"][2]["rx"])[:7] + b"\0\0"
                terminal["steps"][2]["rx"] = (prefix + bench.wire_crc(prefix).to_bytes(2, "little")).hex()
                terminal["state_blocks"][2]["raw_speed"] = 0
            self.retained[request_id + 100] = terminal
            return encoded(reply(request_id, "read-" + args[0], result="accepted", operation_id=request_id + 100, address=1, read_kind=args[0])) + encoded(terminal)
        if command == "result":
            return encoded(dict(self.retained[int(args[0])], type="reply", id=request_id, command="result"))
        if command == "release": self.retained.pop(int(args[0]), None)
        return Serial.normal(request_id, command, args)


def home_terminal(request_id):
    empty = [0, 0, "", 0, 0, False, False, False, False, 0, 0, 0, 0, "OK", 0, 0]
    def frame(step, prefix, at):
        raw = bytes(prefix); raw += bench.wire_crc(raw).to_bytes(2, "little")
        return [step, 0, raw.hex(), len(raw), 21 if step == 0 else 8, True, True, True, False,
                at, at + 10, at + 20, 1, "OK", 0, 0]
    staging = frame(0, (1,16,0,0x31,0,6), 1100)
    trigger = frame(1, (1,6,0,0x27,0,16), 1200)
    completion = frame(2, (1,3,4,0,0,0,3), 1300)
    zero = frame(3, (1,3,4,0,0,0,0), 1400)
    return reply(request_id, "home", type="home", command_id=request_id, operation_id=request_id+100,
        home=True, read_kind=None, action_kind=None, move_kind=None, capture_read=False, recovery=False,
        state="succeeded", outcome="observed", status="OK", detail=0, setup_execution="acknowledged",
        execution="acknowledged", completion="observed", target=1, address=1, generation=9,
        configuration_generation=3, started_us=1000, deadline_us=1000000, serviced_us=1420, polls=1,
        phase=3, method=35, staging_words=[35,60,30,100,0,0], staging_applied=True, uncertain=False,
        running_observed=False, homed_low_observed=True, observation_known=True, raw_alarm=0, raw_motion=3,
        raw_position_words=[0,0], interrupted_by_stop=False, completion_observed_us=1250,
        prerequisites=dict(observed_us=1000,maximum_age_us=100000,raw_motion=1,auxiliary=7,reference_semantics_qualified=True,qualified_parameters=[35,60,30,100]),
        evidence_columns=bench.HOME_EVIDENCE_COLUMNS, staging_evidence=staging, trigger_evidence=trigger,
        activity_evidence=list(empty), low_evidence=list(empty), last_observation=completion,
        completion_evidence=completion, zero_evidence=zero, failure_evidence=list(empty))


class HomeSerial(MoveSerial):
    def __call__(self, request_id, command, args):
        if command == "home":
            terminal = home_terminal(request_id)
            if self.mutate: self.mutate(terminal)
            self.retained[request_id+100] = terminal
            return encoded(reply(request_id, "home", result="accepted", operation_id=request_id+100, address=1)) + encoded(terminal)
        return super().__call__(request_id, command, args)


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
                                 protocol=3, version="0.test", outstanding_capacity=10))
        if command == "probe":
            address = int(args[0]) if args else 1
            tx = bytes((address, 3, 0, 0, 0, 1))
            rx = bytes((address, 3, 2, 0, 60))
            tx = (tx + bench.wire_crc(tx).to_bytes(2, "little")).hex()
            rx = (rx + bench.wire_crc(rx).to_bytes(2, "little")).hex()
            return (encoded(reply(request_id, command, result="accepted", address=address))
                    + encoded(reply(request_id, command, type="probe", address=address,
                                    transport="FRAME", codec="OK", detail=0, frame_error=0, raw_model=60,
                                    outcome="success", execution_unknown=False,
                                    duration_us=12345, tx_bytes=8, rx_bytes=7,
                                    timing_valid=True, raw_truncated=False,
                                    observed_earliest_us=13300, observed_latest_us=13345,
                                    delivered_us=16000,
                                    identity="responder_only", register_start=0, register_count=1,
                                    tx_hex=tx, rx_hex=rx, confidence="responder_model_unresolved",
                                    manufacturer_confirmed=False, exact_model_confirmed=False,
                                    collision_excluded=False)))
        if command == "capture-read":
            address = int(args[0]) if args else 1
            assert address == 1  # This fixed independent raw-frame fixture is node 1.
            return (encoded(reply(request_id, command, result="accepted", address=address))
                    + encoded(reply(request_id, command, type="capture_read", address=address,
                                    transport="FRAME", codec="OK", detail=0, frame_error=0, raw_model=None,
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
    def test_exact_endpoint_completion_without_running(self):
        t = move_terminal(1)
        empty = dict(t["failure_evidence"])
        t.update(running_observed=False, position_feedback_matches_command=True, position_confirmed=True,
                 observed_position=1100, observed_speed=0, endpoint_known=True, endpoint_native=1100,
                 activity_evidence=empty)
        t["reference"].update(target=1, generation=9, configuration_generation=3, native_known=True,
                             native_position=100, source=1, observed_us=900, maximum_age_us=10000)
        def position_frame(e, pos=1100, speed=0, flags=1):
            raw = bytes([1,3,14,0,0,0,flags,0,0,0,0]) + pos.to_bytes(4,'big') + speed.to_bytes(2,'big')
            raw += bench.wire_crc(raw).to_bytes(2,'little')
            e.update(raw_hex=raw.hex(), received_length=len(raw))
        t["position_match_evidence"] = dict(move_terminal(1)["activity_evidence"])
        position_frame(t["position_match_evidence"]); position_frame(t["last_observation"])
        bench.Console._check_move(t,1,None)
        for field, value in (("position_confirmed",False),("execution","unknown"),("observed_position",1101),
                             ("observed_speed",1),("position_feedback_matches_command",False)):
            bad=copy.deepcopy(t);bad[field]=value
            with self.assertRaises(bench.BenchError): bench.Console._check_move(bad,1,None)
        for name in ("position_match_evidence","last_observation"):
            for pos,speed,flags in ((100,0,1),(1101,0,1),(1100,1,1),(1100,0,4),(1100,0,9)):
                bad=copy.deepcopy(t);position_frame(bad[name],pos,speed,flags)
                with self.assertRaises(bench.BenchError): bench.Console._check_move(bad,1,None)
        bad=copy.deepcopy(t);bad["position_match_evidence"]["step"]=1
        with self.assertRaises(bench.BenchError): bench.Console._check_move(bad,1,None)
        bad=copy.deepcopy(t);bad["position_match_evidence"]["delivered_us"]=4100
        with self.assertRaises(bench.BenchError): bench.Console._check_move(bad,1,None)

    HOME_ARGS = ("35", "60", "30", "100", "zero")

    def test_evidence_limits_stop_with_one_retained_failure_record(self):
        for options, fields in ((dict(max_records=2), {}),
                                (dict(max_bytes=512), dict(text="x" * 512)),
                                (dict(max_record_bytes=256), dict(text="x" * 512))):
            with self.subTest(options=options):
                stream = io.StringIO()
                evidence = bench.Evidence(stream, Clock(), **options)
                evidence("first")
                with self.assertRaisesRegex(bench.BenchError, "capacity exhausted"):
                    evidence("second", **fields)
                before = stream.getvalue()
                with self.assertRaises(bench.BenchError): evidence("retry")
                self.assertEqual(stream.getvalue(), before)
                records = [json.loads(line) for line in before.splitlines()]
                self.assertEqual(records[-1]["event"], "evidence_limit")
                self.assertFalse(records[-1]["ok"])
                self.assertEqual(len(records), evidence.records)
                self.assertEqual(len(before), evidence.bytes)
                self.assertLessEqual(evidence.records, evidence.max_records)
                self.assertLessEqual(evidence.bytes, evidence.max_bytes)

    def test_evidence_output_failure_outside_console_forbids_later_send(self):
        for failure in ("write", "flush", "short"):
            class FailingStream(io.StringIO):
                writes = 0
                flushes = 0
                def write(self, text):
                    self.writes += 1
                    if self.writes == 1 and failure == "write":
                        raise OSError("evidence disk unavailable")
                    if self.writes == 1 and failure == "short":
                        return super().write(text[:-1])
                    return super().write(text)
                def flush(self):
                    self.flushes += 1
                    if self.flushes == 1 and failure == "flush":
                        raise OSError("evidence flush unavailable")
                    return super().flush()
            console = self.session()
            stream = FailingStream()
            evidence = bench.Evidence(stream, self.clock)
            console.emit = evidence
            with self.subTest(failure=failure):
                with self.assertRaises((OSError, bench.BenchError)):
                    evidence("phase_command", result={"ok": True})
                self.assertTrue(evidence.exhausted)
                # The stream would recover, but the experiment must not.
                before = stream.getvalue()
                with self.assertRaisesRegex(bench.BenchError, "capacity exhausted"):
                    console.command("stats", timeout_s=.1)
                self.assertFalse(console.synchronized)
                self.assertEqual(self.port.writes, [b"@1 version\n"])
                self.assertEqual(stream.getvalue(), before)
                self.assertEqual(stream.writes, 1)

    def test_cpu_evidence_preserves_available_bounds_or_explicit_unknown(self):
        for change in ({}, {"cpu_valid": True, "cpu0_busy_pct": 0, "cpu1_busy_pct": 100}):
            item = load_reply(1)
            item.update(change)
            bench.check_load_reply(item, (0, 0, 0))
        for change in ({"cpu_valid": None}, {"cpu_valid": 1},
                       {"cpu_valid": True, "cpu0_busy_pct": 0, "cpu1_busy_pct": None},
                       {"cpu_valid": True, "cpu0_busy_pct": -1, "cpu1_busy_pct": 100},
                       {"cpu_valid": True, "cpu0_busy_pct": 101, "cpu1_busy_pct": 0},
                       {"cpu_valid": True, "cpu0_busy_pct": True, "cpu1_busy_pct": 0},
                       {"cpu_valid": True, "cpu0_busy_pct": 50.0, "cpu1_busy_pct": 0},
                       {"cpu0_busy_pct": 0}):
            item = load_reply(1)
            item.update(change)
            with self.subTest(change=change), self.assertRaisesRegex(bench.BenchError, "CPU"):
                bench.check_load_reply(item, (0, 0, 0))
        for field in ("cpu_valid", "cpu0_busy_pct", "cpu1_busy_pct"):
            item = load_reply(1)
            del item[field]
            with self.subTest(missing=field), self.assertRaisesRegex(bench.BenchError, "CPU"):
                bench.check_load_reply(item, (0, 0, 0))

    def test_disconnect_read_or_write_poison_session_without_recovery(self):
        for stage in ("write", "read", "terminal"):
            with self.subTest(stage=stage):
                console = self.session()
                def disconnected(*_): raise OSError("USB disconnected")
                if stage == "write": self.port.write = disconnected
                elif stage == "read": self.port.read = disconnected
                else:
                    self.port.handler = lambda i, cmd, _: encoded(reply(i, cmd, result="accepted", address=1))
                    handle = console.begin("probe", timeout_s=.1)
                    self.assertTrue(handle.accepted)
                    self.port.read = disconnected
                with self.assertRaisesRegex(OSError, "USB disconnected"):
                    console.wait(handle) if stage == "terminal" else console.command("probe", timeout_s=.1)
                self.assertFalse(console.synchronized)
                before = len(self.port.writes)
                with self.assertRaisesRegex(bench.BenchError, "cannot be reused"):
                    console.command("recover", timeout_s=.1)
                self.assertEqual(len(self.port.writes), before)

    def test_single_byte_serial_fragments_keep_original_correlation(self):
        console = self.session()
        self.port.fragment = 1
        result = console.command("probe", address=1, timeout_s=.1)
        self.assertEqual((result["command_id"], result["operation_id"]), (2, 102))
        self.assertTrue(console.synchronized)
        self.assertEqual(self.port.writes, [b"@1 version\n", b"@2 probe 1\n", b"@3 release 102\n"])

    def test_interrupt_during_final_diagnostics_cannot_publish_pass(self):
        console = self.session()
        original = self.port.handler
        stats = 0
        def handler(i, command, args):
            nonlocal stats
            if command == "stats":
                stats += 1
                if stats == 2: raise KeyboardInterrupt()
            return original(i, command, args)
        self.port.handler = handler
        with self.assertRaises(KeyboardInterrupt):
            bench.campaign(console, "stress", count=1, interval_s=0, timeout_s=.1)
        self.assertFalse(self.events[-1]["ok"])
        self.assertEqual(self.events[-1]["iterations"], 1)
        self.assertEqual(self.events[-1]["error"], "interrupted")

    def test_requested_work_or_owner_delay_must_actually_run(self):
        for field, value, error in (("work_us", 0, "no measured work"),
                                    ("owner_gap_max_us", 4999, "not observed")):
            console = self.load_session()
            normal = self.port.handler
            def inactive(i, command, args):
                response = normal(i, command, args)
                if command == "load": response = encoded({**json.loads(response), field: value})
                return response
            self.port.handler = inactive
            with self.subTest(field=field), self.assertRaisesRegex(bench.BenchError, error):
                bench.campaign(console, "load", count=2, interval_s=0, timeout_s=.1, load=(2000,5000,128))
            self.assertFalse(self.events[-1]["ok"])
            self.assertEqual(self.events[-1]["probes_passed"], 2)

    def test_nonrunning_with_nonzero_speed_never_counts_as_cleanup(self):
        for speeds, passes, reads in (((25, 0), True, 2), ((25,) * 10, False, 10)):
            device = MoveSerial()
            original = device.__call__
            samples = iter(speeds)
            def handler(i, command, args):
                response = original(i, command, args)
                if command == "read" and args[0] == "state":
                    accepted, terminal = map(json.loads, response.splitlines())
                    speed = next(samples)
                    prefix = bytes.fromhex(terminal["steps"][2]["rx"])[:7] + speed.to_bytes(2, "big")
                    terminal["steps"][2]["rx"] = (prefix + bench.wire_crc(prefix).to_bytes(2, "little")).hex()
                    terminal["state_blocks"][2]["raw_speed"] = speed
                    device.retained[terminal["operation_id"]] = terminal
                    response = encoded(accepted) + encoded(terminal)
                return response
            console = self.session(handler)
            with self.subTest(passes=passes):
                if passes:
                    bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="fast", timeout_s=3, address=1)
                else:
                    with self.assertRaisesRegex(bench.BenchError, "observation bound"):
                        bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="fast", timeout_s=3, address=1)
                commands = [line.decode().split()[1] for line in self.port.writes]
                self.assertEqual(commands.count("move"), 1)
                self.assertEqual(commands.count("stop"), 1)
                self.assertEqual(commands.count("read"), reads)
                self.assertEqual(self.events[-1]["ok"], passes)
                self.assertEqual(self.events[-1]["cleanup"], "drive_reported_standstill" if passes else "unknown")

    def test_native_inspection_and_release_failures_are_both_retained(self):
        def handler(i, command, args):
            if command == "driver":
                terminal = driver_terminal(i)
                return encoded(reply(i, "driver", result="accepted", address=1, operation_id=i + 100)) + encoded(terminal)
            if command in ("result", "release"):
                return encoded(reply(i, command, ok=False, result="busy"))
            return Serial.normal(i, command, args)
        console = self.session(handler)
        with self.assertRaisesRegex(bench.BenchError, "inspection failed"):
            bench.driver_read_campaign(console, timeout_s=.1, address=1)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands, ["version", "driver", "result", "release"])
        summary = self.events[-1]
        self.assertFalse(summary["ok"])
        self.assertEqual(summary["error"], "driver retained result inspection failed")
        self.assertEqual(summary["release_error"], "driver result release rejected")
        self.assertTrue(console.synchronized)
        self.assertEqual(len(console.operations), 1)

    def test_move_disabled_age_keeps_terminal_and_framing_valid(self):
        # Reproduces the hardware rejection: firmware emits maximum_age_us=0.
        console = self.session(MoveSerial(lambda r: r["prerequisites"].update(maximum_age_us=0)))
        result = console.command("move-relative", address=1, move_args=self.MOVE_ARGS)
        self.assertTrue(result["ok"])
        self.assertTrue(console.synchronized)
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "move", "release"])
        for mutate in (
                lambda r: r["prerequisites"].update(observed_us=r["started_us"] + 1),
                lambda r: r["prerequisites"].update(generation=10),
                lambda r: r["prerequisites"].update(readiness_qualified=False),
                lambda r: r["prerequisites"].update(maximum_age_us=100),
                lambda r: r.update(deadline_us=2100),
                lambda r: r["activity_evidence"].update(latest_us=1000)):
            item = move_terminal(2); item["prerequisites"]["maximum_age_us"] = 0
            mutate(item)
            with self.assertRaises(bench.BenchError): bench.Console._check_move(item, 1, self.MOVE_ARGS)

    def test_move_optional_reference_age_preserves_binding_and_write_budget(self):
        item = move_terminal(2); item["prerequisites"]["maximum_age_us"] = 0
        item.update(endpoint_known=True, endpoint_native=1050)
        item["reference"].update(native_known=True, target=1, generation=9, configuration_generation=3,
                                 native_position=50, source=1, observed_us=100, maximum_age_us=0)
        bench.Console._check_move(item, 1, self.MOVE_ARGS)
        for mutation in ({"observed_us":1001}, {"maximum_age_us":900}, {"generation":10}, {"source":0}):
            bad = copy.deepcopy(item); bad["reference"].update(mutation)
            with self.assertRaises(bench.BenchError): bench.Console._check_move(bad, 1, self.MOVE_ARGS)

    def test_home_optional_age_retains_deadline_and_new_event_checks(self):
        item = home_terminal(2); item["prerequisites"].update(observed_us=0, maximum_age_us=0)
        bench.Console._check_home(item, 1, None)
        for mutate in (
                lambda r: r["prerequisites"].update(observed_us=1001),
                lambda r: r["prerequisites"].update(maximum_age_us=1000),
                lambda r: r.update(deadline_us=1250),
                lambda r: r.update(homed_low_observed=False),
                lambda r: r["prerequisites"].update(qualified_parameters=[35, 60, 30, 101])):
            bad = copy.deepcopy(item); mutate(bad)
            with self.assertRaises(bench.BenchError): bench.Console._check_home(bad, 1, None)

    def test_cached_zero_age_reports_old_checked_evidence_without_rejuvenation(self):
        item = cached_state(2)
        item.update(now_us=31000000, stale_after_ms=0, communication_age_us=30999000)
        for block in item["state_blocks"]:
            block["age_us"] = item["now_us"] - block["observed_earliest_us"]
        bench.Console._check_cached_state(item)
        for mutate in (
                lambda r: r["state_blocks"][0].update(observed_earliest_us=r["now_us"] + 1),
                lambda r: r["state_blocks"][0].update(generation=10),
                lambda r: r["state_blocks"][0].update(valid=False),
                lambda r: r["state_blocks"][0].update(invalidated_us=2000),
                lambda r: r.update(communication_latest_us=r["now_us"] + 1),
                lambda r: r.update(communication_generation=10),
                lambda r: r.update(stale_after_ms=5000)):
            bad = copy.deepcopy(item); mutate(bad)
            with self.assertRaises(bench.BenchError): bench.Console._check_cached_state(bad)
        item.update(stale_after_ms=5000, communication="stale", state="unknown", alarms="unknown")
        for block in item["state_blocks"]: block["fresh"] = False
        bench.Console._check_cached_state(item)

    def test_home_exact_native_arguments_never_send_on_rejection(self):
        console = self.session()
        for tokens in (("3.5", *self.HOME_ARGS[1:]), ("35", "4", "30", "100", "zero"),
                       ("35", "60", "301", "100", "zero"), ("35", "60", "30", "29", "zero"),
                       (*self.HOME_ARGS[:4], "1"), (*self.HOME_ARGS, "extra")):
            with self.assertRaises(ValueError): console.begin("home", home_args=tokens)
        self.assertEqual(len(self.port.writes), 1)
        parsed = bench.arguments(["--port", "FAKE", "--log", "unused", "home", *self.HOME_ARGS, "--cleanup-stop", "normal"])
        self.assertEqual(parsed.home_args, self.HOME_ARGS)
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            bench.arguments(["--port", "FAKE", "--log", "unused", "home", "33", *self.HOME_ARGS[1:], "--cleanup-stop", "normal"])

    def test_home_qualification_gate_is_one_attempt_with_no_cleanup_write(self):
        def handler(i, command, args):
            if command == "home":
                self.assertEqual(tuple(args[:-1]), self.HOME_ARGS)
                return encoded(reply(i, command, ok=False, result="timing_unqualified"))
            return Serial.normal(i, command, args)
        console = self.session(handler)
        with self.assertRaises(bench.BenchError):
            bench.move_campaign(console, command="home", move_args=None, home_args=self.HOME_ARGS,
                                cleanup_stop="normal", timeout_s=3, address=1)
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "home"])
        self.assertEqual(self.events[-1]["homes_attempted"], 1)
        self.assertEqual(self.events[-1]["cleanup"], "not_required")

    def test_home_unresolved_and_unimplemented_are_exact_unadmitted_results(self):
        for method, disposition in (("1", "unimplemented"), ("18", "unresolved")):
            def handler(i, command, args):
                if command != "home": return Serial.normal(i, command, args)
                return encoded(reply(i, command, ok=False, result=disposition, address=1, operation_id=0))
            console = self.session(handler)
            result = console.command("home", home_args=(method, *self.HOME_ARGS[1:]), address=1)
            self.assertFalse(result["ok"])
            self.assertEqual(result["result"], disposition)
            self.assertFalse(console.operations)
            self.assertTrue(console.synchronized)
            self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "home"])

    def test_negative_admission_cannot_invent_disposition_operation_or_target(self):
        for fields in ({"result": "made_up_success"}, {"result": []}, {"result": {}},
                       {"operation_id": 99}, {"operation_id": False},
                       {"address": 2}, {"address": True}):
            def handler(i, command, args):
                if command != "home": return Serial.normal(i, command, args)
                item = reply(i, command, ok=False, result="unresolved", address=1, operation_id=0)
                item.update(fields); return encoded(item)
            console = self.session(handler)
            with self.subTest(fields=fields), self.assertRaises(bench.BenchError):
                console.command("home", home_args=("18", *self.HOME_ARGS[1:]), address=1)
            self.assertFalse(console.synchronized)
            self.assertEqual(len(self.port.writes), 2)

    MOVE_ARGS = ("1000", "steps", "native", "60", "configured")
    def test_home_success_inspection_release_and_bounded_stop_cleanup(self):
        console = self.session(HomeSerial(), fragment=19)
        bench.move_campaign(console, command="home", move_args=None, home_args=self.HOME_ARGS,
                            cleanup_stop="normal", timeout_s=3, address=1)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("home"), 1); self.assertEqual(commands.count("stop"), 1)
        self.assertNotIn("recover", commands); self.assertFalse(console.operations)
        self.assertEqual(self.events[-1]["cleanup"], "drive_reported_standstill")
        self.assertEqual(self.events[-1]["physical_observation"], "not_supplied")

    def test_home_malformed_completion_keeps_unknown_cleanup_without_replay(self):
        console = self.session(HomeSerial(lambda r: r.update(homed_low_observed=False)))
        with self.assertRaises(bench.BenchError):
            bench.move_campaign(console, command="home", move_args=None, home_args=self.HOME_ARGS,
                                cleanup_stop="normal", timeout_s=3, address=1)
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "home"])
        self.assertFalse(console.synchronized); self.assertEqual(self.events[-1]["cleanup"], "unknown")

    def test_home_impossible_admission_motion_is_not_trusted(self):
        for flag in (4, 8, 16, 32, 64):
            record = home_terminal(1)
            record["prerequisites"]["raw_motion"] = flag
            with self.subTest(flag=flag), self.assertRaises(bench.BenchError):
                bench.Console._check_home(record, 1, None)
        # Unknown raw bits are preserved rather than treated as invented faults.
        record = home_terminal(1)
        record["prerequisites"]["raw_motion"] |= 0x8000
        bench.Console._check_home(record, 1, None)

    def test_home_forged_local_failure_cannot_relabel_success(self):
        for outcome, detail in (("cancelled", 22), ("transport_error", 21)):
            record = home_terminal(1)
            record.update(ok=False, state="failed", completion="not_observed", outcome=outcome,
                          uncertain=True, status="ILLEGAL_VALUE", detail=detail)
            record["failure_evidence"] = list(record["zero_evidence"])
            with self.subTest(outcome=outcome), self.assertRaises(bench.BenchError):
                bench.Console._check_home(record, 1, None)

    def test_home_deadline_can_follow_on_time_intermediate_completion(self):
        record = home_terminal(1)
        record.update(ok=False, state="failed", completion="not_observed", outcome="deadline",
                      uncertain=True, status="ILLEGAL_VALUE", detail=20, deadline_us=1350, serviced_us=1600)
        record["zero_evidence"] = [0, 0, "", 0, 0, False, False, False, False, 0, 0, 0, 0, "OK", 0, 0]
        # The qualifying completion closed on time, but delayed delivery leaves
        # no budget to issue its required subsequent zero-pair read.
        record["completion_evidence"][11] = 1600
        record["last_observation"] = list(record["completion_evidence"])
        record["failure_evidence"] = list(record["completion_evidence"])
        bench.Console._check_home(record, 1, None)
        record["serviced_us"] = record["failure_evidence"][11] = 1320
        record["last_observation"][11] = record["completion_evidence"][11] = 1320
        with self.assertRaises(bench.BenchError):
            bench.Console._check_home(record, 1, None)

    def test_driver_exact_grammar_rejects_without_port_traffic(self):
        console = self.session()
        for args in (None, (), ("set",), ("set", "direction", "1", "direction", "0"),
                     ("set", "direction", "1.0"), ("set", "subdivision", "-1"),
                     ("set", "subdivision", "65536"), ("read", "1"), ("set", "unknown", "1"),
                     ("set", "positive-limit", "9223372036854775808")):
            with self.assertRaises(ValueError): console.command("driver", driver_args=args)
        self.assertEqual(len(self.port.writes), 1)
        self.assertEqual(bench.driver_arguments(("set", "positive-limit", "9223372036854775807", "negative-limit", "-9223372036854775808")),
                         {"positive-limit": 2**63 - 1, "negative-limit": -2**63})

    def test_driver_read_update_and_retained_inspection_correlation(self):
        for update in (False, True):
            terminal = None
            def handler(i, command, args):
                nonlocal terminal
                if command == "version": return Serial.normal(i, command, args)
                if command == "driver":
                    self.assertIn(args[0], ("read", "set"))
                    terminal = driver_terminal(i, update)
                    return encoded(reply(i, "driver", result="accepted", address=1, operation_id=i + 100)) + encoded(terminal)
                if command == "result": return encoded({**terminal, "type": "reply", "id": i, "command": "result"})
                return Serial.normal(i, command, args)
            console = self.session(handler)
            args = ("set", "direction", "1") if update else ("read",)
            handle = console.begin("driver", address=1, driver_args=args)
            self.assertEqual(console.wait(handle), terminal)
            inspected = console.command("result", operation_id=handle.operation_id)
            self.assertEqual(inspected["evidence"], terminal["evidence"])
            self.assertIn(b"driver", self.port.writes[1])
            self.assertTrue(console.command("release", operation_id=handle.operation_id)["ok"])

    def test_driver_checked_partial_progress_cannot_be_fabricated(self):
        for update in (False, True):
            item = driver_terminal(2, update)
            bench.Console._check_driver(item, 1, ("set", "direction", "1") if update else ("read",))
            changes = [lambda t: t.update(active_settings_known=True), lambda t: t.update(configuration_generation=0),
                       lambda t: t.update(effects=2), lambda t: t.update(uncertain=True),
                       lambda t: t["evidence"][0].__setitem__(5, "00"), lambda t: t["evidence"][0].__setitem__(13, 0),
                       lambda t: t.update(evidence_columns=[])]
            if update:
                changes += [lambda t: t["progress"][0].__setitem__(4, False), lambda t: t["progress"][0].__setitem__(6, 2),
                            lambda t: t["progress"][0].__setitem__(7, True), lambda t: t["progress"][0].__setitem__(9, "unknown")]
            else:
                changes += [lambda t: t["observation"].update(positive_bits=0), lambda t: t["observation"].update(known_fields=0)]
            for change in changes:
                broken = copy.deepcopy(item); change(broken)
                with self.assertRaises(bench.BenchError): bench.Console._check_driver(broken, 1, None)

    def test_driver_read_campaign_inspects_releases_once_and_never_writes(self):
        terminal = None
        def handler(i, command, args):
            nonlocal terminal
            if command == "driver":
                self.assertEqual(args, ["read", "1"])
                terminal = driver_terminal(i)
                return encoded(reply(i, "driver", result="accepted", address=1, operation_id=i + 100)) + encoded(terminal)
            if command == "result": return encoded({**terminal, "type": "reply", "id": i, "command": "result"})
            return Serial.normal(i, command, args)
        console = self.session(handler)
        bench.driver_read_campaign(console, timeout_s=0.1, address=1)
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "driver", "result", "release"])
        parsed = bench.arguments(["--port", "fake", "--log", "unused.jsonl", "driver-read"])
        self.assertEqual(parsed.mode, "driver-read")
        self.assertEqual(parsed.count, 1)

    def test_driver_unconfirmed_reads_fail_without_publishing_readback(self):
        for update in (False, True):
            item = driver_terminal(2, update)
            item["evidence"][-1][9] = False
            # A valid CRC, address and payload do not confirm the responder.
            with self.assertRaises(bench.BenchError): bench.Console._check_driver(item, 1, None)
            item.update(ok=False, state="failed", outcome="unconfirmed_response", status="ILLEGAL_VALUE", detail=17,
                        completed_steps=len(item["evidence"]) - 1, observation=None)
            if update:
                item["uncertain"] = True
                item["progress"][0][5] = False
                item["progress"][0][6] = 0
            bench.Console._check_driver(item, 1, None)
            if update:
                self.assertTrue(item["progress"][0][4])
                self.assertEqual(item["progress"][0][9], "acknowledged")
                broken = copy.deepcopy(item)
                broken["progress"][0][5] = True
                broken["progress"][0][6] = 1
                broken["uncertain"] = False
                with self.assertRaises(bench.BenchError): bench.Console._check_driver(broken, 1, None)
    VELOCITY_ARGS = ("60", "rpm", "native", "500", "configured", "normal")

    def test_velocity_strict_correlation_retention_and_single_cleanup(self):
        console = self.session(VelocitySerial(), fragment=19)
        events = []; console.emit = lambda event, **data: events.append((event, data))
        bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS, cleanup_stop="normal", timeout_s=3, address=1)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("velocity"), 1); self.assertEqual(commands.count("stop"), 1)
        self.assertNotIn("recover", commands); self.assertFalse(console.operations)
        self.assertEqual(events[-1][1]["cleanup"], "drive_reported_standstill")
        self.assertEqual(events[-1][1]["physical_observation"], "not_supplied")
        self.assertEqual(events[-1][1]["velocities_attempted"], 1)

    def test_velocity_rejection_and_poison_never_replay(self):
        def rejected(i, command, args):
            if command == "velocity": return encoded(reply(i, command, ok=False, result="timing_unqualified"))
            return Serial.normal(i, command, args)
        console = self.session(rejected)
        with self.assertRaises(bench.BenchError):
            bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS, cleanup_stop="fast", timeout_s=3, address=1)
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
        t["uncertain"] = False
        with self.assertRaisesRegex(bench.BenchError, "uncertainty"):
            bench.Console._check_velocity(t, 1, self.VELOCITY_ARGS)

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
        parsed = bench.arguments(["--port", "FAKE", "--log", "unused", "velocity", "60", "rpm", "native", "500", "configured", "normal", "--cleanup-stop", "fast"])
        self.assertEqual(parsed.velocity_args, self.VELOCITY_ARGS); self.assertEqual(parsed.cleanup_stop, "fast")

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
        t["needs_stop"] = False
        with self.assertRaisesRegex(bench.BenchError, "stop obligation"):
            bench.Console._check_velocity(t, 1, self.VELOCITY_ARGS)

    def test_velocity_host_timeout_is_finite_and_retains_unknown_stop(self):
        console = self.session(VelocitySerial(admission_only=True))
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaises(bench.BenchError):
            bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS, cleanup_stop="normal", timeout_s=1, address=1)
        self.assertLess(self.clock.now, 1.1)
        self.assertFalse(console.synchronized); self.assertEqual(events[-1][1]["cleanup"], "unknown")
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "velocity"])

    def test_velocity_clean_idle_interrupt_requests_one_stop_and_routes_original_terminal(self):
        fixture = VelocitySerial(admission_only=True)
        def handler(request_id, command, args):
            response = fixture(request_id, command, args)
            if command == "stop":
                # The interrupted velocity still owns its original correlation;
                # its terminal may arrive interleaved with the cleanup stop.
                return encoded(fixture.retained[102]) + response
            return response
        console = self.session(handler, fragment=19)
        interrupted = False
        def sleep(delay):
            nonlocal interrupted
            if not interrupted:
                interrupted = True
                raise KeyboardInterrupt()
            self.clock.sleep(delay)
        console.sleep = sleep
        with self.assertRaises(KeyboardInterrupt):
            bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS,
                                    cleanup_stop="normal", timeout_s=3, address=1)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("velocity"), 1)
        self.assertEqual(commands.count("stop"), 1)
        self.assertNotIn("recover", commands)
        self.assertTrue(console.synchronized)
        self.assertFalse(console.operations)
        summary = self.events[-1]
        self.assertEqual(summary["cleanup"], "drive_reported_standstill")
        self.assertEqual(summary["velocity_result"]["operation_id"], 102)
        self.assertEqual(summary["error"], "interrupted")
        self.assertFalse(summary["ok"])

    def test_velocity_interrupted_serial_read_keeps_unknown_cleanup(self):
        console = self.session(VelocitySerial(admission_only=True))
        original = self.port.read
        def interrupted_read(limit):
            if any(handle.accepted for handle in console.pending.values()):
                raise KeyboardInterrupt()  # No assertion that the OS consumed zero bytes.
            return original(limit)
        self.port.read = interrupted_read
        with self.assertRaises(KeyboardInterrupt):
            bench.velocity_campaign(console, velocity_args=self.VELOCITY_ARGS,
                                    cleanup_stop="normal", timeout_s=3, address=1)
        self.assertFalse(console.synchronized)
        self.assertEqual([line.decode().split()[1] for line in self.port.writes], ["version", "velocity"])
        self.assertEqual(self.events[-1]["cleanup"], "unknown")

    def test_velocity_rejects_impossible_precision_tokens_and_failure_records(self):
        for change in (
                lambda t: t.update(exact_arithmetic=False),
                lambda t: t.update(requested_rpm_approximate=60),
                lambda t: t.update(ok=False, state="failed", outcome="transport_error", status="ILLEGAL_VALUE", detail=18),
                lambda t: t["activity_evidence"].update(step=0),
                lambda t: t["last_observation"].update(step=255),
                lambda t: t["last_observation"].update(latest_us=3049),
                lambda t: t["stop_write_evidence"].update(step=1),
                lambda t: t["stop_observation"].update(step=0),
                lambda t: t["stop_write_evidence"].update(earliest_us=1100, latest_us=1150, delivered_us=1200),
                lambda t: t["failure_evidence"].update(event=3, delivered_us=503100, status="ILLEGAL_VALUE", detail=17)):
            terminal = velocity_terminal(2)
            change(terminal)
            with self.assertRaises(bench.BenchError):
                bench.Console._check_velocity(terminal, 1, self.VELOCITY_ARGS)

    def test_velocity_local_time_and_missing_activity_failures_have_no_fabricated_frame(self):
        terminal = velocity_terminal(2)
        terminal.update(ok=False, state="failed", outcome="deadline", status="ILLEGAL_VALUE", detail=24,
                        uncertain=True, service_missed=True)
        bench.Console._check_velocity(terminal, 1, self.VELOCITY_ARGS)
        terminal = velocity_terminal(2)
        terminal.update(ok=False, state="failed", outcome="observation_limit", status="ILLEGAL_VALUE", detail=25,
                        uncertain=True, running_observed=False, raw_motion=1)
        terminal["activity_evidence"] = dict(terminal["failure_evidence"])
        raw = bytes((1, 3, 4, 0, 0, 0, 1))
        terminal["last_observation"]["raw_hex"] = (raw + bench.wire_crc(raw).to_bytes(2, "little")).hex()
        bench.Console._check_velocity(terminal, 1, self.VELOCITY_ARGS)

    def test_velocity_unsent_trigger_expiry_preserves_staging_without_fake_reply(self):
        terminal = velocity_terminal(2)
        terminal.update(ok=False, state="failed", outcome="deadline", status="ILLEGAL_VALUE", detail=17,
                        phase=1, serviced_us=501000, execution="not_transmitted", uncertain=True,
                        completion="not_observed", needs_stop=False, running_observed=False,
                        observation_known=False, raw_alarm=None, raw_motion=None, polls=0,
                        stop_execution="not_transmitted", stop_completion="not_observed", stop_outcome="none")
        empty = terminal["failure_evidence"]
        for name in ("trigger_evidence", "activity_evidence", "stop_write_evidence", "stop_observation"):
            terminal[name] = dict(empty)
        terminal["last_observation"] = {key: empty[key] for key in
                                       ("step", "raw_hex", "earliest_us", "latest_us", "delivered_us")}
        bench.Console._check_velocity(terminal, 1, self.VELOCITY_ARGS)
        terminal["serviced_us"] -= 1
        with self.assertRaisesRegex(bench.BenchError, "failure lacks terminal evidence"):
            bench.Console._check_velocity(terminal, 1, self.VELOCITY_ARGS)

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

    def test_move_setup_policies_and_checked_partial_writes(self):
        import copy
        for setup, changes, selected in (("stored", [], (0, 0)), ("verify", [], (0, 0)),
                ("verify", [0], (0, 1)), ("verify", [1], (1, 1)), ("verify", [2], (2, 1)),
                ("verify", [3], (3, 2)), ("verify", [4], (3, 2)), ("verify", [0, 2], (0, 5))):
            t = move_terminal(2)
            empty = dict(t["failure_evidence"])
            t.update(setup=setup, trigger_step=2 if setup == "verify" else 1,
                     setup_offset=selected[0], setup_count=selected[1], verification_known=setup == "verify",
                     verification_words=[0] * 5, verification_raw_hex="", verification_evidence=dict(empty))
            if setup == "verify":
                before = list(t["staging_words"])
                for i in changes: before[i] += 1
                raw = bytes((1, 3, 10)) + b"".join(word.to_bytes(2, "big") for word in before)
                raw += bench.wire_crc(raw).to_bytes(2, "little")
                t.update(verification_words=before, verification_raw_hex=raw.hex(),
                         verification_evidence=dict(t["staging_evidence"], raw_hex=raw[:9].hex(),
                             received_length=15, tx_accepted=8, earliest_us=1010, latest_us=1020, delivered_us=1030))
                for name in ("staging_evidence", "trigger_evidence", "activity_evidence", "last_observation"):
                    t[name]["step"] += 1
            if selected[1] == 0:
                t.update(staging_applied=False, setup_execution="not_transmitted", staging_evidence=dict(empty))
            else:
                offset, count = selected
                value = t["staging_words"][offset]
                prefix = bytes((1, 6, 0, 0x21 + offset, value >> 8, value & 255)) if count == 1 else bytes((1, 16, 0, 0x21 + offset, 0, count))
                t["staging_evidence"].update(raw_hex=(prefix + bench.wire_crc(prefix).to_bytes(2, "little")).hex(),
                                             tx_accepted=8 if count == 1 else 13 if count == 2 else 19)
            args = self.MOVE_ARGS + ("setup", setup)
            bench.Console._check_move(t, 1, args)
            bad = copy.deepcopy(t)
            bad["trigger_step"] = 3
            with self.assertRaises(bench.BenchError): bench.Console._check_move(bad, 1, args)
            if setup == "verify":
                for key, value in (("verification_raw_hex", t["verification_raw_hex"][:-2] + "ff"),
                                   ("verification_known", False), ("setup_count", 4)):
                    bad = copy.deepcopy(t); bad[key] = value
                    with self.assertRaises(bench.BenchError): bench.Console._check_move(bad, 1, args)
        for tokens in (("setup",), ("setup", "unknown"), ("setup", "verify", "setup", "stored")):
            with self.assertRaises(ValueError): bench.move_arguments("relative", self.MOVE_ARGS + tokens)

    def test_move_verification_timeout_is_read_failure_without_write_uncertainty(self):
        t = move_terminal(2)
        empty = dict(t["failure_evidence"])
        failure = dict(empty, event=1, tx_accepted=8, tx_complete=True, delivered_us=1100,
                       status="ILLEGAL_VALUE", detail=1)
        t.update(setup="verify", trigger_step=2, setup_offset=0, setup_count=5,
                 verification_known=False, verification_words=[0] * 5, verification_raw_hex="",
                 verification_evidence=failure, failure_evidence=dict(failure),
                 ok=False, state="failed", outcome="transport_error", status="ILLEGAL_VALUE", detail=1,
                 setup_execution="not_transmitted", execution="not_transmitted", completion="not_observed",
                 staging_applied=False, uncertain=False, running_observed=False, observation_known=False,
                 raw_alarm=None, raw_motion=None, serviced_us=1100, polls=0)
        for key in ("staging_evidence", "trigger_evidence", "activity_evidence", "last_observation"):
            t[key] = dict(empty)
        bench.Console._check_move(t, 1, self.MOVE_ARGS + ("setup", "verify"))
        t["uncertain"] = True
        with self.assertRaisesRegex(bench.BenchError, "uncertainty"):
            bench.Console._check_move(t, 1, self.MOVE_ARGS + ("setup", "verify"))

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

    def test_unreferenced_absolute_keeps_displacement_and_reference_unknown(self):
        item = move_terminal(2)
        item.update(command="move-absolute", move_kind="absolute", effective_native=0, endpoint_native=0,
            endpoint_known=True, displacement_known=False, displacement_native=0,
            execution="unknown")
        item["staging_words"][3:] = [0, 0]
        item["requested"].update(relative=False, numerator=0)
        raw = bytes((1, 6, 0, 0x27, 0, 5))
        item["trigger_evidence"].update(raw_hex=(raw + bench.wire_crc(raw).to_bytes(2, "little")).hex(),
                                        response_confirmed=False)
        arguments = ("0", "steps", "native", "60", "configured")
        bench.Console._check_move(item, 1, arguments, "absolute")
        mutations = [lambda t: t.pop("displacement_known"), lambda t: t.update(displacement_known=True),
            lambda t: t.update(displacement_known=0), lambda t: t.update(displacement_native=-99),
            lambda t: t.update(endpoint_known=False), lambda t: t.update(zero_displacement=True),
            lambda t: t.update(effective_native=1), lambda t: t.update(endpoint_native=1),
            lambda t: t["requested"].update(numerator=1), lambda t: t["requested"].update(frame=1),
            lambda t: t["requested"].update(unit="fullsteps"), lambda t: t["requested"].update(relative=True),
            lambda t: t["reference"].update(native_known=True), lambda t: t["reference"].update(native_position=99),
            lambda t: t["reference"].update(source=3), lambda t: t["reference"].update(observed_us=900),
            lambda t: t["activity_evidence"].update(response_confirmed=False),
            lambda t: t["last_observation"].update(response_confirmed=False),
            lambda t: t["staging_evidence"].update(response_confirmed=False)]
        for mutate in mutations:
            bad = copy.deepcopy(item); mutate(bad)
            with self.subTest(mutation=mutate):
                with self.assertRaises(bench.BenchError): bench.Console._check_move(bad, 1, arguments, "absolute")
        for kind in ("relative", "angle"):
            with self.assertRaises(bench.BenchError): bench.Console._check_move(item, 1, None, kind)
        ordinary = move_terminal(2); ordinary["displacement_known"] = False
        with self.assertRaises(bench.BenchError): bench.Console._check_move(ordinary, 1, self.MOVE_ARGS)

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
            "--round", "nearest", "--maximum-error", "1", "--approximation-error", "0.001", "--cleanup-stop", "fast"])
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
        self.assertEqual(events[-1][1]["cleanup"], "drive_reported_standstill")
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
        self.assertEqual(summary["cleanup"], "drive_reported_standstill")
        self.assertEqual(summary["physical_observation"], "not_supplied")
        self.assertTrue(summary["ok"])

    def test_move_campaign_rejection_sends_no_cleanup_motor_command(self):
        def rejected(i, command, args):
            if command == "move": return encoded(reply(i, "move-relative", ok=False, result="timing_unqualified"))
            return Serial.normal(i, command, args)
        console = self.session(rejected)
        events = []; console.emit = lambda event, **data: events.append((event, data))
        with self.assertRaises(bench.BenchError):
            bench.move_campaign(console, move_args=self.MOVE_ARGS, cleanup_stop="fast", timeout_s=3, address=1)
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

    def test_debug_commands_and_nonconsuming_interleaved_frames(self):
        mode="off"
        def handler(request_id, command, args):
            nonlocal mode
            if command=="debug":
                if args: mode=args[0]
                return encoded(reply(request_id,"debug",mode=mode,observed=2,emitted=2,dropped=0,cursor=2,
                    overwritten=0,capture_dropped=0,retained=2,capacity=16))
            normal=Serial.normal(request_id,command,args)
            if command=="probe":
                accepted,terminal=normal.splitlines(keepends=True)
                return accepted+encoded(traffic_record())+encoded(traffic_record(2,"RX"))+terminal
            return normal
        console=self.session(handler,fragment=17)
        self.assertEqual(console.command("debug",host_args=("decoded",))["mode"],"decoded")
        result=console.command("probe",address=1)
        self.assertTrue(result["ok"])
        self.assertEqual(len([e for e in self.events if e["event"]=="traffic"]),2)
        self.assertFalse(console.operations)
        self.assertEqual(console.command("debug",host_args=())["mode"],"decoded")
        self.assertEqual(console.command("debug",host_args=("off",))["mode"],"off")
        before=len(self.port.writes)
        for args in (("invalid",),("raw","off"),["raw"],("decoded\n",)):
            with self.assertRaises(ValueError): console.command("debug",host_args=args)
        self.assertEqual(len(self.port.writes),before)

    def test_debug_cached_snapshot_and_independent_loss_counters(self):
        good = reply(2,"debug",mode="raw",observed=3,emitted=2,dropped=1,missed=4,skipped=5,cursor=12,
                     overwritten=4,capture_dropped=0,retained=3,capacity=16)
        for mutate in (lambda x:x.update(missed=3), lambda x:x.update(dropped=2),
                lambda x:x.update(skipped=-1), lambda x:x.pop("owner"),
                lambda x:x["owner"].update(busy=1), lambda x:x["owner"].update(phase="running"),
                lambda x:x["capture"].update(faults=-1), lambda x:x["capture"].update(mode="unknown"),
                lambda x:x["memory"].update(valid=1), lambda x:x["memory"].update(internal_free=-1)):
            bad=copy.deepcopy(good); mutate(bad)
            def handler(i,cmd,args):
                if cmd=="debug": return encoded(dict(bad,id=i))
                return Serial.normal(i,cmd,args)
            console=self.session(handler)
            with self.assertRaises(bench.BenchError): console.command("debug",host_args=("raw",))
        def valid(i,cmd,args):
            return encoded(dict(good,id=i)) if cmd=="debug" else Serial.normal(i,cmd,args)
        console=self.session(valid)
        self.assertEqual(console.command("debug",host_args=("raw",))["missed"],4)

    def test_debug_session_uses_regular_path_and_restores_previous_mode(self):
        mode="raw"
        def handler(i,cmd,args):
            nonlocal mode
            if cmd=="debug":
                if args: mode=args[0]
                return encoded(reply(i,cmd,mode=mode,observed=0,emitted=0,dropped=0,cursor=0,
                                     overwritten=0,capture_dropped=0,retained=0,capacity=16))
            return Serial.normal(i,cmd,args)
        console=self.session(handler)
        with bench.debug_session(console,"decoded",.1) as evidence:
            result=console.command("probe",address=1)
            self.assertTrue(result["ok"])
        self.assertEqual(mode,"raw")
        self.assertEqual(evidence["cleanup"],"restored")
        commands=[line.decode().split()[1:] for line in self.port.writes]
        self.assertEqual(commands,[['version'],['debug'],['debug','decoded'],['probe','1'],['release','104'],['debug'],['debug','raw']])
        before=len(self.port.writes)
        with bench.debug_session(console,None,.1): pass
        self.assertEqual(len(self.port.writes),before)

    def test_debug_session_preserves_operation_error_if_display_cleanup_fails(self):
        mode="off"; queries=0
        def handler(i,cmd,args):
            nonlocal mode,queries
            if cmd!="debug": return Serial.normal(i,cmd,args)
            queries+=1
            if queries>2: return encoded(reply(i,cmd,ok=False,result="unavailable"))
            if args: mode=args[0]
            return encoded(reply(i,cmd,mode=mode,observed=0,emitted=0,dropped=0,cursor=0,
                                 overwritten=0,capture_dropped=0,retained=0,capacity=16))
        console=self.session(handler)
        with self.assertRaisesRegex(bench.BenchError,"original operation failure"):
            with bench.debug_session(console,"decoded",.1) as evidence:
                raise bench.BenchError("original operation failure")
        self.assertEqual(evidence["cleanup"],"failed")
        self.assertIn("debug selection",evidence["cleanup_error"])
        self.assertFalse(any(b"probe" in line for line in self.port.writes))

    def test_debug_session_does_not_send_cleanup_into_untrusted_stream(self):
        def handler(i,cmd,args):
            if cmd!="debug": return Serial.normal(i,cmd,args)
            return encoded(reply(i,cmd,mode=args[0] if args else "off",observed=0,emitted=0,dropped=0,cursor=0,
                                 overwritten=0,capture_dropped=0,retained=0,capacity=16))
        console=self.session(handler)
        with bench.debug_session(console,"raw",.1) as evidence:
            console.synchronized=False
        self.assertEqual(evidence["cleanup"],"skipped_unsynchronized")
        self.assertEqual(len(self.port.writes),3)

    def test_debug_session_restores_after_synchronized_final_query_refusal(self):
        mode="off"; queries=0
        def handler(i,cmd,args):
            nonlocal mode,queries
            if cmd!="debug": return Serial.normal(i,cmd,args)
            queries+=1
            if queries==3: return encoded(reply(i,cmd,ok=False,result="unavailable"))
            if args: mode=args[0]
            return encoded(reply(i,cmd,mode=mode,observed=0,emitted=0,dropped=0,cursor=0,
                                 overwritten=0,capture_dropped=0,retained=0,capacity=16))
        console=self.session(handler)
        with self.assertRaisesRegex(bench.BenchError,"debug selection/query failed"):
            with bench.debug_session(console,"decoded",.1) as evidence:
                pass
        self.assertEqual(mode,"off")
        self.assertEqual(evidence["restored"]["mode"],"off")
        self.assertEqual(evidence["cleanup"],"failed")
        self.assertEqual([line.decode().split()[1:] for line in self.port.writes],
                         [['version'],['debug'],['debug','decoded'],['debug'],['debug','off']])

    def test_debug_session_evidence_failure_preserves_primary_or_first_cleanup_error(self):
        for failure in ("body", "query", "none"):
            queries=0
            def handler(i,cmd,args):
                nonlocal queries
                if cmd!="debug": return Serial.normal(i,cmd,args)
                queries+=1
                if failure=="query" and queries==3:
                    return encoded(reply(i,cmd,ok=False,result="unavailable"))
                return encoded(reply(i,cmd,mode=args[0] if args else "off",observed=0,emitted=0,dropped=0,cursor=0,
                                     overwritten=0,capture_dropped=0,retained=0,capacity=16))
            console=self.session(handler)
            def failing_sink(event,**fields):
                if event=="debug_session": raise OSError("evidence sink unavailable")
            console.emit=failing_sink
            expected="primary operation" if failure=="body" else "debug selection/query" if failure=="query" else "evidence sink"
            with self.subTest(failure=failure), self.assertRaisesRegex((bench.BenchError,OSError),expected):
                with bench.debug_session(console,"raw",.1) as evidence:
                    if failure=="body": raise bench.BenchError("primary operation")
            self.assertEqual(evidence["evidence_error"],"evidence sink unavailable")
            self.assertEqual(evidence["restored"]["mode"],"off")
            self.assertEqual(len(self.port.writes),5)

    def test_debug_cli_selection_is_optional_and_no_old_alias(self):
        prefix=["--port","fake","--log","unused.jsonl"]
        self.assertIsNone(bench.arguments(prefix+["probe"]).debug)
        self.assertEqual(bench.arguments(prefix+["--debug","decoded","probe"]).debug,"decoded")
        self.assertEqual(bench.arguments(prefix+["debug","raw"]).host_args,("raw",))
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit): bench.arguments(prefix+["--debug","raw","debug","off"])
            with self.assertRaises(SystemExit): bench.arguments(prefix+["sniff","raw"])
        console=self.session()
        with self.assertRaises(ValueError): console.command("sniff",host_args=("raw",))

    def test_debug_invalid_translation_cannot_publish_invented_values(self):
        good=traffic_record(2,"RX")
        for mutate in (lambda x:x.update(raw_hex=x["raw_hex"][:-4]+"0000"),
                lambda x:x.update(expected_request_hex=""),lambda x:x.update(expected_request_sequence=2),
                lambda x:x.update(complete=False),lambda x:x.update(length=8),
                lambda x:x.update(sequence=0),lambda x:x.update(id=1),
                lambda x:x.update(mode="off"),lambda x:x.update(decode_detail=1),
                lambda x:x["decoded"].update(words=[99]),lambda x:x["decoded"].update(register_start=1),
                lambda x:x["decoded"].update(function_name="write_register")):
            console=self.session(); bad=copy.deepcopy(good); mutate(bad)
            with self.subTest(mutation=mutate):
                with self.assertRaises(bench.BenchError): console._dispatch(bad)
        console=self.session()
        bad=copy.deepcopy(good); bad.update(raw_hex=bad["raw_hex"][:-4]+"0000",decode_status="CRC_ERROR",decoded=None,frame_error=7)
        console._dispatch(bad)
        self.assertEqual(self.events[-1]["response"]["raw_hex"],bad["raw_hex"])
        with self.assertRaises(bench.BenchError): console._dispatch(bad)

    def test_interleaved_replies_do_not_exhaust_another_command_budget(self):
        retained = []
        def handler(i, cmd, args):
            normal = Serial.normal(i, cmd, args)
            if cmd == "probe":
                accepted, terminal = normal.splitlines(keepends=True)
                retained.append(terminal)
                return accepted
            if cmd == "status":
                item = json.loads(normal)
                item["padding"] = "x" * 4000
                return encoded(item)
            return normal
        console = self.session(handler)
        pending = console.begin("probe", address=1, timeout_s=10)
        initial = pending.input_bytes
        for _ in range(12):
            self.assertTrue(console.command("status")["ok"])
        self.assertEqual(pending.input_bytes, initial)
        self.port.input.extend(retained[0])
        self.assertTrue(console.wait(pending, release=True)["ok"])

    def test_debug_noise_budget_is_separate_from_command_response(self):
        def handler(request_id,command,args):
            normal=Serial.normal(request_id,command,args)
            if command!="probe": return normal
            accepted,terminal=normal.splitlines(keepends=True)
            records=[]
            for sequence in range(1,61):
                item=traffic_record(sequence,mode="raw")
                item.update(complete=False,length=256,raw_hex="ff"*256)
                records.append(encoded(item))
            self.assertGreater(sum(map(len,records)),bench.MAX_INPUT)
            return accepted+b"".join(records)+terminal
        console=self.session(handler)
        result=console.command("probe",address=1)
        self.assertTrue(result["ok"])
        self.assertEqual(len([e for e in self.events if e["event"]=="traffic"]),60)
        self.assertFalse(console.operations)

    def test_partial_traffic_line_is_completed_before_next_command(self):
        console=self.session()
        raw=encoded(traffic_record(mode="raw"))
        split=60
        console.buffer.extend(raw[:split])
        original_sleep=self.clock.sleep
        sent=False
        def sleep(delay):
            nonlocal sent
            original_sleep(delay)
            if not sent:
                self.port.input.extend(raw[split:]); sent=True
        console.sleep=sleep
        result=console.command("probe",address=1,timeout_s=.1)
        self.assertTrue(result["ok"] and console.synchronized)
        self.assertEqual(console.traffic_sequence,1)
        self.assertEqual(len([e for e in self.events if e["event"]=="traffic"]),1)

    def test_debug_counter_regression_rejected_across_mode_changes(self):
        def handler(request_id,command,args):
            if command!="debug": return Serial.normal(request_id,command,args)
            return encoded(reply(request_id,"debug",mode=args[0],observed=2 if args[0]=="raw" else 1,
                emitted=2 if args[0]=="raw" else 1,dropped=0,cursor=2 if args[0]=="raw" else 1,
                overwritten=0,capture_dropped=0,retained=2,capacity=16))
        console=self.session(handler)
        console.command("debug",host_args=("raw",))
        with self.assertRaises(bench.BenchError): console.command("debug",host_args=("off",))

    def test_motion_profile_commands_retain_snapshot_and_restore_proof(self):
        restored = False
        def handler(request_id, command, args):
            nonlocal restored
            if command != "motion-profile": return Serial.normal(request_id, command, args)
            if args[0] == "restore": restored = True
            return encoded(motion_profile_reply(request_id, args[0], restored, args[0] != "inspect"))
        console = self.session(handler)
        for action in ("read", "inspect", "restore", "inspect"):
            result = console.command("motion-profile", host_args=(action,))
            self.assertEqual(result["pending"], action != "inspect")
        self.assertTrue(result["restored"])
        self.assertEqual([line.decode().split()[1:] for line in self.port.writes][1:],
                         [["motion-profile", x] for x in ("read", "inspect", "restore", "inspect")])

    def test_motion_profile_rejects_false_success_and_malformed_evidence(self):
        mutations = [lambda x: x.update(request="read"), lambda x: x.update(tx_complete=False),
            lambda x: x.update(tx_accepted=7), lambda x: x.update(session_ok=1),
            lambda x: x.update(pending=True), lambda x: x.update(phase=2),
            lambda x: x.update(rx_hex=x["rx_hex"][:-4] + "0000"),
            lambda x: x.update(current=[0]*6), lambda x: x.update(restored=False),
            lambda x: x.update(write_tx_accepted=18), lambda x: x.update(write_tx_complete=False),
            lambda x: x.update(write_reply_hex=x["write_reply_hex"][:-4] + "0000"),
            lambda x: x.update(write_execution_unknown=True), lambda x: x.update(execution_unknown=True),
            lambda x: x.update(closure_latest_us=10001, delivered_us=10002),
            lambda x: x.update(write_closure_latest_us=10001, write_delivered_us=10002),
            lambda x: x.update(closure_earliest_us=1200)]
        for mutate in mutations:
            def handler(request_id, command, args):
                if command != "motion-profile": return Serial.normal(request_id, command, args)
                item = motion_profile_reply(request_id, restored=True); mutate(item); return encoded(item)
            console = self.session(handler)
            with self.subTest(mutation=mutate):
                with self.assertRaises(bench.BenchError): console.command("motion-profile", host_args=("inspect",))
                self.assertFalse(console.synchronized)

    def test_motion_profile_grammar_rejects_before_transmission(self):
        console = self.session()
        before = len(self.port.writes)
        for tokens in (None, (), ("read", "1"), ("write",), ("restore\n",), ["read"]):
            with self.assertRaises(ValueError): console.command("motion-profile", host_args=tokens)
        self.assertEqual(before, len(self.port.writes))

    def test_motion_profile_unknown_restore_can_only_reconcile_with_read_only_evidence(self):
        attempt = motion_profile_reply(0, restored=True, pending=True)
        attempt.update(pending=False, error="transaction", execution_unknown=True, restore_unsettled=True,
            tx_accepted=19, tx_complete=True, delivered_us=2000, write_tx_hex=attempt["tx_hex"],
            write_tx_accepted=19, write_tx_complete=True, write_delivered_us=2000, write_execution_unknown=True)
        stage = 0
        def handler(i, command, args):
            nonlocal stage
            if command != "motion-profile": return Serial.normal(i, command, args)
            action = args[0]
            if stage == 0 or action in ("forget", "restore"):
                item = copy.deepcopy(attempt)
                if action in ("forget", "restore"): item.update(ok=False, result="busy")
                stage = 1
            else:
                item = motion_profile_reply(i, action, restored=True)
                for key in attempt:
                    if key.startswith("write_"): item[key] = copy.deepcopy(attempt[key])
                item.update(deadline_us=20000, execution_unknown=True, restore_unsettled=stage < 4,
                    closure_earliest_us=6000, closure_latest_us=6200, delivered_us=6300, serial_generation=2)
                if stage >= 5:
                    item.update(phase=1, restored=False)
                if action == "read":
                    item.update(pending=True, session_ok=False, restored=False, rx_hex="", tx_accepted=0,
                        tx_complete=False, closure_qualified=False, closure_earliest_us=0,
                        closure_latest_us=0, delivered_us=0)
                elif stage == 2:
                    item.update(session_ok=False, restored=False, error="stationary_required")
                stage += 1
            item.update(id=i, request=action)
            return encoded(item)
        console = self.session(handler)
        original = console.command("motion-profile", host_args=("inspect",))
        for action in ("forget", "restore"):
            refused = console.command("motion-profile", host_args=(action,))
            self.assertFalse(refused["ok"])
            self.assertTrue(refused["restore_unsettled"])
            self.assertEqual(refused["write_tx_hex"], original["write_tx_hex"])
        first_read = console.command("motion-profile", host_args=("read",))
        self.assertEqual(first_read["phase"], 3)
        self.assertEqual(first_read["serial_generation"], 2)
        self.assertEqual(first_read["write_serial_generation"], 1)
        stationary = console.command("motion-profile", host_args=("inspect",))
        self.assertEqual(stationary["error"], "stationary_required")
        self.assertTrue(stationary["restore_unsettled"])
        console.command("motion-profile", host_args=("read",))
        settled = console.command("motion-profile", host_args=("inspect",))
        self.assertTrue(settled["session_ok"])
        self.assertTrue(settled["restored"])
        self.assertFalse(settled["restore_unsettled"])
        self.assertTrue(settled["write_execution_unknown"])
        self.assertTrue(settled["execution_unknown"])
        self.assertEqual(settled["write_reply_hex"], "")
        self.assertEqual(settled["write_deadline_us"], 10000)
        self.assertEqual(settled["deadline_us"], 20000)
        refreshed = console.command("motion-profile", host_args=("read",))
        self.assertEqual(refreshed["phase"], 1)
        self.assertEqual(refreshed["write_tx_hex"], original["write_tx_hex"])
        refreshed = console.command("motion-profile", host_args=("inspect",))
        self.assertTrue(refreshed["session_ok"])
        self.assertFalse(refreshed["restored"])
        self.assertTrue(refreshed["write_execution_unknown"])
        self.assertTrue(refreshed["execution_unknown"])
        self.assertEqual([line.decode().split()[1:] for line in self.port.writes][1:],
            [["motion-profile", action] for action in ("inspect", "forget", "restore", "read", "inspect", "read", "inspect", "read", "inspect")])

    def test_motion_profile_retained_write_context_cannot_change_or_disappear(self):
        mutations = [dict(write_deadline_us=11000), dict(write_configuration_generation=4),
            dict(write_serial_generation=2), dict(write_binding_generation=10),
            dict(serial_generation=2),
            dict(write_tx_accepted=18, write_tx_complete=False), dict(write_delivered_us=1500),
            dict(write_execution_unknown=True, execution_unknown=True), dict(restore_unsettled=True, saved=False)]
        for fields in mutations:
            calls = 0
            def handler(i, command, args):
                nonlocal calls
                if command != "motion-profile": return Serial.normal(i, command, args)
                item = motion_profile_reply(i, restored=True)
                if calls: item.update(fields)
                calls += 1
                return encoded(item)
            console = self.session(handler)
            console.command("motion-profile", host_args=("inspect",))
            with self.subTest(fields=fields), self.assertRaises(bench.BenchError):
                console.command("motion-profile", host_args=("inspect",))
            self.assertFalse(console.synchronized)
            self.assertEqual(len(self.port.writes), 3)

    def test_motion_profile_new_read_resets_only_previous_read_uncertainty(self):
        def handler(i, command, args):
            if command != "motion-profile": return Serial.normal(i, command, args)
            item = motion_profile_reply(i, args[0], restored=True)
            item.update(phase=1, restored=False, deadline_us=20000)
            if args[0] == "inspect":
                item.update(session_ok=False, error="transaction", execution_unknown=True, rx_hex="",
                    closure_qualified=False, closure_earliest_us=0, closure_latest_us=0)
            else:
                item.update(pending=True, session_ok=False, tx_accepted=0, tx_complete=False, rx_hex="",
                    closure_qualified=False, closure_earliest_us=0, closure_latest_us=0, delivered_us=0)
            return encoded(item)
        console = self.session(handler)
        failed_read = console.command("motion-profile", host_args=("inspect",))
        self.assertTrue(failed_read["execution_unknown"])
        self.assertFalse(failed_read["write_execution_unknown"])
        refresh = console.command("motion-profile", host_args=("read",))
        self.assertFalse(refresh["execution_unknown"])
        self.assertFalse(refresh["write_execution_unknown"])
        self.assertEqual(refresh["write_tx_hex"], failed_read["write_tx_hex"])
        self.assertEqual(refresh["write_deadline_us"], failed_read["write_deadline_us"])
        self.assertEqual(len(self.port.writes), 3)

    def test_wiring_query_and_all_terminal_declarations_are_local(self):
        inputs, outputs = [0] * 4, [0] * 2
        def handler(i, command, args):
            if command != "wiring": return Serial.normal(i, command, args)
            if args:
                terminal, disposition = bench.wiring_arguments(tuple(args))
                (inputs if terminal[0] == "x" else outputs)[int(terminal[1])] = disposition
            return encoded(reply(i, command, result="done", target=17, address=1, generation=9,
                configuration_generation=3, inputs=inputs, outputs=outputs, bus_traffic=False))
        console = self.session(handler)
        self.assertEqual(console.command("wiring", host_args=())["inputs"], [0] * 4)
        for terminal in ("x0", "x1", "x2", "x3", "y0", "y1"):
            for disposition in ("unknown", "unconnected", "connected"):
                result = console.command("wiring", host_args=(terminal, disposition))
                states = result["inputs" if terminal[0] == "x" else "outputs"]
                self.assertEqual(states[int(terminal[1])], ("unknown", "unconnected", "connected").index(disposition))
        self.assertFalse(console.operations)
        self.assertEqual(len(self.port.writes), 20)  # Version plus 19 local commands; no read/release.
        self.assertTrue(all(line.decode().split()[1] == "wiring" for line in self.port.writes[1:]))

    def test_wiring_grammar_rejects_before_transmission(self):
        console = self.session()
        before = len(self.port.writes)
        for tokens in (("x0",), ("x0", "unknown", "1"), ("x4", "unknown"), ("y2", "connected"),
                       ("X0", "unknown"), ("x0", "disabled"), ("x0", "1"), ("x0", "connected\nprobe"),
                       ("x0", False), (False, "unknown"), ["x0", "unknown"]):
            with self.subTest(tokens=tokens), self.assertRaises(ValueError):
                console.command("wiring", host_args=tokens)
        self.assertEqual(len(self.port.writes), before)
        self.assertTrue(console.synchronized)

    def test_wiring_context_accepts_full_width_ids_without_truncation(self):
        for number, address in ((1, 1), (0xFFFFFFFF, 247)):
            def handler(i, command, args):
                if command != "wiring": return Serial.normal(i, command, args)
                return encoded(reply(i, command, result="done", target=number, address=address,
                    generation=number, configuration_generation=number, inputs=[0, 1, 2, 0],
                    outputs=[1, 2], bus_traffic=False))
            console = self.session(handler)
            result = console.command("wiring", host_args=())
            self.assertEqual(result["target"], number)
            self.assertEqual(result["generation"], number)
            self.assertEqual(result["configuration_generation"], number)
            self.assertEqual(result["address"], address)
            self.assertFalse(console.operations)

    def test_wiring_query_preserves_disabled_configuration_generation(self):
        def handler(i, command, args):
            if command != "wiring": return Serial.normal(i, command, args)
            return encoded(reply(i, command, result="done", target=1, address=1, generation=9,
                configuration_generation=0, inputs=[0] * 4, outputs=[0] * 2, bus_traffic=False))
        console = self.session(handler)
        self.assertEqual(console.command("wiring", host_args=())["configuration_generation"], 0)
        self.assertFalse(console.operations)
        self.assertEqual(len(self.port.writes), 2)

    def test_wiring_rejects_malformed_context_and_wrong_correlation_without_retry(self):
        mutations = [dict(id=999), dict(command="config"), dict(profile="another"), dict(ok=1),
            dict(result="accepted"), dict(result="unknown", ok=False), dict(bus_traffic=True),
            dict(target=0), dict(target=True), dict(target=0x100000000), dict(address=0),
            dict(address=248), dict(address=1.0), dict(generation=False), dict(generation=0),
            dict(configuration_generation=False), dict(configuration_generation=0x100000000),
            dict(inputs=[0] * 3), dict(inputs=[0, 0, 0, True]), dict(inputs=[0, 0, 0, 3]),
            dict(outputs=[0]), dict(outputs=[0, -1]), dict(outputs=[0, "0"]),
            dict(inputs=[1, 0, 0, 0]), dict(operation_id=0), dict(command_id=5),
            dict(tx_hex=""), dict(rx_bytes=0), dict(tx_accepted=0)]
        for fields in mutations:
            def handler(i, command, args):
                if command != "wiring": return Serial.normal(i, command, args)
                item = reply(i, command, result="done", target=1, address=1, generation=9,
                    configuration_generation=3, inputs=[0] * 4, outputs=[0] * 2, bus_traffic=False)
                item.update(fields)
                return encoded(item)
            console = self.session(handler)
            with self.subTest(fields=fields), self.assertRaises(bench.BenchError):
                console.command("wiring", host_args=("x0", "unknown"))
            self.assertFalse(console.synchronized)
            self.assertEqual(len(self.port.writes), 2)
            with self.assertRaises(bench.BenchError): console.command("wiring", host_args=())
            self.assertEqual(len(self.port.writes), 2)

    def test_wiring_refusal_preserves_returned_declarations(self):
        for refusal in ("busy", "invalid", "ids_exhausted"):
            def handler(i, command, args):
                if command != "wiring": return Serial.normal(i, command, args)
                return encoded(reply(i, command, ok=False, result=refusal, target=1, address=1,
                    generation=9, configuration_generation=0 if refusal == "ids_exhausted" else 3, inputs=[0] * 4, outputs=[0] * 2,
                    bus_traffic=False))
            console = self.session(handler)
            result = console.command("wiring", host_args=("x0", "connected"))
            self.assertFalse(result["ok"])
            self.assertEqual(result["inputs"], [0] * 4)
            self.assertFalse(console.operations)
            self.assertTrue(console.synchronized)

    def test_useaddr_is_explicit_synchronous_selection_without_operation(self):
        def handler(i, command, args):
            if command == "useaddr":
                return encoded(reply(i, command, result="done", address=int(args[0]), operation_id=0))
            return Serial.normal(i, command, args)
        console = self.session(handler)
        for address in (1, 17, 247):
            result = console.command("useaddr", address=address)
            self.assertEqual(result["address"], address)
            self.assertEqual(result["operation_id"], 0)
        self.assertFalse(console.operations)
        self.assertEqual([line.decode().split()[1:] for line in self.port.writes][1:],
                         [["useaddr", str(address)] for address in (1, 17, 247)])
        before = len(self.port.writes)
        for address in (None, False, True, 0, 248, -1, 4294967296, 1.0, "1", "1\nprobe"):
            with self.subTest(address=address), self.assertRaises(ValueError):
                console.command("useaddr", address=address)
        self.assertEqual(before, len(self.port.writes))

    def test_useaddr_rejects_wrong_correlation_and_bus_shaped_success(self):
        for fields in ({"address": 2}, {"address": True}, {"operation_id": 1}, {"operation_id": False},
                       {"result": "accepted"}, {"command_id": 99}, {"tx_bytes": 8}, {"bus_traffic": True},
                       {"ok": False, "result": "unknown"}, {"ok": False, "result": "busy", "operation_id": 1}):
            def handler(i, command, args):
                if command != "useaddr": return Serial.normal(i, command, args)
                item = reply(i, command, result="done", address=1, operation_id=0); item.update(fields)
                return encoded(item)
            console = self.session(handler)
            with self.subTest(fields=fields), self.assertRaises(bench.BenchError):
                console.command("useaddr", address=1)
            self.assertFalse(console.synchronized)
            self.assertEqual(len(self.port.writes), 2)

    def test_useaddr_explicit_refusal_remains_a_local_result(self):
        for refusal in ("busy", "invalid", "unavailable", "ids_exhausted"):
            def handler(i, command, args):
                if command != "useaddr": return Serial.normal(i, command, args)
                return encoded(reply(i, command, ok=False, result=refusal, address=17, operation_id=0))
            console = self.session(handler)
            self.assertEqual(console.command("useaddr", address=17)["result"], refusal)
            self.assertTrue(console.synchronized)
            self.assertFalse(console.operations)

    def test_motion_profile_forget_releases_only_the_explicit_snapshot(self):
        forgotten = False
        def handler(i, command, args):
            nonlocal forgotten
            if command != "motion-profile": return Serial.normal(i, command, args)
            if args[0] == "forget": forgotten = True
            if args[0] == "read": forgotten = False
            item = motion_profile_reply(i, "forget" if forgotten else args[0], pending=args[0] == "read")
            item["request"] = args[0]
            return encoded(item)
        console = self.session(handler)
        console.command("motion-profile", host_args=("inspect",))
        self.assertTrue(console.motion_profile["saved"])
        result = console.command("motion-profile", host_args=("forget",))
        self.assertEqual(result["phase"], 0)
        self.assertFalse(result["saved"])
        self.assertEqual(console.motion_profile, result)
        self.assertFalse(console.operations)
        self.assertEqual(self.port.writes[-1].decode().split()[1:], ["motion-profile", "forget"])
        self.assertFalse(console.command("motion-profile", host_args=("inspect",))["saved"])
        # A later snapshot is a new explicit read, with no hidden restoration.
        self.assertTrue(console.command("motion-profile", host_args=("read",))["pending"])

    def test_motion_profile_forget_rejects_retained_or_invented_wire_evidence(self):
        mutations = [lambda x: x.update(saved=True), lambda x: x.update(original=[1]*6),
            lambda x: x.update(current=[1]*6), lambda x: x.update(address=1), lambda x: x.update(serial_generation=1),
            lambda x: x.update(execution_unknown=True), lambda x: x.update(write_execution_unknown=True),
            lambda x: x.update(delivered_us=1), lambda x: x.update(tx_hex="0103"),
            lambda x: x.update(ok=False, result="unknown"), lambda x: x.update(result="done")]
        for mutate in mutations:
            def handler(i, command, args):
                if command != "motion-profile": return Serial.normal(i, command, args)
                item = motion_profile_reply(i, "forget"); mutate(item); return encoded(item)
            console = self.session(handler)
            with self.subTest(mutation=mutate), self.assertRaises(bench.BenchError):
                console.command("motion-profile", host_args=("forget",))
            self.assertFalse(console.synchronized)
            self.assertEqual(len(self.port.writes), 2)

    def test_busy_motion_profile_forget_preserves_the_snapshot(self):
        def handler(i, command, args):
            if command != "motion-profile": return Serial.normal(i, command, args)
            item = motion_profile_reply(i)
            item["request"] = args[0]
            if args[0] == "forget": item.update(ok=False, result="busy")
            return encoded(item)
        console = self.session(handler)
        console.command("motion-profile", host_args=("inspect",))
        original = copy.deepcopy(console.motion_profile)
        result = console.command("motion-profile", host_args=("forget",))
        self.assertFalse(result["ok"])
        self.assertTrue(result["saved"])
        self.assertEqual(result["original"], original["original"])
        self.assertEqual(result["tx_hex"], original["tx_hex"])
        self.assertTrue(console.synchronized)
        self.assertEqual(len(self.port.writes), 3)

    def test_action_observation_requires_checked_echo(self):
        for command, policy in (("enable", None), ("motor-release", None), ("stop", "normal"), ("stop", "fast")):
            good = action_terminal(1, command, policy)
            good.update(execution="unknown")
            good["write_evidence"]["response_confirmed"] = False
            bench.Console._check_action(good, command, 1, policy)
            mutations = [lambda x: x["write_evidence"].update(raw_hex="0106002D00120000"),
                lambda x: x["write_evidence"].update(tx_accepted=7, tx_complete=False),
                lambda x: x["write_evidence"].update(event=1),
                lambda x: x["write_evidence"].update(qualified=False, earliest_us=0, latest_us=0),
                lambda x: x["last_observation"].update(response_confirmed=False),
                lambda x: x["last_observation"].update(earliest_us=1200),
                lambda x: x.update(deadline_us=1150)]
            for mutate in mutations:
                bad = copy.deepcopy(good); mutate(bad)
                with self.subTest(command=command, mutation=mutate):
                    with self.assertRaises(bench.BenchError): bench.Console._check_action(bad, command, 1, policy)

    def test_unconfirmed_move_keeps_confirmed_staging_and_fresh_completion(self):
        good = move_terminal(1)
        good.update(execution="unknown")
        good["trigger_evidence"]["response_confirmed"] = False
        bench.Console._check_move(good, 1, self.MOVE_ARGS)
        mutations = [lambda x: x["trigger_evidence"].update(raw_hex="0106002700010000"),
            lambda x: x["trigger_evidence"].update(tx_accepted=7, tx_complete=False),
            lambda x: x["trigger_evidence"].update(event=1),
            lambda x: x["trigger_evidence"].update(qualified=False, earliest_us=0, latest_us=0),
            lambda x: x["staging_evidence"].update(response_confirmed=False),
            lambda x: x["activity_evidence"].update(response_confirmed=False),
            lambda x: x["last_observation"].update(response_confirmed=False),
            lambda x: x["last_observation"].update(earliest_us=3100),
            lambda x: x["prerequisites"].update(maximum_age_us=1150)]
        for mutate in mutations:
            bad = copy.deepcopy(good); mutate(bad)
            with self.subTest(mutation=mutate):
                with self.assertRaises(bench.BenchError): bench.Console._check_move(bad, 1, self.MOVE_ARGS)

    def test_unconfirmed_echo_late_delivery_retains_deadline_without_observation(self):
        action = action_terminal(1, "enable")
        action.update(execution="unknown", ok=False,
            state="failed", completion="not_observed", outcome="deadline", observation_known=False,
            raw_alarm=None, raw_motion=None, polls=0, serviced_us=10000)
        empty = copy.deepcopy(action["failure_evidence"])
        action["write_evidence"].update(response_confirmed=False, delivered_us=10000)
        action["failure_evidence"] = copy.deepcopy(action["write_evidence"])
        action["last_observation"] = empty
        bench.Console._check_action(action, "enable", 1, None)
        move = move_terminal(1)
        empty = copy.deepcopy(move["failure_evidence"])
        move.update(execution="unknown", ok=False,
            state="failed", completion="not_observed", outcome="deadline", status="ILLEGAL_VALUE", detail=10,
            observation_known=False, running_observed=False, uncertain=True,
            raw_alarm=None, raw_motion=None, polls=0, serviced_us=10000)
        move["trigger_evidence"].update(response_confirmed=False, delivered_us=10000)
        move["failure_evidence"] = copy.deepcopy(move["trigger_evidence"])
        move["last_observation"] = copy.deepcopy(empty); move["activity_evidence"] = empty
        bench.Console._check_move(move, 1, self.MOVE_ARGS)

    def test_action_terminal_and_retained_round_trip(self):
        for command, policy in (("enable", None), ("motor-release", None), ("alarm-clear", None),
                                ("stop", "normal"), ("stop", "fast")):
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
        fixture.retained[handle.operation_id] = dict(fixture.retained[handle.operation_id], stop_policy="fast")
        with self.assertRaisesRegex(bench.BenchError, "policy"):
            console.command("result", operation_id=handle.operation_id)

    def test_actions_share_ordinary_quota_but_stop_has_reserved_result(self):
        fixture = ActionSerial()
        console = self.session(fixture)
        for _ in range(bench.MAX_PROBES):
            handle = console.begin("enable", address=1)
            console.wait(handle)
        recovery = console.begin("recover"); console.wait(recovery)
        stop = console.begin("stop", address=1, stop_policy="fast"); console.wait(stop)
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
        for policy in ("normal", "fast"):
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

    def test_state_read_uses_canonical_wire_command(self):
        console = self.session(TypedSerial(), fragment=1)
        terminal = console.command("read-state", address=1, timeout_s=0.1)
        self.assertEqual(terminal["command"], "read-state")
        self.assertEqual(self.port.writes[-2:], [b"@2 read state 1\n", b"@3 release 102\n"])

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
            if command == "read" and args and args[0] == "state":
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
        self.assertEqual(commands.count(["read", "state", "1"]), 2)
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
                    if command == "read" and args and args[0] == "state":
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
                self.assertEqual(len([entry for entry in self.port.writes if b"read state" in entry]), 1)

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

    def test_probe_raw_frames_and_decoded_model_must_agree_before_release(self):
        # CRC-valid wrong-address, wrong-window and wrong-model vectors test
        # correlation independently of the parser's claimed OK disposition.
        for change in ({"rx_hex": "010302003C0000"},
                       {"rx_hex": "020302003CFC55"},
                       {"rx_hex": "0103020000B844"},
                       {"rx_hex": "010402003CB921"},
                       {"rx_hex": "010304003C5854"},
                       {"rx_hex": "010302003C"},
                       {"rx_hex": "010302003CB85500"},
                       {"rx_hex": "01 0302003CB855"},
                       {"tx_hex": "010300000001840B"},
                       {"tx_hex": "010300010001D5CA"},
                       {"tx_hex": None}, {"rx_hex": True},
                       {"register_start": 1}, {"register_count": 2},
                       {"register_start": False}, {"register_count": True},
                       {"identity": "exact_model"}):
            with self.subTest(change=change):
                console = self.session()
                def malformed(i, cmd, args):
                    accepted, terminal = map(json.loads, Serial.normal(i, cmd, args).splitlines())
                    return encoded(accepted) + encoded({**terminal, **change})
                self.port.handler = malformed
                self.failed(lambda: console.command("probe", address=1, timeout_s=.1),
                            "raw frame evidence")
                self.assertEqual(self.port.writes, [b"@1 version\n", b"@2 probe 1\n"])

    def test_successful_read_cannot_claim_parser_errors_or_omit_raw_evidence(self):
        for command in ("probe", "capture-read"):
            for key in ("detail", "frame_error", "tx_hex", "rx_hex"):
                for value in (None, True, 7):
                    with self.subTest(command=command, key=key, value=value):
                        console = self.session()
                        def malformed(i, cmd, args):
                            accepted, terminal = map(json.loads, Serial.normal(i, cmd, args).splitlines())
                            if value is None:
                                terminal.pop(key)
                            else:
                                terminal[key] = value
                            return encoded(accepted) + encoded(terminal)
                        self.port.handler = malformed
                        self.failed(lambda: console.command(command, address=1, timeout_s=.1),
                                    "result evidence|raw frame evidence")
                        self.assertEqual(len(self.port.writes), 2)

    def test_successful_probe_requires_current_confidence_and_native_window_fields(self):
        for key in ("confidence", "manufacturer_confirmed", "exact_model_confirmed",
                    "collision_excluded", "register_start", "register_count", "identity"):
            with self.subTest(key=key):
                console = self.session()
                def malformed(i, cmd, args):
                    accepted, terminal = map(json.loads, Serial.normal(i, cmd, args).splitlines())
                    terminal.pop(key)
                    return encoded(accepted) + encoded(terminal)
                self.port.handler = malformed
                self.failed(lambda: console.command("probe", timeout_s=.1),
                            "confidence|raw frame evidence")
                self.assertEqual(len(self.port.writes), 2)

    def test_probe_exception_remains_failure_without_replay(self):
        console = self.session()
        def exception(i, cmd, args):
            if cmd != "probe":
                return Serial.normal(i, cmd, args)
            accepted, terminal = map(json.loads, Serial.normal(i, cmd, args).splitlines())
            terminal.update(ok=False, codec="EXCEPTION", detail=2, frame_error=10,
                            outcome="reply_error", raw_model=None, rx_bytes=5,
                            rx_hex="018302C0F1", identity="unknown", confidence="responder_only")
            return encoded(accepted) + encoded(terminal)
        self.port.handler = exception
        terminal = console.command("probe", address=1, timeout_s=.1)
        self.assertFalse(terminal["ok"])
        self.assertEqual((terminal["codec"], terminal["detail"], terminal["confidence"]),
                         ("EXCEPTION", 2, "responder_only"))
        self.assertEqual(self.port.writes, [b"@1 version\n", b"@2 probe 1\n", b"@3 release 102\n"])

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

    def test_owner_watchdog_diagnostics_are_not_a_panic_line(self):
        console = self.session()
        def diagnostic(request_id, command, args):
            return encoded({**json.loads(Serial.normal(request_id, command, args)),
                            "owner_watchdog": {"subscribed": True, "error": 0,
                                               "timeout_ms": 5000, "last_feed_us": 123,
                                               "completed_loops": 42}, "reset_reason": 6})
        self.port.handler = diagnostic
        result = console.command("drv", timeout_s=0.1)
        self.assertTrue(result["owner_watchdog"]["subscribed"])
        self.assertTrue(console.synchronized)

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
                ("probe", {"raw_model": 61, "rx_hex": "010302003D7995"}),
                ("probe", {"observed_earliest_us": 13301, "delivered_us": 16001}),
                ("probe", {"ok": False, "outcome": "cancelled", "transport": "CANCELLED", "confidence": "none"}),
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

    def test_previous_protocols_are_rejected_before_motor_commands(self):
        for protocol in (1, 2):
            console = self.session(identify=False)
            self.port.handler = lambda i, command, args: encoded(reply(i, command, product="MotorControl-RS",
                                                                      protocol=protocol, outstanding_capacity=10))
            self.failed(lambda: console.identify(timeout_s=0.1), "not the supported")
            self.assertEqual(self.port.writes, [b"@1 version\n"])

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


class HostDevice:
    """Host configuration state and immutable retained operation records."""
    def __init__(self):
        self.original = dict(baud=115200, format="8N1")
        self.active = dict(self.original); self.requested = dict(self.original)
        self.generation = 1; self.blocked = False; self.failure = "none"
        self.retained = {}; self.mutate = None; self.mismatch_transport = "NO_RESPONSE"
        self.mismatch_evidence = {}
        self.setup_failure = False; self.restore_failure = False
        self.recovery_required = False

    def serial(self):
        return dict(known=True, **self.active, generation=self.generation)

    def __call__(self, i, command, args):
        if command == "host":
            requested = bench.host_arguments(tuple(args))
            ok = True; result = "done"
            if args and args != ["caps"]:
                selected = dict(self.original if requested.get("restore") else self.active)
                selected.update({key: value for key, value in requested.items() if key != "restore"})
                if self.recovery_required and not self.blocked:
                    ok = False; result = "recovery_required"
                elif self.blocked or selected != self.active:
                    self.requested = selected; self.generation += 1
                    failed = self.restore_failure if requested.get("restore") else self.setup_failure
                    self.blocked = failed
                    self.failure = ("restore" if requested.get("restore") else "adapter") if failed else "none"
                    if failed: ok = False; result = "failed"
                    else: self.active = dict(selected)
            state = reply(i, command, ok=ok, result=result, original=dict(self.original),
                          requested=dict(self.requested), active=dict(self.active), active_known=not self.blocked,
                          blocked=self.blocked, configuring=False, serial_generation=self.generation,
                          actual_baud=0 if self.blocked else self.active["baud"], failure=self.failure,
                          device_settings_changed=False, supported_bauds=list(bench.HOST_BAUDS),
                          supported_formats=list(bench.HOST_FORMATS), timing=bench.host_timing(**dict(baud=self.active["baud"], fmt=self.active["format"])))
            if self.mutate: self.mutate(state)
            return encoded(state)
        if command == "result":
            value = dict(self.retained[int(args[0])], id=i, type="reply", command="result")
            if self.mutate: self.mutate(value)
            return encoded(value)
        if command == "release":
            del self.retained[int(args[0])]
            return Serial.normal(i, command, args)
        if command in ("probe", "recover"):
            admission, terminal = [json.loads(line) for line in Serial.normal(i, command, args).splitlines()]
            terminal["host_serial"] = self.serial()
            if command == "probe" and self.active != self.original:
                terminal.update(ok=False, transport=self.mismatch_transport, codec="NOT_CHECKED", outcome="transport",
                                raw_model=None, execution_unknown=True, tx_bytes=8, rx_bytes=0,
                                timing_valid=False, raw_truncated=False, rx_hex="", identity="unknown", confidence="none")
                terminal.update(self.mismatch_evidence)
                self.recovery_required = True
            if command == "recover": self.recovery_required = False
            if self.mutate: self.mutate(terminal)
            self.retained[terminal["operation_id"]] = copy.deepcopy(terminal)
            return encoded(admission) + encoded(terminal)
        if command == "read" and args == ["identity", "1"]:
            terminal = typed_terminal(i, "identity")
            terminal["host_serial"] = self.serial()
            parity, stops = {"8N1": (1, 1), "8N2": (1, 2), "8E1": (2, 1), "8O1": (3, 1)}[self.active["format"]]
            terminal["active_serial"] = dict(known=True, baud=self.active["baud"], data_bits=8, parity=parity, stop_bits=stops)
            if self.mutate: self.mutate(terminal)
            self.retained[terminal["operation_id"]] = copy.deepcopy(terminal)
            return encoded(reply(i, "read-identity", result="accepted", read_kind="identity", address=1,
                                 operation_id=terminal["operation_id"])) + encoded(terminal)
        return Serial.normal(i, command, args)


class HostSerialTests(unittest.TestCase):
    def session(self):
        self.device = HostDevice(); self.clock = Clock(); self.events = []
        self.port = Serial(self.device)
        self.console = bench.Console(self.port, clock=self.clock, sleeper=self.clock.sleep,
                                    on_event=lambda event, **fields: self.events.append(dict(event=event, **fields)))
        self.console.identify(timeout_s=0.1)
        return self.console

    def test_all_reviewed_tuples_and_bounded_arguments(self):
        c = self.session(); c.command("host")
        for baud in bench.HOST_BAUDS:
            for fmt in bench.HOST_FORMATS:
                item = c.command("host", host_args=("set", str(baud), fmt))
                self.assertEqual(item["active"], dict(baud=baud, format=fmt))
                self.assertEqual(item["timing"], bench.host_timing(baud, fmt))
                bits = 2 if fmt == "8N2" else 1
                self.assertEqual(item["timing"]["stop_guard_us"], (bits * 100000000 + baud * 98 - 1) // (baud * 98) + 2)
                self.assertEqual(item["timing"]["reply_gap_us"], 304 if (baud, fmt) == (115200, "8N1") else item["timing"]["gap35_us"])
        before = len(self.port.writes)
        for tokens in (("baud", "57600"), ("fmt", "7E1"), ("set", "9600", "8E2"), ("set", "9600", "8N1", "again"), ("baud", "9600\nprobe")):
            with self.assertRaises(ValueError): c.command("host", host_args=tokens)
        self.assertEqual(len(self.port.writes), before)
        c.command("host", host_args=("restore",))
        previous = c.serial["serial_generation"]
        c.command("host", host_args=("restore",))
        self.assertEqual(c.serial["serial_generation"], previous)
        args = bench.arguments(["--port", "FAKE", "--log", "unused", "host-check", "--baud", "9600", "--fmt", "8E1"])
        self.assertEqual((args.baud, args.host_baud, args.fmt), (115200, 9600, "8E1"))

    def test_host_state_mutations_rejected(self):
        changes = [lambda x: x.update(active_known=False), lambda x: x.update(device_settings_changed=True),
                   lambda x: x.update(supported_bauds=[9600] * 5), lambda x: x.update(serial_generation=True),
                   lambda x: x["timing"].update(character_max_us=87), lambda x: x["active"].update(format="8E2"),
                   lambda x: x.update(failure="adapter"), lambda x: x.update(actual_baud=-1),
                   lambda x: x.update(actual_baud=0), lambda x: x.update(actual_baud=117505)]
        for mutation in changes:
            with self.subTest(mutation=mutation):
                c = self.session(); self.device.mutate = mutation
                with self.assertRaises(bench.BenchError): c.command("host")

    def test_successful_change_must_match_request_and_generation(self):
        for mutation in (lambda x: x.update(serial_generation=1), lambda x: x["active"].update(baud=38400)):
            c = self.session(); c.command("host"); self.device.mutate = mutation
            with self.assertRaises(bench.BenchError): c.command("host", host_args=("baud", "9600"))

    def test_8n2_requires_two_slow_stop_bits_of_publication_guard(self):
        c = self.session(); c.command("host")
        self.device.mutate = lambda x: x["timing"].update(stop_guard_us=bench.host_timing(9600, "8N1")["stop_guard_us"])
        with self.assertRaisesRegex(bench.BenchError, "timing does not match"):
            c.command("host", host_args=("set", "9600", "8N2"))

    def test_failed_setup_query_explicit_repair_and_failed_restore(self):
        c = self.session(); c.command("host"); self.device.setup_failure = True
        failed = c.command("host", host_args=("set", "9600", "8N2"))
        self.assertFalse(failed["ok"]); self.assertTrue(failed["blocked"])
        self.assertFalse(c.command("host")["active_known"])
        self.device.setup_failure = False
        fixed = c.command("host", host_args=("set", "9600", "8N2"))
        self.assertTrue(fixed["ok"]); self.assertEqual(fixed["serial_generation"], 3)
        self.device.restore_failure = True
        failed = c.command("host", host_args=("restore",))
        self.assertEqual(failed["failure"], "restore"); self.assertFalse(failed["active_known"])
        self.device.restore_failure = False
        self.assertTrue(c.command("host", host_args=("restore",))["ok"])
        self.assertNotIn("recover", [line.decode().split()[1] for line in self.port.writes])

    def test_failed_attempt_must_retain_exact_request_and_generation(self):
        mutations = (lambda x: x["requested"].update(baud=38400), lambda x: x["requested"].update(format="8O1"),
                     lambda x: x.update(serial_generation=1), lambda x: x.update(serial_generation=3),
                     lambda x: x.update(failure="restore"), lambda x: x["active"].update(baud=9600))
        for mutation in mutations:
            c = self.session(); c.command("host"); self.device.setup_failure = True; self.device.mutate = mutation
            with self.assertRaises(bench.BenchError): c.command("host", host_args=("set", "9600", "8N2"))
        c = self.session(); c.command("host"); self.device.restore_failure = True
        c.command("host", host_args=("baud", "9600"))
        self.device.mutate = lambda x: x["requested"].update(baud=19200)
        with self.assertRaises(bench.BenchError): c.command("host", host_args=("restore",))

    def test_query_cannot_fabricate_repair_or_change_requested_tuple(self):
        for tokens in ((), ("caps",)):
            for mutate in (lambda x: x.update(active_known=True, blocked=False, failure="none", actual_baud=115200),
                           lambda x: x["requested"].update(baud=19200)):
                c = self.session(); c.command("host"); self.device.setup_failure = True
                self.assertFalse(c.command("host", host_args=("set", "9600", "8N2"))["ok"])
                self.device.mutate = mutate
                with self.assertRaisesRegex(bench.BenchError, "query changed saved host state"):
                    c.command("host", host_args=tokens)

    def test_historical_operation_tuple_survives_host_change(self):
        c = self.session(); c.command("host")
        h = c.begin("probe", address=1); original = c.wait(h)
        c.command("host", host_args=("set", "9600", "8E1"))
        inspected = c.command("result", operation_id=h.operation_id)
        self.assertEqual(inspected["host_serial"], original["host_serial"])
        self.assertNotEqual(inspected["host_serial"]["generation"], c.serial["serial_generation"])
        c.command("release", operation_id=h.operation_id)

    def test_queries_and_refusals_preserve_failure_and_actual_baud(self):
        for tokens in ((), ("caps",), ("restore",)):
            with self.subTest(tokens=tokens):
                c = self.session(); c.command("host"); self.device.setup_failure = True
                self.assertEqual(c.command("host", host_args=("set", "9600", "8N2"))["failure"], "adapter")
                if tokens == ("restore",):
                    original = self.port.handler
                    def refused(i, command, args):
                        item = json.loads(original(i, command, [])) if command == "host" else None
                        if item is not None:
                            item.update(ok=False, result="busy", failure="runner")
                            return encoded(item)
                        return original(i, command, args)
                    self.port.handler = refused
                else:
                    self.device.mutate = lambda item: item.update(failure="runner")
                with self.assertRaisesRegex(bench.BenchError, "altered host state|changed saved host state"):
                    c.command("host", host_args=tokens)
        for tokens in ((), ("caps",), ("restore",), ("set", "9600", "8N1")):
            with self.subTest(actual_baud=tokens):
                c = self.session(); c.command("host")
                self.device.recovery_required = tokens[0:1] == ("set",)
                self.device.mutate = lambda item: item.update(actual_baud=115201)
                with self.assertRaisesRegex(bench.BenchError, "altered host state|changed saved host state"):
                    c.command("host", host_args=tokens)

    def test_compiled_capabilities_cannot_change_on_query_or_selection(self):
        mutations = (lambda item: item.update(supported_bauds=[9600, 115200]),
                     lambda item: item.update(supported_formats=["8N1", "8N2"]))
        for tokens in ((), ("caps",), ("set", "9600", "8N2")):
            for mutation in mutations:
                with self.subTest(tokens=tokens, mutation=mutation):
                    c = self.session(); c.command("host"); self.device.mutate = mutation
                    with self.assertRaisesRegex(bench.BenchError, "adapter capabilities changed"):
                        c.command("host", host_args=tokens)

    def test_fabricated_historical_tuple_and_typed_tuple_disagreement_rejected(self):
        for field, value in (("baud", 9600), ("format", "8N2"), ("generation", 2), ("known", False)):
            c = self.session(); c.command("host")
            self.device.mutate = lambda x: x["host_serial"].update({field: value})
            with self.assertRaises(bench.BenchError): c.command("probe", address=1)
        c = self.session(); c.command("host", host_args=("set", "9600", "8E1"))
        self.assertTrue(c.command("read-identity", address=1)["ok"])
        self.device.mutate = lambda x: x["active_serial"].update(stop_bits=2)
        with self.assertRaises(bench.BenchError): c.command("read-identity", address=1)

    def test_finite_host_check_has_exact_declared_reads_recovery_restore(self):
        c = self.session()
        bench.host_check_campaign(c, baud=9600, fmt="8N1", timeout_s=1, address=1)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("probe"), 3); self.assertEqual(commands.count("recover"), 1)
        self.assertEqual(commands.count("result"), 3); self.assertEqual(commands.count("release"), 4)
        self.assertEqual(self.device.active, self.device.original)
        summary = self.events[-1]
        self.assertTrue(summary["ok"]); self.assertTrue(summary["restored"])
        self.assertEqual((summary["motor_writes"], summary["automatic_retries"]), (0, 0))

    def test_unexpected_mismatch_failure_has_no_read_replay_or_recovery(self):
        c = self.session(); self.device.mismatch_transport = "RX_ERROR"
        with self.assertRaisesRegex(bench.BenchError, "expected read-only"):
            bench.host_check_campaign(c, baud=9600, fmt="8N1", timeout_s=1, address=1)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("probe"), 2); self.assertNotIn("recover", commands)
        self.assertFalse(self.events[-1]["restored"])

    def test_length_mismatch_retains_received_byte_and_refused_restoration(self):
        c = self.session(); self.device.mismatch_transport = "LENGTH"
        self.device.mismatch_evidence = dict(rx_bytes=1, rx_hex="F9", timing_valid=True)
        with self.assertRaisesRegex(bench.BenchError, "expected read-only no-response evidence"):
            bench.host_check_campaign(c, baud=9600, fmt="8N1", timeout_s=1, address=1)
        commands = [line.decode().split()[1:] for line in self.port.writes]
        self.assertEqual(commands, [["version"], ["host"], ["probe", "1"], ["result", "103"], ["release", "103"],
                                    ["host", "set", "9600", "8N1"], ["probe", "1"], ["result", "107"],
                                    ["release", "107"], ["host", "restore"]])
        summary = self.events[-1]
        self.assertFalse(summary["ok"]); self.assertFalse(summary["restored"])
        self.assertTrue(summary["restoration_attempted"])
        self.assertEqual(summary["reads_attempted"], 2)
        self.assertEqual(summary["recoveries_attempted"], 0)
        self.assertEqual((summary["motor_writes"], summary["automatic_retries"]), (0, 0))
        mismatch = summary["mismatch_result"]
        self.assertEqual((mismatch["transport"], mismatch["tx_bytes"], mismatch["rx_bytes"], mismatch["rx_hex"]),
                         ("LENGTH", 8, 1, "F9"))
        self.assertTrue(mismatch["timing_valid"]); self.assertTrue(mismatch["execution_unknown"])
        self.assertEqual(mismatch["codec"], "NOT_CHECKED")
        restore = next(event["response"] for event in reversed(self.events)
                       if event["event"] == "reply" and event["response"].get("command") == "host")
        self.assertFalse(restore["ok"]); self.assertEqual(restore["result"], "recovery_required")
        self.assertEqual(c.serial["active"], dict(baud=9600, format="8N1"))
        self.assertTrue(c.serial["active_known"]); self.assertFalse(c.serial["blocked"])
        self.assertEqual(c.serial["serial_generation"], 2)
        self.assertTrue(self.device.recovery_required); self.assertTrue(c.synchronized)
        self.assertFalse(c.operations); self.assertFalse(self.device.retained)

    def test_two_host_checks_release_all_retained_recovery_results(self):
        c = self.session()
        for baud, fmt in ((9600, "8N1"), (115200, "8E1")):
            bench.host_check_campaign(c, baud=baud, fmt=fmt, timeout_s=1, address=1)
            self.assertFalse(self.device.retained)
            self.assertFalse(c.operations)
        commands = [line.decode().split()[1] for line in self.port.writes]
        self.assertEqual(commands.count("recover"), 2); self.assertEqual(commands.count("probe"), 6)

    def test_failed_declared_restoration_is_not_replayed(self):
        c = self.session(); self.device.restore_failure = True
        with self.assertRaisesRegex(bench.BenchError, "restoration failed"):
            bench.host_check_campaign(c, baud=9600, fmt="8N1", timeout_s=1, address=1)
        restores = [line for line in self.port.writes if line.decode().split()[1:] == ["host", "restore"]]
        self.assertEqual(len(restores), 1)
        self.assertTrue(self.device.blocked); self.assertFalse(self.events[-1]["restored"])
        self.assertTrue(self.events[-1]["restoration_attempted"])

    def test_probe_admission_timeout_or_interruption_counts_each_attempt(self):
        for stage in (1, 2, 3):
            for interrupted in (False, True):
                with self.subTest(stage=stage, interrupted=interrupted):
                    c = self.session(); original = self.port.handler
                    attempts = 0
                    def dropped(i, command, args):
                        nonlocal attempts
                        if command == "probe":
                            attempts += 1
                            if attempts == stage: return b""
                        return original(i, command, args)
                    self.port.handler = dropped
                    read = self.port.read
                    def interrupted_read(limit):
                        if attempts == stage: raise KeyboardInterrupt()
                        return read(limit)
                    if interrupted: self.port.read = interrupted_read
                    error = KeyboardInterrupt if interrupted else bench.BenchError
                    with self.assertRaises(error):
                        bench.host_check_campaign(c, baud=9600, fmt="8N1", timeout_s=0.1, address=1)
                    commands = [line.decode().split()[1:] for line in self.port.writes]
                    self.assertEqual(sum(command[0] == "probe" for command in commands), stage)
                    self.assertEqual(sum(command[0] == "recover" for command in commands), int(stage == 3))
                    self.assertEqual(commands.count(["host", "restore"]), int(stage == 3))
                    summary = self.events[-1]
                    self.assertEqual(summary["reads_attempted"], stage)
                    self.assertFalse(summary["ok"])
                    self.assertEqual(summary["restored"], stage == 3)
                    self.assertEqual(summary["restoration_attempted"], stage == 3)
                    self.assertEqual(summary["automatic_retries"], 0)
                    self.assertFalse(c.synchronized)
                    self.assertEqual(self.device.active, self.device.original if stage != 2 else dict(baud=9600, format="8N1"))


if __name__ == "__main__":
    unittest.main()
