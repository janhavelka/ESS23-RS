# Fresh independent audit of prompt 02

Audited on 2026-10-03 against baseline
`0bfb161ed2c4afd6dcb9f3a1b0b80cb1626dfa54`. The final commit is the commit
introducing this report (`git log -1 --format=%H -- <report path>`). Re-read the
whole original prompt, execution contract, sequence index and axis stop/sequence
contract; inspected actual owner/Runner, callers, tests and the original
`58cdc10..0bfb161` implementation diff. Prompt 01's reservation, copied request,
stable ID and checked parser handoff remain current and pass their real tests.

Two fresh agents reviewed scheduling/liveness and cancellation/uncertainty in
parallel, without relying on the prior implementation summary. The lead verified
their findings against source, baseline executable reproductions and corrected
production builds. Both reviewers re-audited the integrated corrections.

## Findings, corrections and evidence

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Earlier accepted cancellation survives later deadlines | `cancellationFirst`, centralized `closureDeadline` classification; RECEIVE EMPTY, late byte and qualified closure share cutoff priority | Baseline cancel at 3000 during PENDING became NO_RESPONSE at 13000 or REQUEST_DEADLINE at 4000. Corrected real owner returns CANCELLED with one TX, original context and unknown execution. Six silence, seven frame and four late-byte cases cover earlier/later/tied request/response cutoffs, straddling bounds and historical success | NOT RUN: owner integration remains 03 | Actual application service budgets in 03 |
| Recovery retains ordered evidence across drain passes | `Runner::discard` reuses `observed_`, `observedUs_`, `haveRx_`, `lastRxEndUs_`, `rxUncertaintyUs_` | Baseline 64 ordered bytes then retrograde 65th byte produced RECOVERED. Correction returns READ_ERROR/CLOCK_ERROR, retains fault; regressions cover byte order, watermark behind last byte and watermark behind previous EMPTY | NOT RUN: malformed adapter evidence simulated | Electrical/capture qualification in 04 |
| First cancellation cause survives expired recovery | Existing interrupted RequestId and reserved recovery result remain unchanged | REQUEST cancellation during TX, recovery expiry before TX idle, retained/released control result variants: original request stays CANCELLED/unknown, DE settles, fresh urgent request cannot bypass fault | NOT RUN | CLI recovery/cancellation wiring in 03 |
| Fairness/FIFO, deferred waits and reserved urgency | Existing single pending ring, producer-head scratch and cyclic dispatch; separate result quotas | Re-inspected selection/compaction/wrap pointers; existing three-producer ESS/independent checked FC04 fairness and FC06 stop-shaped priority tests pass | NOT RUN | Measure owner/capture/console limits in 03 |
| Immutable deadlines, physical TX settlement and no replay | Original copied deadline/dispatch/not-before context and real Runner; no new side effects | Existing queued/deferred/WAIT_BUS/SETUP/TX expiry, every-phase cancellation, delayed closure, stale sequence and one-TX/result-retention assertions pass | NOT RUN | Board owner timing in 03 |
| Explicit recovery, reserved result pressure and stale generations | Existing terminal queue cancellation, all-producer invalidation, distinct control result, checked parser settlement | Active/queued/unread/full-pressure, repeated/failed recovery, historical completion after expiry/release and READ_BUDGET tests pass | NOT RUN | Actual settled adapter cleanup in 03 |
| Scope, simplicity and conditional urgent bound | Application-only support; no new storage, ownership flags, queues, tasks or core API | Strict C++11 native/Xtensa compilation, unchanged object sizes, conditional bound independently re-reviewed | NOT APPLICABLE to physical stop: no ESS stop/write added | Real stop and measured service budgets retain later gates |

The first defect occurred with valid Port evidence: cancellation was accepted
while capture was PENDING, then a delayed EMPTY or frame encountered a later
deadline. Three separate code paths gave deadline failure priority merely
because servicing occurred later. Classification now compares immutable
cancellation/request/response cutoffs consistently. Earlier RECEIVE cancellation
wins; earlier request/response expiry and ties retain expiry priority. Closure
uncertainty still fails explicitly; on-time qualified history can still succeed.
Physical TX settlement and its absolute deadline remain unchanged.

The second defect required contradictory adapter evidence. `discard` initialized
its ordering lower bound to zero every call, allowing a regression across the
64-read boundary to clear recovery. It now applies the existing receive timing
rules and keeps those bounds across passes/retries. Only valid observations
advance them. Valid ESP32 adapter cleanup retains the clock epoch and starts
new capture from current time; it does not justify regressing bounds. No new
recovery reader state or reset API was added. Terminal Result/raw bytes remain
untouched, and each service still uses at most 64 read callbacks.

No additional confirmed defect remained in either final source review. Ordinary
fairness remains conditional on a finite urgent workload; the urgent latency
bound requires finite service/capture/backlog/physical settlement budgets.
Recovery cancels old queued urgent work and requires fresh submission. A
bit-identical late RTU reply remains indistinguishable after host recovery;
neither correction claims to remove it or clear an uncertain motor operation.

## Verification and retained evidence

PASS on the final integrated code:

- CMake/Ninja Release, C++11, assertions enabled and `-Werror`: all **15 CTest
  suites**. Scheduling now has **18 test groups**, including the three added
  groups above. Original Runner/Owner and SDK-fake console/adapter/application
  paths also pass. Full log: `build/bench/prompt02_audit_ctest_full.log`.
- Python generator tests **8/8**, transport tests **44/44**; version, ESS ledger
  (221 descriptors / 242 words) and all five preserved serial references checked.
- All three firmware builds: `bench_s3_probe`, `bench_s3_load_poll`,
  `bench_s3_load_timer`. Toolchains unchanged: native MinGW GCC 15.1.0, Xtensa
  GCC 14.2.0, pioarduino 55.03.311, Arduino 3.3.11, IDF libraries 5.5.5.
- Actual Owner/Runner/ESS validator compile separately on Xtensa with C++11,
  `-Os -Wall -Wextra -Werror`; `git diff --check` passes.

Source and retained image hashes, byte sizes and verification dispositions are
in [the audit manifest](ess_release_02_audit_2026-10-03.json). Baseline and fixed
scratch binaries demonstrate both failures without modifying production source
to run the baseline. The committed scheduling suite contains the durable
regressions; no alternate owner/transport implementation was introduced.

Object sizes are unchanged: native/Xtensa Runner 360/296, Owner 216/144,
pending 392/360, result slot 488/456, producer 24/24 bytes. Original owner storage
and stack measurements remain in [the contract](../bus_owner.md#memory-and-next-dependency).
Current Xtensa static Runner frames: discard 96, cutoff comparison 32, closure
classification 64, poll 96 bytes; callee frames are additional, not runtime peaks.

| Built environment | SHA-256 (not uploaded) |
| --- | --- |
| `bench_s3_probe` | `3bcf98744471e554652522812ab7507a26a5d3c372cf43f89bf268a567ce477a` |
| `bench_s3_load_poll` | `18d6de7bc31f0cb570090880405a4d8b1561712301f47d5beaf3d43185293b0d` |
| `bench_s3_load_timer` | `ed1c2a45fc7ec905de37dc9a0bf5b5d8cbfe56434df9854c2c5d77d9b36e5e9c` |

Hardware status: **NOT RUN**. The owner/captured-cancellation/recovery path still
awaits prompt 03 console integration; no image was uploaded, port opened, motor
request sent or runtime latency/RAM/PSRAM/stack measurement made in this audit.
Historical bench state and image remain in the
[prompt 01 audit](ess_release_01_audit_2026-10-03.md); they are not current-owner
hardware proof. Earlier firmware backups/raw artifacts remain preserved.

Reinspected current FieldCore RS485 owner/recovery source read-only at
`b954b0245b74a94d44d8cce775747a573bb17187`, including request/outstanding/result
storage, recovery and backend clear. Bounded request/work/result vocabulary,
queued interruption, module invalidation and a separate recovery outcome remain
compatible. Deliberate differences remain physical TX/DE settlement, portable
callbacks, full checked RTU context/closure evidence, copied larger frames and
application-owned storage; no FieldCore measurement/product/RTOS types were
imported and no reference files changed.

The [original prompt 02 handoff](ess_release_02_2026-10-03.md) remains historical
implementation evidence; this audit supplies corrected cutoff/drain contracts
and regressions for prompt 03. Root owns staging, commit, push and upstream
verification for this completed audit block. No later numbered prompt executed.
