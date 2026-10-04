"""Actual control core/formatter records and strict retained-result transport."""
import copy
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bench_probe import BenchError, Console, MAX_LINE, driver_arguments

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
