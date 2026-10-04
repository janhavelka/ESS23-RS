"""Check actual core/console velocity terminals through the Python transport."""
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from bench_probe import Console, MAX_LINE, unique_object

expected = {
    "readiness_before_tx", "readiness_after_staging", "duration_after_staging",
    "absent_activity", "service_missed", "success", "radians", "stop_failure",
    "late_trigger", "stop_deadline_after_ack",
}
output = subprocess.check_output([sys.argv[1], "--velocity-fixtures"], text=True, timeout=10)
seen = set()
for line in output.splitlines():
    fixture = json.loads(line, object_pairs_hook=unique_object)
    name, record = fixture["case"], fixture["record"]
    assert name in expected and name not in seen, name
    assert len(json.dumps(record, separators=(",", ":"))) <= MAX_LINE
    Console._check_velocity(record, 1, None)
    seen.add(name)
assert seen == expected, (seen, expected)
print(f"PASS: {len(seen)} actual core/console velocity terminals accepted by Python")
