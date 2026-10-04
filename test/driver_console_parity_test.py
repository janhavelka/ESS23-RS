"""Check actual bounded driver-settings outcomes through the Python parser."""
import copy
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bench_probe import BenchError, Console, MAX_LINE, unique_object

expected = {"read", "unknown_read", "full_update", "partial_lost_ack", "cancelled",
            "readback_mismatch", "deadline", "wrong_echo", "late_delivery", "unqualified",
            "unconfirmed_echo", "late_closure", "unconfirmed_read", "unconfirmed_readback"}
output = subprocess.check_output([sys.argv[1], "--driver-fixtures"], text=True, timeout=10)
seen = set()
for line in output.splitlines():
    fixture = json.loads(line, object_pairs_hook=unique_object)
    name, record = fixture["case"], fixture["record"]
    assert name in expected and name not in seen, name
    assert len(json.dumps(record, separators=(",", ":"))) < MAX_LINE
    Console._check_driver(record, 1, None)
    for mutate in (lambda r: r.update(active_settings_known=True),
                   lambda r: r.update(configuration_generation=0),
                   lambda r: r.update(address=2),
                   lambda r: r["evidence"][0].__setitem__(0, 1),
                   lambda r: r["evidence"][0].__setitem__(5, "00")):
        broken = copy.deepcopy(record); mutate(broken)
        try:
            Console._check_driver(broken, 1, None)
        except BenchError:
            pass
        else:
            raise AssertionError((name, "malformed provenance accepted"))
    seen.add(name)
    if record["ok"]:
        broken = copy.deepcopy(record)
        broken["evidence"][-1][9] = False
        try:
            Console._check_driver(broken, 1, None)
        except BenchError:
            pass
        else:
            raise AssertionError((name, "unconfirmed response accepted as success"))
        for outcome in ("timing_unqualified", "unconfirmed_response", "deadline", "readback_mismatch"):
            broken = copy.deepcopy(record)
            broken.update(ok=False, state="failed", outcome=outcome, status="ILLEGAL_VALUE", detail=123456)
            try:
                Console._check_driver(broken, 1, None)
            except BenchError:
                pass
            else:
                raise AssertionError((name, "successful evidence disguised as failure", outcome))
assert seen == expected, (seen, expected)
print(f"PASS: {len(seen)} actual core/console driver terminals accepted by Python")
