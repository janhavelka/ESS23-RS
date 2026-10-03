# ESS release 01: fresh independent audit

Audited the complete original prompt 01 and execution contract on 2026-10-03,
starting from clean, synchronized `main` at
`75ad28a46f24a579ee6f1abea326599b83174c50`. The final audit revision is the
commit introducing this report and [audit manifest](ess_release_01_audit_2026-10-03.json).
The [original implementation report](ess_release_01_2026-10-03.md) and its
manifest preserve their original native and hardware provenance. This audit
executes no subsequent numbered prompt.

## Findings and corrections

Two deadline-reporting bugs were confirmed against the actual Runner and
reproduced through the real BusOwner path:

- With physical TX end 2160 us, relative response budget 1300 us and absolute
  deadline 3550 us, closure bounds [3540,3560] are definitely beyond the earlier
  relative cutoff 3460 us. Runner previously returned `TIMING_UNCERTAIN` because
  the later absolute deadline straddled closure. It now reports `PARTIAL_RESPONSE`.
  The same priority error affected unqualified closure and no-response expiry.
- With relative cutoff 2890 us and a received byte stop in [2890,2910], Runner
  previously returned `TIMING_UNCERTAIN` and left closure bounds zero. Including
  the required 350-us final gap gives candidate closure [3240,3260], which is
  definitely late. It now retains those bounds and reports `PARTIAL_RESPONSE`,
  preserving the three-byte raw prefix and accepted TX count.

One private closure classifier now serves qualified frames, candidate closure
on watermark expiry and rejected late bytes. The earlier latest timeout decides
the failure reason; acceptance must still fit both budgets for all retained
TX/RX uncertainty. Comparison uses subtraction rather than adding a relative
deadline, avoiding clock overflow. Neither budget is renewed. Receive admission,
read budget, parser barrier and recovery policy remain intact.

The source reviewer also questioned zero closure bounds for uncertain DE release
before a response. No RX closure exists in that phase; zero bounds correctly
retain absence of response evidence. The contract now states this explicitly.
No fabricated closure or additional diagnostic framework was added.

## Requirement coverage rechecked

The lead and parallel source/ownership and failure-test reviewers inspected
actual code, callers, native fixtures and the complete `75ad28a` implementation
diff. Findings were checked against source before correction, then both reviewers
independently re-audited the integrated Runner correction. No further confirmed
prompt 01 defect remains.

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Cooperative bounded FIFO and one active transaction | `BusOwner::admit/service`, portable Runner callbacks | Transient copies, FIFO/wrapped compaction, bounded 64 reads | NOT RUN for owner integration | Prompt 03 connects real producers |
| Separate queue/results, immutable context and stale IDs | Reserved `ResultSlot`, copied `Expectation`, `result/release` | Zero/full storage, pressure, generation exhaustion, release/reuse, foreign IDs, repeated polls | NOT RUN for owner storage | PSRAM/runtime owner placement in 03 |
| One terminal per admission; rejection/queue expiry/transport distinct | `BusAdmission`, `Outcome`, completion reservation | Existing invalid, expiry and retained-result scenarios pass | NOT RUN for owner result flow | CLI correlation belongs to 03 |
| Absolute deadline throughout queue/setup/TX/RX | Copied `Request::deadlineUs`, Runner action guards | Queue, WAIT_BUS/SETUP no-TX, active TX expiry, cleanup and no replay | NOT RUN for absolute feature | Board path in 03 |
| Earlier closure budget and uncertainty | `closureDeadline`, `completedFrame`, RECEIVE expiry | Nine added scenarios plus exact deadline, straddling and historical closure tests | PASS changed-Runner regression; injected expiry NOT RUN | Independent timing evidence in 04 |
| Checked FRAME before another dispatch; exceptions distinct | `Validator::checkReply`, `Runner::rejectFrame` | Barrier, address/CRC/write echo rejection, known/unknown checked exceptions | NOT RUN for new owner handoff | Board integration in 03 |
| Recovery refuses queued dispatch and retains original outcomes | `needsRecovery`, empty-queue `recover` | Faulted queue expiry, full reserved results, uncertain TX and preserved outcomes | Ending Runner idle/DE released/no fault PASS | Queue cancellation/disposition belongs to 02 |
| FC03 one/16 words, identical FC06 ack, four FC10 windows | Real Runner and `essValidator()` | 7/37-byte replies, echo modes, 13/15/19/21-byte write requests, exact storage bounds | PASS one-word read only; raw writes NOT APPLICABLE | Long-frame/write qualification is separate |
| Portable storage/construction, no firmware dependencies | Application support under `examples/common`, caller arrays | Strict C++11 native/Xtensa checks; installed core unchanged | Existing PSRAM/stack observations retained below | No framework ingress needed for cooperative callers |

Current FieldCore HEAD remains `ec89f52c037a1f4bd69b296505d6f96533dbca73`.
Its transaction, queue/result ownership, ingress, module binding, backend and
RS485 CLI were reinspected read-only; unrelated dirty settings work was preserved.
Compatible terms remain copied request, original deadline, one active transport,
reserved result and parser handoff. Deliberate differences remain cooperative
access, non-consuming inspection/explicit release, owner/slot/generation IDs,
256-byte storage versus eight-byte TX, explicit FC06 echo, and qualified RTU
interval/watermark closure versus untimed fixed/suffix collection and prefix
stripping. No FieldCore, product, sensor lane, health or retry types were imported.

## Final software verification

The added precedence test failed against the baseline before correction, then
passed with the production fix. `testEffectiveClosureBudget` adds eight
qualified/unqualified/no-response budget scenarios and one relative byte-expiry
scenario, without optional traces. Both reviewers verified the final code and
the ownership reviewer independently ran the focused Runner/BusOwner suites.

- Release native build with `-Werror`: **14/14 CTest suites PASS** after integration.
- Explicit Python: **8 generator + 44 bench cases PASS**. Version and ESS
  generated files current; all five contrast references verified offline.
- Strict C++11 owner suite compilation/execution and Xtensa Runner compilation:
  **PASS** with `-Wall -Wextra -Wpedantic -Werror`.
- `bench_s3_probe`, `bench_s3_load_poll`, `bench_s3_load_timer`: **PASS**.
- `git diff --check`: **PASS**. No public core/package/generated-file change.

Public APIs, storage layouts and measured sizes are unchanged: native/Xtensa
Owner 88/44, PendingSlot 344/320, Completion 424/400, ResultSlot 440/416,
Runner 352/288 and Result 56/56 bytes. Four queued plus six retained slots
still use 4104/3820 bytes, separately from Runner buffers/trace. Runtime stack
headroom is hardware evidence below; earlier compiler-local stack figures are
historical and do not qualify new total call-stack use.

## Current-image hardware regression

COM13 was rechecked as USB `303A:1001`, serial/MAC `3C:0F:02:CD:6B:98`.
Before upload, version/config/load/status/memory showed the existing timer
console, workload disabled, no active request, DE released and no recovery fault.
Original backup and prior image hashes were verified unchanged. The new retained
image `build/bench/prompt01_audit_timer_firmware.bin` is 334944 bytes, SHA-256
`808585c604cf245fef8430a92bf0063617575eaacaea63de9ce7ac9d6042740f`.
The upload verified written hashes; the upload log and all snapshots/campaigns
are hashed in the manifest. Host sessions used explicit DTR/RTS false.

Settings remained address 1, UART2 TX47/RX48/DE21 active high, 115200 8N1,
response 200000 us, reply-gap override 304 us, t1.5/t3.5 750/1750 us,
setup/hold 20/20 us, bus/TX/capture limits 100000/20000/10000 us, timer 20 us.
All motor traffic was the checked non-changing one-word model probe; raw model
remains `0x4EEA`, with exact identity/settings unknown.

| Campaign under `build/bench/` | Result | Probe latency |
| --- | --- | --- |
| `prompt01_audit_unloaded_20261003.jsonl`: 10 probes, 50-ms interval | PASS 10/10 | 5382–5437 us; mean 5416.9 us |
| `prompt01_audit_loaded_20261003.jsonl`: 10 probes, work 2000 us / 10 ms, owner delay 5000 us, console 128 bytes | PASS 10/10 | 11888–16888 us; mean 14439.9 us |
| Explicit `load 0 0 0`, then `prompt01_audit_final_20261003.jsonl`: 3 probes | PASS 3/3 | 5399–5419 us; mean 5408.0 us |

The loaded campaign measured active owner/capture maximum gaps 8092/45 us,
72 complete console lines accepted and three dropped. The later cleanup
snapshot, after continued load, recorded 185/18; these are different measurement
windows. No command or probe failed. Capture section 117312/748940 us is about
15.7% of one core; scheduler owner-core busy was 32%, a separate observation.
Internal free/minimum/largest memory was 336848/331688/278516 bytes; PSRAM
8380332/8380332/8257524. Owner/worker stack headroom was 5712/2952 bytes.

Ending cached evidence: 23 started/23 FRAME, zero failures/timeouts/capture faults/
RX errors, codec OK, idle owner, DE released, recovery false, workload 0/0/0.
No motion, motor setting change or motor-stop cleanup occurred. Original backup
remains intact; ignored raw bench files are retained as evidence.

Owner and absolute-deadline feature hardware tests remain **NOT RUN**, because
the shipped console integration belongs to 03. This regression proves compatibility
of the changed Runner path; injected timeout corrections have native evidence.
Raw write/motion tests are **NOT APPLICABLE** to 01. Electrical/cache-off/long-frame
and extended soak qualification remain open. Prompt 02 is still the next dependency;
it must preserve these deadline, reservation, validation and result-lifetime rules.
