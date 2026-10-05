"""Actual control core/formatter records and strict retained-result transport."""
import copy
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bench_probe import BenchError, Console, MAX_LINE, driver_arguments, wire_crc

rows = subprocess.check_output([sys.argv[1], "--control-fixtures"], text=True, timeout=10).splitlines()
assert len(rows) == 23
candidate = ("set", "algorithm", "algorithm-1", "encoder-resolution", "8000",
             "maximum-effective-current", "1000", "closed-maximum-current", "100",
             "closed-base-current", "40", "open-maximum-current", "100",
             "lock-current", "50", "lock-delay", "3999")

def check_failure_bytes(record, arguments, group):
    """Real successful replies cannot become failures by changing labels alone."""
    columns = record["evidence_columns"]
    def failed(raw, status, detail, error):
        value = copy.deepcopy(record)
        value.update(ok=False, state="failed", outcome="reply_error", status=status,
                     detail=detail, completed_steps=len(value["evidence"]) - 1, observation=None)
        row = value["evidence"][-1]
        for key, content in (("raw_hex", raw.hex()), ("received_length", len(raw)),
                             ("status", status), ("detail", detail), ("frame_error", error)):
            row[columns.index(key)] = content
        return value
    def rejected(value):
        try:
            Console._check_driver(value, 1, arguments, group)
        except BenchError:
            return
        raise AssertionError("invented parser failure or terminal priority accepted")
    raw = bytes.fromhex(record["evidence"][-1][columns.index("raw_hex")])
    rejected(failed(raw, "CRC_ERROR", 6, 6))
    variants = [(raw[:-1] + bytes((raw[-1] ^ 1,)), "CRC_ERROR", 6),
                (bytes((2,)) + raw[1:], "FRAME_ERROR", 3),
                (raw[:1] + bytes((4,)) + raw[2:], "FRAME_ERROR", 4),
                (raw[:2] + bytes((0,)) + raw[3:], "FRAME_ERROR", 5),
                (raw[:-1], "FRAME_ERROR", 2), (b"", "FRAME_ERROR", 2)]
    exception = bytes((1, 0x83, 2))
    exception += wire_crc(exception).to_bytes(2, "little")
    variants.append((exception, "EXCEPTION", 10))
    for altered, status, error in variants:
        valid = failed(altered, status, 2 if error == 10 else error, error)
        Console._check_driver(valid, 1, arguments, group)
        forged = copy.deepcopy(valid)
        forged["evidence"][-1][columns.index("frame_error")] = 255
        rejected(forged)
        for timing in ("unqualified", "late"):
            timed = copy.deepcopy(valid)
            row = timed["evidence"][-1]
            if timing == "unqualified":
                row[columns.index("qualified")] = False
                row[columns.index("earliest_us")] = row[columns.index("latest_us")] = 0
                outcome, detail = "timing_unqualified", 16
            else:
                row[columns.index("earliest_us")] = timed["deadline_us"] + 1
                row[columns.index("latest_us")] = timed["deadline_us"] + 2
                row[columns.index("delivered_us")] = timed["serviced_us"] = timed["deadline_us"] + 3
                outcome, detail = "deadline", 13
            rejected(timed)  # Parser failure cannot outrank missing/late closure.
            timed.update(outcome=outcome, status="ILLEGAL_VALUE", detail=detail)
            Console._check_driver(timed, 1, arguments, group)

for row in rows:
    fixture = json.loads(row)
    record = fixture["record"]
    arguments = ("read",) if record["driver_kind"] == "read" else candidate
    Console._check_driver(record, 1, arguments, "control_settings")
    assert len(row) < MAX_LINE
    assert record["command"] == record["type"] == "control"
    if fixture["case"] in ("previous_settings_deadline", "previous_settings_late_echo"):
        assert record["stationary_valid_until_us"] == 250
        assert record["outcome"] == "deadline" and not record["ok"]
        first = dict(zip(record["progress_columns"], record["progress"][0]))
        assert not first["acknowledged"] and not first["readback_known"]
        if fixture["case"] == "previous_settings_late_echo":
            assert first["execution"] == "unknown" and record["uncertain"]
        else:
            assert first["execution"] == "not_transmitted" and not record["uncertain"]
        forged = copy.deepcopy(record)
        forged["stationary_valid_until_us"] = 280
        try:
            Console._check_driver(forged, 1, arguments, "control_settings")
        except BenchError:
            pass
        else:
            raise AssertionError("previous-settings deadline replaced by identity deadline")
    if record["ok"] and record["driver_kind"] == "update":
        assert record["completed_steps"] == 16 and len(record["progress"]) == 8
    if record["ok"] and record["driver_kind"] == "read":
        check_failure_bytes(record, arguments, "control_settings")
    mutations = [lambda r: r.update(driver_group="drive"),
                 lambda r: r.update(active_settings_known=True),
                 lambda r: r.update(configuration_generation=0),
                 lambda r: r["evidence"][0].__setitem__(1, 0x2042),
                 lambda r: r["evidence"][0].__setitem__(5, "0000"),
                 lambda r: r.update(fields=r["fields"] | 1)]
    if record["driver_kind"] == "read":
        mutations.extend((lambda r: r["observation"].update(current_percent_base="maximum-effective-current"),
                          lambda r: r["observation"].update(maximum_effective_current_ma=4000),
                          lambda r: r["observation"].update(encoder_scale_usable=not r["observation"]["encoder_scale_usable"]),
                          lambda r: r["observation"].update(algorithm_known=not r["observation"]["algorithm_known"]),
                          lambda r: r["evidence"][0].__setitem__(r["evidence_columns"].index("execution_unknown"), True)))
    else:
        mutations.append(lambda r: r["progress"][0].__setitem__(3, 1))
        if record["echo_readback_policy"] and record["ok"]:
            mutations.append(lambda r: r.update(echo_readback_policy=False))
    for mutate in mutations:
        broken = copy.deepcopy(record)
        mutate(broken)
        try:
            Console._check_driver(broken, 1, arguments, "control_settings")
        except BenchError:
            pass
        else:
            raise AssertionError((fixture["case"], "corrupt control evidence accepted"))
    try:
        Console._check_driver(record, 2, arguments, "control_settings")
    except BenchError:
        pass
    else:
        raise AssertionError("wrong target accepted")

# Synthetic timing variants keep the actual formatter's checked wire bytes and
# schema. Closure, rather than application delivery, determines whether an
# acknowledgement or device rejection belongs to the admitted write budget.
update = next(json.loads(row)["record"] for row in rows if json.loads(row)["case"] == "full_checked_update")
progress_names = update["progress_columns"]
evidence_names = update["evidence_columns"]
# A CRC-valid FC06 reply with the wrong echoed register remains an uncertain
# parser failure; its shape alone cannot establish acknowledgement or rejection.
echo_failure = copy.deepcopy(update)
echo_failure["progress"] = echo_failure["progress"][:1]
echo_failure["evidence"] = echo_failure["evidence"][:1]
progress = dict(zip(progress_names, echo_failure["progress"][0]))
evidence = dict(zip(evidence_names, echo_failure["evidence"][0]))
raw = bytearray.fromhex(evidence["raw_hex"])
raw[3] ^= 1
raw[-2:] = wire_crc(raw[:-2]).to_bytes(2, "little")
progress.update(acknowledged=False, readback_known=False, readback=0, execution="unknown")
evidence.update(raw_hex=raw.hex(), status="FRAME_ERROR", detail=7, frame_error=7)
echo_failure.update(fields=progress["field"], effects=progress["field"], uncertain=True,
                    ok=False, state="failed", outcome="reply_error", status="FRAME_ERROR", detail=7,
                    completed_steps=0, observation=None)
echo_failure["progress"][0] = [progress[k] for k in progress_names]
echo_failure["evidence"][0] = [evidence[k] for k in evidence_names]
Console._check_driver(echo_failure, 1, ("set", "algorithm", "algorithm-1"), "control_settings")
for error in (2, 3, 6, 10, 255):
    forged = copy.deepcopy(echo_failure)
    forged["evidence"][0][evidence_names.index("frame_error")] = error
    try:
        Console._check_driver(forged, 1, ("set", "algorithm", "algorithm-1"), "control_settings")
    except BenchError:
        pass
    else:
        raise AssertionError("wrong echoed register lost its parser diagnostic")

def timing_variant(exception, late, stationary_expiry):
    record = copy.deepcopy(update)
    record["progress"] = record["progress"][:1]
    record["evidence"] = record["evidence"][:1]
    progress = dict(zip(progress_names, record["progress"][0]))
    evidence = dict(zip(evidence_names, record["evidence"][0]))
    limit = 500 if stationary_expiry else record["deadline_us"]
    record.update(fields=progress["field"], effects=progress["field"], ok=False, state="failed",
                  observation=None, completed_steps=0, stationary_valid_until_us=limit,
                  serviced_us=record["deadline_us"] + 5, uncertain=late,
                  outcome="deadline" if late else "reply_error", status="ILLEGAL_VALUE" if late else "EXCEPTION",
                  detail=13 if late else 2)
    progress.update(acknowledged=False, execution="unknown" if late else "rejected",
                    readback_known=False, readback=0)
    evidence.update(earliest_us=limit + 1 if late else limit - 2,
                    latest_us=limit + 2 if late else limit - 1,
                    delivered_us=record["serviced_us"])
    if exception:
        raw = bytes((record["address"], 0x86, 2))
        raw += wire_crc(raw).to_bytes(2, "little")
        evidence.update(raw_hex=raw.hex(), received_length=5, status="EXCEPTION", detail=2, frame_error=10)
    record["progress"][0] = [progress[k] for k in progress_names]
    record["evidence"][0] = [evidence[k] for k in evidence_names]
    return record

for exception in (False, True):
    for stationary_expiry in (False, True):
        record = timing_variant(exception, True, stationary_expiry)
        Console._check_driver(record, 1, ("set", "algorithm", "algorithm-1"), "control_settings")
        forged = copy.deepcopy(record)
        forged["progress"][0][progress_names.index("execution")] = "rejected" if exception else "acknowledged"
        if exception:
            forged["uncertain"] = False
        else:
            forged["progress"][0][progress_names.index("acknowledged")] = True
        try:
            Console._check_driver(forged, 1, ("set", "algorithm", "algorithm-1"), "control_settings")
        except BenchError:
            pass
        else:
            raise AssertionError("late frame promoted to certain acknowledgement/rejection")
for stationary_expiry in (False, True):
    Console._check_driver(timing_variant(True, False, stationary_expiry), 1,
                          ("set", "algorithm", "algorithm-1"), "control_settings")

for arguments in (("set", "algorithm", "closed-loop"), ("set", "lock-delay", "1/1"),
                  ("set", "lock-delay", "1.0"), ("set", "encoder-resolution", "-1"),
                  ("set", "lock-delay", "65536"), ("set", "lock-delay", "1", "lock-delay", "2")):
    try:
        driver_arguments(arguments, "control_settings")
    except ValueError:
        pass
    else:
        raise AssertionError("invalid exact grammar accepted")

# The wire lifecycle uses the actual formatter record; accepted and terminal
# records may arrive together. Retained inspection cannot consume the result.
from bench_probe_test import Clock, Serial, encoded, reply
import bench_probe

fixture = json.loads(rows[0])["record"]
terminal = None
def handler(command_id, command, arguments):
    global terminal
    if command == "control":
        assert arguments == ["read", "1"]
        terminal = {**fixture, "id": command_id, "command_id": command_id, "operation_id": 101}
        return encoded(reply(command_id, "control", result="accepted", address=1, operation_id=101)) + encoded(terminal)
    if command == "result":
        return encoded({**terminal, "type": "reply", "id": command_id, "command": "result"})
    return Serial.normal(command_id, command, arguments)

clock = Clock()
port = Serial(handler)
events = []
console = bench_probe.Console(port, clock=clock, sleeper=clock.sleep,
                              on_event=lambda event, **data: events.append((event, data)))
console.identify(timeout_s=0.1)
bench_probe.driver_read_campaign(console, timeout_s=0.1, address=1, command="control", driver_args=("read",))
assert events[-1][0] == "summary" and events[-1][1]["mode"] == "control-read"
assert [line.decode().split()[1] for line in port.writes] == ["version", "control", "result", "release"]
parsed = bench_probe.arguments(["--port", "fake", "--log", "unused.jsonl", "control", "set", "algorithm", "open-loop"])
assert parsed.mode == "control" and driver_arguments(parsed.driver_args, "control_settings") == {"algorithm": 1}
print("PASS: 23 actual control terminals, snapshot deadlines, 16-step capacity, exact grammar and retained transport")
