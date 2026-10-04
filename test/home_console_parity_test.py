"""Validate actual core/console homing terminals and reject forged provenance."""
import copy
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bench_probe import BenchError, Console, MAX_LINE, unique_object, wire_crc

expected = {"current_origin", "index_origin", "stale_homed", "lost_trigger_ack", "cancelled", "deadline", "unconfirmed_trigger", "exception", "late_trigger_echo",
            "timing_unqualified", "drive_fault", "unexpected_activity", "nonzero_position", "delayed_completion_deadline", "wrong_function_exception"}
seen = set()
for line in subprocess.check_output([sys.argv[1], "--home-fixtures"], text=True, timeout=10).splitlines():
    fixture = json.loads(line, object_pairs_hook=unique_object)
    name, record = fixture["case"], fixture["record"]
    assert name in expected and name not in seen
    assert len(json.dumps(record, separators=(",", ":"))) < MAX_LINE
    Console._check_home(record, 1, None)
    if name == "exception":
        assert record["status"] == "EXCEPTION" and record["execution"] == "rejected"
    if name == "wrong_function_exception":
        assert record["status"] == "FRAME_ERROR" and record["execution"] == "unknown"
    if record["ok"]:
        # A final zero reply closed on time can be delivered after the retained
        # request deadline. Its delivery never changes the physical outcome.
        delayed = copy.deepcopy(record)
        delayed["serviced_us"] = delayed["zero_evidence"][11] = record["deadline_us"] + 20
        Console._check_home(delayed, 1, None)
    for mutate in (lambda r: r.update(address=2), lambda r: r.update(configuration_generation=0),
                   lambda r: r["prerequisites"]["qualified_parameters"].__setitem__(0, 34),
                   lambda r: r["prerequisites"]["qualified_parameters"].__setitem__(1, 61),
                   lambda r: r["prerequisites"]["qualified_parameters"].__setitem__(2, 31),
                   lambda r: r["prerequisites"]["qualified_parameters"].__setitem__(3, 101),
                   lambda r: r["staging_words"].__setitem__(4, 1), lambda r: r["staging_evidence"].__setitem__(4, 8)):
        broken = copy.deepcopy(record); mutate(broken)
        try: Console._check_home(broken, 1, None)
        except BenchError: pass
        else: raise AssertionError((name, "forged homing accepted"))
    if record["ok"]:
        for mutate in (lambda r: r.update(homed_low_observed=False), lambda r: r["zero_evidence"].__setitem__(6, False),
                       lambda r: r["staging_evidence"].__setitem__(11, r["trigger_evidence"][11] + 1),
                       lambda r: r.update(ok=False, state="failed", completion="not_observed", outcome="deadline", uncertain=True)):
            broken = copy.deepcopy(record); mutate(broken)
            try: Console._check_home(broken, 1, None)
            except BenchError: pass
            else: raise AssertionError((name, "false transition accepted"))
        if record["method"] != 35:
            for field in ("activity_evidence", "low_evidence"):
                for index, value in ((3, 1), (6, 0x78)):
                    broken = copy.deepcopy(record)
                    raw = bytearray.fromhex(broken[field][2]); raw[index] = value
                    raw[-2:] = wire_crc(raw[:-2]).to_bytes(2, "little")
                    broken[field][2] = raw.hex()
                    try: Console._check_home(broken, 1, None)
                    except BenchError: pass
                    else: raise AssertionError((name, "faulted transition accepted", field))
            for field in ("activity_evidence", "low_evidence", "completion_evidence", "zero_evidence"):
                broken = copy.deepcopy(record); broken[field][0] = 1
                try: Console._check_home(broken, 1, None)
                except BenchError: pass
                else: raise AssertionError((name, "uncorrelated transition accepted", field))
    seen.add(name)
assert seen == expected
print(f"PASS: {len(seen)} actual core/console homing terminals accepted by Python")
