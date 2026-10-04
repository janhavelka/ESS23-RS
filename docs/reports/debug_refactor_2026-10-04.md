# Debugging the production execution path

Baseline `fd4e6be`; final revision is the commit introducing this report.
This user-directed block consolidates debugging around ordinary operations; it
does not execute prompt21 or add another motion implementation.
[Structured results](debug_refactor_2026-10-04.json) and the
[raw evidence archive](debug_refactor_2026-10-04_evidence.zip) retain commands,
responses, firmware, source snapshot, verification logs and the failed attempts.

## Structure and corrections

`debug [off|raw|decoded]` replaces the example console's `sniff` spelling.
It reports diagnostic accounting plus the existing owner, capture and memory
snapshot. Normal typed reads/actions/moves/settings, `result`, `motion-profile`,
`stats`, `drv`, `load` and `memory` retain their own explicit commands and effects.
The [debug workflow](../traffic.md) explains these together. No debug flag changes
motor admission, framing, timeout, response confirmation or retry policy.

- `DebugSession` owns the fixed traffic ring, cursor, correlated request and
  counters; `DebugApp` observes one copied record per loop. Installed
  `TrafficCapture` and the checked ESS decoder retain their existing API.
- Display drops, missed ring records and intentionally skipped mode-change
  records are separate. Turning observation off first retains a partial RX copy;
  mode changes neither clear other readers' history nor consume protocol data.
- A delayed next-frame byte previously caused the observer to mark the preceding
  valid reply incomplete. The runner now lets its unchanged parser establish
  closure before copying that next byte as discarded evidence. Observer on/off
  has the same UART reads, transaction outcome and completion timestamp.
- `MotionReadinessApp` owns normal readiness/envelope checks. The profile session
  groups its view, configuration binding and request, has one deadline and named
  phases. An invalid profile command now fails before admitting a read, and a
  rejected readiness check preserves its output.
- Both host tools use one `debug_session` on the existing connection. An omitted
  `--debug` sends no diagnostic selection. Explicit selection preserves and
  restores the prior mode. Restoration still gets one attempt after a synchronized
  final-query failure; cleanup/logging errors cannot replace a primary operation
  failure. An untrusted stream receives no cleanup commands or replay.

Current FieldCore `41aeaf9c` RS485 task, diagnostics and backend were inspected
read-only. Fixed diagnostic storage and cached snapshots are useful conventions;
its consuming trace reader and sensor response handling are deliberately not
imported. No vendor semantics, access windows or source documents changed.

## Verification

| Requirement / failure case | Production path | Native/build evidence | Hardware evidence | Remaining proof |
| --- | --- | --- | --- | --- |
| Same execution with debug off/raw/decoded | Console callbacks, one owner and runner | Urgent stop/interrupted results survive full output pressure in all modes, both application variants | Normal actions, finite moves and moving stops with raw/decoded output PASS | Independent shaft/electrical measurements |
| Non-consuming raw and translated traffic | TrafficCapture, checked ESS decoder, DebugApp | Delayed service/two frames, overflow, partial RX mode change, counter accounting PASS | 1,534 displayed records; 303 decoded payloads; no capture loss | Best-effort display can drop copies |
| Clear profile lifetime and rejection | MotionReadinessApp / MotionProfileApp | Invalid command and unchanged-output regressions; exact readback/binding tests PASS | Original six-word profile restored exactly | Broader motion semantics unchanged |
| Same-session host diagnostics | debug_session, ordinary campaigns | 203 protocol/harness and 20 motion-campaign tests PASS, including teardown failures | Previous mode restored after every selected campaign | Disconnected stream cannot be restored automatically |
| Software timing under explicit load | Existing timer/load fixture, fixed 37-byte FC03 | Existing delayed/masked-interrupt suites PASS | 10/10 loaded long reads PASS | Electrical timing remains unmeasured |

Final complete CTest run: **55/55 PASS**, including generated artifacts and
18 generator cases. Installed C++11/C++17 consumers PASS with assertions enabled
and strict warnings; five preserved contrast references verify offline.
All three affected firmware environments build: probe, polling-load and
timer-load. Units code/public headers did not change. Independent reviewers
cross-checked the runner/debug, profile/readiness and Python cleanup changes;
the two confirmed cleanup findings were fixed and re-reviewed.

The first integration run caught two Python fixtures missing the newly required
counters; final fixtures and actual C++ JSON output pass. An initial disposable
installed-consumer Release build disabled its assertion-only checks and failed
unused-variable warnings. Its test build now explicitly enables assertions;
both standards pass without weakening warnings. These logs are preserved.

## Final-image hardware

Ordinary `bench_s3_load_timer`, 543,776 bytes, SHA-256
`99237be3c701da5540b4c1aaa4ede4caaae020169f5e54851d3323d900e16d28`.
COM13 ESP32-S3, UART2 TX47/RX48/DE21, ESS23-RS20 node1, 115200 8N1
(SDK divider reports115211). Capture20us, starvation guard85us, reply gap304us,
response timeout200ms and request budget500ms remain unchanged.
The user-supplied combined DE/~RE wiring contract remains explicit.

Plans and cleanup commands were prepared before movement. Native finite targets
remain within the example's0..250 envelope, speed60, saved configured ramps;
all operations go through their regular library preparations and owner.

| Scenario | Drive-reported result |
| --- | --- |
| Snapshot, normal/direct stopped-state stop, release and enable | PASS; release/enable flag observations retained |
| Relative100, then absolute0 | PASS; raw position0→99→0, separate zero-speed checks |
| Normal stop during relative250 | PASS; new RUNNING then stopped/speed0 at raw182; interrupted move retained; return0 |
| Direct stop during relative250 | PASS; new RUNNING then stopped/speed0 at raw132; interrupted move retained; return0 |
| Profile restoration | PASS; `[30,100,100,60,0,5000]` read back exactly |
| Ten ordinary model probes with decoded debug | 10/10 PASS |
| Ten fixed37-byte replies with raw debug and explicit load | 10/10 PASS |

The loaded test used2000us work/10ms,5000us owner delay and128 competing console
bytes. It measured owner gap8036us, capture gap55us and ring high-water37.
Capture-section time was768097/3795885us (**20.23% of one core**); scheduler
estimates were0%/33%, a distinct measurement. The prior unloaded measurement was
24847005/121688014us (**20.42%**), owner gap3885us, capture gap56us and
high-water10. No timing guard was relaxed. Loaded dummy output dropped47 lines;
these are fixture output losses, not transaction failures.

The first post-load status phase correctly refused the still-enabled fixture
before further motor traffic. The raw `settled_final` failure is retained.
An explicit `load 0 0 0` operation, one checked probe and the repeated status
phase (`settled_unloaded`) passed. No motion write was retried.

Final cumulative transport: **303 frames,3213 RX bytes; zero failed transactions,
timeouts, capture faults, RX errors or discarded bytes**. Debug accounting:
1704 observed =1534 emitted +170 display drops; missed0, skipped0,
capture-input drops0, historical ring overwrites1688. The archive contains all
1534 unique displayed records (680raw,854decoded-mode), including303 checked
decoded payloads. Display losses remain visible and never consume bus results.

Final internal free/minimum/largest336608/331448/278516 bytes; PSRAM
8193580/8193580/8126452 bytes. Owner/worker stack headroom2052/3268 bytes.
Ordinary console input drops, output blocking and short writes are zero.
Original configuration/profile are retained/restored; final alarm0, motion1,
enabled, not running, raw speed0, raw position0. Load/monitor/debug are off,
DE released, owner pending/retained/reserved0 and no recovery requirement.

Raw99 for requested100 remains a drive observation, not an exact physical
coordinate claim. Electrical timing, independent shaft observation, calibrated
feedback, negative wire encoding and broader velocity/homing semantics remain
unqualified. The several-hour soak was omitted as requested. No communication
setting, persistence, restart, input reassignment or moving link-loss test ran.
