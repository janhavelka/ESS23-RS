# Prompt 03 fresh independent audit

Audited the complete original prompt 03 against baseline `b6d8535`, the actual
production sources, affected callers/tests and the `204c0e9..b6d8535` diff.
Completed 2026-10-04 local time (bench timestamps are 2026-10-03 UTC).
The final commit is the commit introducing this report. Prompt 04 remains
unexecuted. Historical prompt 03 evidence is preserved separately.

## Confirmed findings and corrections

- Successful result lookup now replaces the entire `ResultView`. Reusing a
  recovery or terminal view cannot leave a stale kind, payload or pointers in
  a probe/pending view. Failed lookup preserves the caller's output.
- Adapter recovery checks its original absolute deadline before clearing RX.
  Delayed service after expiry publishes EXPIRED without reinitializing the
  adapter; physical TX settlement and the separate guard still apply.
- Driver diagnostics take the minimum outstanding request/recovery deadline.
  Recovery no longer hides an earlier interrupted active-request deadline.
- A blocked earlier terminal no longer prevents harvesting later completed
  observations. The existing bounded eight-record loop continues after a
  blocked report; retained terminals and FIFO output remain intact.
- Failed reads preserve the last checked model, target and qualified observation
  bounds. Latest transmitted attempt/error and last valid model have separate
  ordering IDs. This also fixes the preceding CLI cache-contract gap: a failure
  does not publish zero or rejuvenate an old value. `model_address` attributes
  the cache independently from `probe_address`; driver `model_operation_id`
  attributes its timestamps. Delayed successful delivery updates only that
  model's delivery time. Recovery advances both harvest watermarks and
  invalidates confidence once, so old retained successes cannot restore it.
- Python retained inspection checks the admitted probe/recovery kind, rejects
  terminal-to-pending regression, and rejects changes to immutable terminal
  evidence. Only query routing fields differ from the original terminal.
  Correlation/framing failure still poisons the session without retry/recovery.
- Corrected stale Runner size and integration text in `docs/runner.md`.

Six new SDK cases reproduced assertion failures before the application fixes.
Eleven malicious Python scenarios reproduce failures against the preceding
validator. A seventh SDK case exercises delayed harvesting of actual owner
completions in reversed bookkeeping order, keeping the latest failed attempt
separate from the older valid model. The test's initial premature assumption
that WAIT_BUS had already started TX was corrected to wait for the fake physical
write; no timing budget or compiler check was weakened.

## Requirement and failure evidence

| Requirement / failure scenario | Actual production path/API | Native/build evidence | Current-image hardware disposition | Remaining proof |
| --- | --- | --- | --- | --- |
| Every bus/load probe has admitted checked ESS validation | `probe`, `BusOwner::admit`, `checkProbe`/`parseProbe`; no direct Runner admission | Real app, owner and parser suites PASS | PASS: 37 checked FC03 model reads | Exact model/firmware and motor state unknown |
| Input remains responsive during TX/RX and output pressure | Console feed 32 bytes/loop; one 64-byte USB enqueue/loop; eight output lines plus one pending line | Active input, full/short USB enqueue, saturated cancel and two completed targets PASS | PASS: ten interleaved status/driver pairs saw busy=true | Full physical USB ring/disconnect NOT RUN; native evidence only |
| Queue/results/cancel/driver diagnostics and correlation | Four ordinary pending, eight request results, separate recovery result; `result`, `release`, `cancel`, `drv` | Full queue/results, stale IDs, reuse, immutable inspection, help parity PASS | PASS: correlated interleaved inspection/release; exact records retained | Physical stop NOT IMPLEMENTED |
| Recovery cannot resume old queued work or bypass TX/deadline | CLI recover -> owner cancellation/generation policy; deadline-gated UART clear; `service(now,recoveryReady)` | Unsettled DE, full results, distinct control/interrupted results, expired idle recovery and minimum deadline PASS | PASS: explicit recovery in delayed/boundary runs; no replay | Stuck physical transmitter/full retained pressure NOT RUN live |
| Observation, delivery and settlement remain separate | Qualified closure bounds; independent attempt/model IDs; record delivery; post-service recovery guard | Delayed timer age, repeated queries, blocked output, reversed harvest, multi-target exception/CRC PASS | PASS: 10-ms injected delay, immutable increasing age; pre-TX failure keeps model/bounds/delivery | External wire timestamps/cache-off NOT RUN |
| Python interleaving stays strict and bounded | `Console.begin`/`wait`, retained operation table, poisoned sessions | 70 cases PASS; old recorded campaigns replayed with fragmented reads | PASS: new image accepted/terminal/query records validate | No automatic retry or recovery |
| One UART/task owner, portable callbacks, memory placement | PSRAM App; internal UART/capture and load stack; worker protected text slot only | Actual polling/timer/load targets build; no worker owner/UART/USB calls | PASS: bounded worker/console campaigns and watermarks | SDK fake does not schedule worker; no general real-time guarantee |
| Upper owner-delay boundary remains fail-closed | Unchanged 20-ms setup/TX budget versus 20-ms service delay plus jitter | Existing load regression PASS | PASS of expected failure: TX_TIMEOUT, accepted TX=0, unknown=false; retained result, explicit cleanup/recovery then successful read | Not every admitted load setting meets the service envelope |

## Verification

Strict C++11 Release/assertions with GCC 15.1.0 and warnings as errors:
**16/16 CTest suites PASS**, including 21 shared actual-application SDK groups
and two additional load groups. Generator tests **8/8 PASS**; Python console
tests **70/70 PASS**; version, ESS ledger and preserved contrast checks PASS.
All three PlatformIO probe/load environments PASS with pioarduino 55.03.311,
Arduino 3.3.11, IDF 5.5.5 and Xtensa GCC 14.2.0. Diff whitespace check PASS.

Three parallel reviewers inspected console/correlation, production/platform,
and Python/failure coverage. Findings were verified against current code and
reproductions; corrected source, caller formatting and final tests were
re-reviewed independently. No confirmed scoped production defect remains.

FieldCore `dd5b9a3153c1ea6f7b2c4875b751268a75222ce4` was reinspected read-only:
`Rs485Task` request/recovery/result owners, backend write/clear, `Rs485Cli`, and
the device-module cache/finish path. Compatible conventions are explicit
owner admission/results, deadline-gated cleanup and pending/unavailable states.
Deliberate differences are non-consuming retention with explicit release,
independent recovery capacity, checked ESS parsing/physical evidence and no
sensor retries, product or framework types. No FieldCore file was edited.

## Current firmware and bench measurements

COM13 was reidentified as USB 303A:1001, serial `3C:0F:02:CD:6B:98`, protocol 2
timer firmware, workload disabled and settled DE before upload. The uploaded
`bench_s3_load_timer` image is `build/bench/prompt03_audit_timer_firmware.bin`,
SHA-256 `af531db1a18a086ddce5e1ab61c025839efff1523ef37665a72e395a5ce5979e`.
RS485 remains UART2 TX47/RX48/DE21 active-high, node 1, 115200 8N1.
Only FC03 model register 0x0000/one-word probes ran: request
`010300000001840A`, checked reply `0103024EEA0C6B`. `0x4EEA` remains responder
evidence. No motor setting write, motion or physical stop occurred.

| New-image campaign | Checked successes / probe terminals | Sent commands / JSON lines | Host fixture lines | Transport duration |
| --- | ---: | ---: | ---: | ---: |
| Unloaded regression | 10 / 10 | 53 / 63 | 0 | 5407–5496 us |
| Work 2000 us, delay 5000 us, payload 128 | 10 / 10 | 64 / 74 | 141 | 14537–17584 us |
| Active local queries, retained inspection/release, cleanup | 11 / 11 | 68 / 79 | 137 | 5480–18661 us |
| Delay 10000 us, repeated cache queries, recovery/cleanup | 4 / 4 | 30 / 35 | 45 | 5470–25573 us |
| Deliberate delay 20000 us and explicit cleanup | 2 / 3 | 25 / 29 | 0 | Expected pre-TX failure 42005 us |

Measurement-window fixture admissions/drops were 138/0, 91/0 and 38/0 for
loaded/interleaved/delayed runs. Full host fixture counts above include traffic
outside those measurement windows. Loaded owner/capture maximum gaps were
8035/54 us; interleaved 7024/56 us; delayed 11007/54 us. Local host query
round trips were 0–32 ms under the Windows clock; these are not hard latency
bounds. Delayed delivery lag was 7745–7769 us; repeated ages advanced
22→68, 33→73 and 25→72 ms. The expected pre-TX failure preserved the older
model and timestamps while age advanced 30→124 ms.

Internal free/minimum/largest block: 336672/331512/278516 bytes.
PSRAM free/minimum/largest: 8358444/8358444/8257524 bytes.
Owner stack minimum observed headroom: 4836 bytes; worker: 3268 bytes.
Loaded scheduler CPU estimates: core 0 about 0%, core 1 31–34%.
App sizes remain 27936 native / 27128 Xtensa bytes; Console 3392/3344,
Runner 384/320 and Result 80. UART object 1696 and load object 4816 bytes
(including 4096-byte stack) remain internal. No capacity expansion was needed.

Ending workload is 0/0/0, DE settled, no recovery required, no pending,
reserved or retained work, no queued output. Final diagnostics show 245 input
lines, zero input drops, output blockage or short writes. The deliberate failure
was inspected and released; recovery and its result were explicitly released.
Original 16-MiB CO2control backup is unchanged:
`85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.
The [machine-readable evidence](ess_release_03_audit_2026-10-04.json) retains
exact record counters, measurements and artifact paths/sizes/hashes. Local
build/bench artifacts are preserved; historical logs were not overwritten.

## Handoff

Prompt 04 inherits the real owner/console integration, unchanged bounded
admission/retention limits and deadline budgets, corrected cache attribution
and strict transport. Callback views borrow bytes only during synchronous
formatting and are replaced on successful lookup. Core remains framework-free;
all application calls stay in one cooperative owner context. External electrical
timing, physical USB saturation/disconnect, cache-off, motion/stop and soak
qualification remain separate and unperformed. No qualification was inferred
from the expected pre-TX failure or native fakes. Root completes commit/push;
the final hash is obtainable from the commit introducing this report.
