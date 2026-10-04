"""Actual control core/formatter records and strict retained-result transport."""
import copy
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bench_probe import BenchError, Console, MAX_LINE, driver_arguments, wire_crc

rows = subprocess.check_output([sys.argv[1], "--control-fixtures"], text=True, timeout=10).splitlines()
assert len(rows) == 21
candidate = ("set", "algorithm", "algorithm-1", "encoder-resolution", "8000",
             "maximum-effective-current", "1000", "closed-maximum-current", "100",
             "closed-base-current", "40", "open-maximum-current", "100",
             "lock-current", "50", "lock-delay", "3999")
for row in rows:
    fixture = json.loads(row)
    record = fixture["record"]
    arguments = ("read",) if record["driver_kind"] == "read" else candidate
    Console._check_driver(record, 1, arguments, "control_settings")
    assert len(row) < MAX_LINE
    assert record["command"] == record["type"] == "control"
    if record["ok"] and record["driver_kind"] == "update":
        assert record["completed_steps"] == 16 and len(record["progress"]) == 8
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
                          lambda r: r["observation"].update(algorithm_known=not r["observation"]["algorithm_known"])))
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
    if command == "profile":
        assert arguments == ["ess_rs", "control", "read", "1"]
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
assert [line.decode().split()[1] for line in port.writes] == ["version", "profile", "result", "release"]
parsed = bench_probe.arguments(["--port", "fake", "--log", "unused.jsonl", "control", "set", "algorithm", "open-loop"])
assert parsed.mode == "control" and driver_arguments(parsed.driver_args, "control_settings") == {"algorithm": 1}
print("PASS: 21 actual control terminals, 16-step capacity, exact grammar, corruption rejection and retained transport")
