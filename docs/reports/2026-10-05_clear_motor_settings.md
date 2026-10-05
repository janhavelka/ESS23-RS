# Consolidated motor settings and startup motion - 2026-10-05

Follow-up to `f3088155282c8a3cbf25252e7593a3ecae091563`. This changes the shared
Arduino/native-IDF example and presentation, not the framework-independent core.
The current FieldCore RS485 owner header was inspected read-only; no FieldCore
files, bus owner or firmware-owned types were added.

## Implementation and source review

`settings` obtains current configuration and the six-word position profile,
shows subdivision prominently, decodes known enums and separates drive readbacks
from remembered next-move choices. Unknown firmware codes remain explicit.
The short `help` menu exposes the normal workflow; `help advanced`, original typed
commands, `@ID` JSON and copied raw/decoded debug traffic retain diagnostics.
`settings` replaces a former alias for host `config`; that intentional command
change is documented. Existing diagnostic report schemas remain available;
`motor_settings` is an added report type with retained session correlation.

Original ESS function manual physical PDF pages 69-70 show subdivision 400-51200
(default 1000), positioning speed 0-3000 rpm and acceleration/deceleration times
0-2000 ms. Physical pages 15-16 corroborate ms, including 100 ms encoded 0064 and
60 rpm encoded 003C. Factory-default annotations contradict one another, so the
chosen host defaults are **our** 60 rpm/100 ms/100 ms preferences, not a claim about
factory settings. Position range/sign notation remains malformed; existing native
encoding checks are retained. The old example 60-rpm/250-increment restrictions
are removed, not replaced with a claim of maximum-speed physical qualification.
A move still needs nonzero speed at least as large as the drive's starting speed.

Startup performs no movement, enable, I/O reassignment, position clear or save.
Default host angle scale is explicitly ASSUMED 1000 command steps/turn; it is
neither a calibrated angle nor an inferred equivalence to subdivision. Native
steps remain command increments. Host origins, linear travel and unresolved
negative encoding retain their independent prerequisites.

Preparation is bounded to five seconds. Once admitted, move observation gets its
own finite deadline (default 30 seconds; ApplicationOptions allows 1..30000 ms).
Polling is 20 ms until RUNNING is observed, then 500 ms with the same 64-poll cap.
A timeout/poll limit is not proof that the motor stopped; stop and uncertainty
handling remain explicit, and writes are never replayed automatically.

Independent reviewers checked source ranges, operation/result lifetime, formatter
claims and tests. Review caught that reusing the restoration snapshot for settings
could reject fresh reads after configuration changes. The correction keeps the
read-only display separate from saved restoration evidence while sharing the
existing profile-read implementation and bus owner.

## Verification

PASS: 77 native checks, clean source/install C++ consumers and all eight Arduino/
ESP-IDF builds through the full verifier:

```text
python scripts/verify.py --mode full --build-dir build/clear_settings/verify --idf-path C:/pio/packages/framework-espidf --idf-python build/p25/idf-python/Scripts/python.exe
```

Regressions cover valid/invalid speed and ramp boundaries, startup defaults,
>250-increment / >60-rpm preparation, movement lasting more than five seconds,
read-only settings with exactly six FC03 transactions and no state read/write,
desired-vs-actual values, failure retention, result reclamation, in-flight cancel
settlement, and preservation of the original restoration record after subdivision
or selected-target changes. Parser tests retain exact JSON correlation, raw
private-profile evidence and human/advanced help separation.

The first combined native pass caught two tests expecting the old UNKNOWN startup
scale; those expectations were updated to the explicit ASSUMED default. Both the
initial failure log and passing full-verifier logs are retained. Independent
re-review found no further lifetime/cancellation/evidence defect after the snapshot
fix. No failed hardware run occurred in this follow-up.

COM13 Arduino timer image: `build/clear_settings/pio/bench_s3_load_timer/firmware.bin`,
623264 bytes, SHA-256
`e30bf727f9349dafe660eddf5397dc0713cf24c451d9271e942fa76dacea57cb`.
Upload verified flash bytes; same runtime source as full verifier. Bench remains
the recorded ESS23-RS20, node 1, 115200/8N1 (programmed baud 115211).

Finite 5.875-second campaign, with counts/thresholds and cleanup predeclared:

| Case | Outcome |
| --- | --- |
| Fresh-boot `moveby 100 steps` before speed/ramp/scale or motor-read commands | PASS, acknowledged setup/start and new running/arrival |
| `settings` | PASS, subdivision 1000, start speed 30 rpm, actual 60 rpm/100 ms ramps; code 3 remains unknown |
| Host intent 3000 rpm / 2000 ms accel / 0 ms decel, then `settings` | PASS, desired changed, actual drive parameters unchanged; no move at extremes |
| `moveby 100 steps` at 90 rpm, ramps restored to 100 ms | PASS |
| `moveby 300 steps` at 60 rpm | PASS, old experiment limit removed |
| `moveby 36 deg` with boot ASSUMED scale | PASS, exact preparation 100 increments; angle calibration not claimed |
| Direct stop, zero-speed observation, original profile restoration, three probes | PASS |

Each of four moves retained checked running/arrival and no drive alarm; three
polls per move, observed terminal duration 576.376-577.292 ms after admission.
These include the 500 ms completion-poll interval; they are not motor response
time or independent shaft timing.

69 checked frames / 697 RX bytes; zero failures, timeouts, discarded bytes,
capture faults or RX errors. Capture gap maximum 57 us; owner gap maximum 1366 us.
Sampled capture-section time 3391337 us / 16528057 us (~20.5%); scheduler CPU
snapshot 0%/10% is a separate measurement. Free internal RAM 336656 -> 336624 bytes,
free PSRAM unchanged at 8177196 bytes; final stack watermark 2212 bytes. This short
check establishes neither an endurance trend nor electrical timing qualification.

Final drive reports enabled, not running, speed/alarm zero, raw position 10465;
original profile `[30,100,100,60,0,100]` restored. Default host preferences remain
60 rpm/100 ms ramps/ASSUMED 1000 steps per turn. No queued/retained owner results,
recovery requirement or DE assertion; load, monitor and debug remain off.

[Retained raw evidence and exact runner](2026-10-05_clear_motor_settings_evidence.zip)
include verifier/upload logs, firmware/source hashes, human output and JSONL
transport/operation evidence. Archive SHA-256:
`2ad68bf6941e24452684936f9d42c43bf9c5cef621b5dbed78c5de02854a04f8`.
Unchanged core-package SHA-256:
`2ad1e52eed8b2f66418fd13e21ed722b12571e471161b7e766ee0ccd4a8b3af8`.

Candidate identity is the commit containing this report. No tag/release is
published; the final response records its upstream synchronization and CI run.
Physical angle, electrical timing, full-speed behavior and multi-hour endurance
remain unmeasured. This follow-up does not close earlier release/native-family gaps.
