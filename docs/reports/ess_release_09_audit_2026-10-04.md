# Prompt 09 independent audit

Fresh audit of the complete [original prompt09](../prompts/ess_release/09_first_relative_motion.md),
its production code, callers, tests and baseline diff at `47e9575`. The final
commit is the commit introducing this report. Implementation/software checks and
the final-image read-only COM13 regression: **PASS**. Physical positive/negative
moves, independent shaft observations and dynamic-stop latency: **NOT RUN**;
required electrical/FC06 echo and units/sign/basis/ramp/input qualification remain
unresolved. The existing gate was preserved. Prompt10 was not started.

[Structured results](ess_release_09_audit_2026-10-04.json) and the
[evidence archive](ess_release_09_audit_2026-10-04_evidence.zip) preserve actual
console lines/traffic, firmware, source, tests, diagnostics and SHA-256 manifests.
The earlier [implementation evidence](ess_release_09_2026-10-04.md) is historical
and remains intact.

## Confirmed findings and corrections

1. Readiness was checked only at preparation. Staging and trigger could remain
   queued or deferred beyond that evidence's maximum age. Both yielded write
   deadlines now use the earlier immutable readiness/operation bound, including
   queue, setup, physical TX and qualified closure. New writes require time
   strictly before the cutoff; preparation rejects at equality with unchanged
   output. The application settles an expired unadmitted continuation immediately.
   Timely staging delivered too late retains applied/uncertain setup and never
   triggers. Timely trigger closure delivered later may continue observation
   under the unchanged operation deadline. TX expiry interlocks the runner,
   retains unknown effects and lets physical transmission settle without replay.
2. A consumed coordinate reference could hide stale limit evidence behind its
   cached `nowUs`. Preparation now checks a bounded reference copy at operation
   time through the existing `preparePosition`, and requires its freshness to
   cover the write budget. Refresh the reference or reduce the budget. Optional
   unestablished reference data remains unnecessary for unrestricted native
   relative displacement. Caller reference and rejected outputs stay unchanged.
3. Python rejected legitimate equal delivery/closure-lower-bound timestamps,
   including same-tick cancellation after staging. Inclusive comparisons now
   match the core while preserving ordering and strict correlation. Retained
   validation independently enforces write cutoffs and rejects inconsistent
   activity/final-observation proof.
4. `KeyboardInterrupt` after a move was transmitted but before acceptance could
   leave the transport synchronized and label cleanup `not_required`. Interrupted
   acceptance/result waits now poison framing; cleanup is `unknown`, with no
   replay or further commands in that session.
5. Move help/capabilities required fewer callbacks than dispatch. Both require
   `startMove`, `snapshot` and `axis`; partial-host cases cannot advertise a
   usable move route. Stale README claims that moves were unimplemented were
   corrected. No register policy, setting, retry, task, queue or public layout
   was added.

## Requirement and failure coverage

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Exact common/native finite relative preparation, complete parameter/flag/readiness checks | `prepareMoveRelative` calls existing `preparePosition`; fixed trigger flags | Signed32, exact/rounding/common parity, full rejection/unchanged-output matrix; current-time reference and budget tests PASS | Positive/negative move gates reject at ID0, zero TX PASS | Physical sign, units, basis, device bounds and ramp relationship |
| Serial-only path; no implicit input or limit changes | `MovePrerequisites` plus configuration provenance | Unresolved input effects reject; qualified software fixture needs no switches/loads PASS | Config payloads identical before/after PASS | Actual assigned input effects; any I/O change belongs to15 |
| Reviewed staged parameters then trigger | `nextMove` FC10 five-word window, then fixed FC06; checked `advanceMove` | Exact19-byte request, echo/address/CRC, exceptions and wrong-step tests PASS | No writes transmitted | Physical stage/start behavior |
| Partial setup or lost ACK retained uncertain; no replay | Separate staging/trigger evidence and axis conflict reservation | Real-owner checked FC10 exception, wrong stage echo, lost trigger, failure matrices, repeated service/recovery PASS | Timing gate rejects before TX PASS | No transport-only proof of non-execution |
| Same-axis reservation and second producer conflict | `admitAxisWrite`, shared action reservation | Target/speed/configuration injection rejects between stage ACK and trigger; original values alone trigger; configuration changes cancel stale continuations PASS | Gated moving path NOT RUN | Future writers must use admission policy adapter |
| Stop supersedes sequence while physical TX settles | Existing urgent admission and action operation | Stop/cancel at eight simulated sequence phases, retained-result saturation, TX settlement PASS | Normal/direct stop reject without TX PASS | Physical acceleration/movement and dynamic-stop latency |
| Readiness and immutable operation deadline | Capped `PreparedMove.deadlineUs` copied to owner; local deferred expiry settlement | Queue/setup zero TX, TX expiry, queue pressure, equality/overflow, on-time capture delivered late, gap beyond cutoff, no retry PASS | Existing read-only capture/regression PASS | Electrical timestamp qualification remains open |
| New relevant completion observations | `advanceMove`: new RUNNING then later ARRIVED/not-RUNNING | Old arrival, faults, stale tokens/generations/time, delayed closure, poll bounds PASS; Python proof validation PASS | Cached timestamps unchanged and age grows PASS | Actual drive sample time and independent shaft movement |
| API/CLI/harness parity and cleanup | Existing console move/profile routes, strict Python finite campaign | Actual SDK-fake app, complete-host help/caps, exact parser, interleaving, interrupted acceptance/wait cleanup PASS | Finite harness rejects gate without cleanup writes PASS | Live finite scenario and physical cleanup NOT RUN |
| Bounded portable state, no duplication | Installed public headers/core, one owner and PSRAM records | 23 CTest suites, installed consumer, generation/catalogue checks, four firmware builds PASS | RAM/PSRAM/stack and owner/capture gaps recorded PASS | Moving-path stack/timing still unqualified |

## Independent review and source ownership

Manual/command, sequence/completion and console/correlation reviewers worked in
parallel on disjoint paths. Findings were checked against actual code and
executable reproductions. The manual reviewer independently re-reviewed core
readiness/reference fixes; the console reviewer reviewed the application fixes;
the sequence reviewer audited Python proof handling. The root reviewed the
integrated diff and ran final verification and hardware. No reviewer claims a
physical motion test from a fake fixture.

Original function-manual pages13–16,68 and70 were visually rechecked: position
window/order, fixed start command, alarm/status polarity and input effects.
No undocumented range/default was promoted to qualification. Current FieldCore
read-only reference HEAD: `7c356aff59132ab7ab7be6e3ec26a599ede6b99e`.
Its `Rs485OwnerTransaction.h` and `Rs485Task.cpp` supply request/WaitUntil,
deadline-before-configure/clear/write and bounded ownership vocabulary. Deliberate
differences remain: ESS19-byte staging, separate axis reservation, checked full
FC06 ACK, retained physical closure/uncertainty, and no sensor retry/prefix-echo
policy or framework types. FieldCore was not edited.

## Final verification and bench

Native GCC15.1 C++11 with warnings as errors: all23 CTest suites PASS. All137 Python
harness cases, eight generator negative cases,13 operation inventory cases, version/catalogue and
five contrasting-reference checks PASS. Installed-core consumer compiles public
headers with no example include path and passes. Four PlatformIO environments
(`bench_s3_units`, `bench_s3_probe`, polling and timer load/probe) build PASS;
the timer image upload verified flash hashes.

COM13 USB `303A:1001`, serial `3C:0F:02:CD:6B:98`; firmware0.6.0/protocol2;
TX47/RX48/DE21, node1 at115200 8N1. Image409760 bytes, SHA-256
`090cbea2ffec208731e11816085f7acec364fda0fc66be151f9d7213e8e1d240`.
The original16MiB firmware backup retains SHA-256
`85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.
Preflight used local controls only; no fault/statistics reset or recovery.

The final campaign performed28 FC03 frames/288 RX bytes (identity, config/state
before/after,37-byte capture read and ten probes), then the required ten-probe
stress regression: **38 frames/358 RX bytes**, zero failures/timeouts/cancels,
capture faults/RX errors/echo/discarded bytes. Config/state payloads match exactly:
model4EEA, firmware0029, subdivision1000, encoder setting4000, raw algorithm3
still unknown, input assignments1/2/3/0, raw alarm0/motion1 and speed/position0.
No physical resolution, native coordinate basis or shaft standstill is inferred
from these raw observations. Move/action gate requests changed no bus counters.

Ten campaign probes took5437–5496us in the runner; application delivery followed
qualified closure by228–272us, with closure interval widths28–46us. Host probe
round trips were15–32ms; multi-read config94ms, state62–63ms, identity31ms.
These are observed read-only measurements, not a stop-latency bound.

Ending RAM: internal free/minimum336656/331496 bytes, largest278516;
PSRAM free/minimum8310316, largest8257524. Owner stack headroom3604 bytes;
worker3268. Owner/capture maximum gaps139/53us, capture threshold85us, no
exceedance. Owner loop static frame784 bytes versus historical768; measured
headroom is32 bytes lower than3636 previously. No stack enlargement or whole-record
temporary was introduced. Production motion gates exclude moving-path qualification.
Public layout unchanged: target/native `MoveContext`824/944, `PreparedMove`72/80,
`MovePrerequisites`56/56 bytes; maximum retained move line3995/4096 bytes.

Ending load0/0/0, monitor off, DE released, owner pending/retained/reserved0,
no axis reservation, output/input drops0 and no recovery requirement. Read-only
cleanup is settled; physical move/stop cleanup was not exercised. Remaining
motion prerequisites carry forward explicitly to10–11; no default or retry
was changed to obtain physical success.

Host-only verification notes: an early root test used the wrong enum namespace
and a nonexistent capacity name; compilation failed, both were corrected before
running rebuilt tests. The TX-expiry test initially stopped servicing at logical
completion rather than physical fault settlement; it now checks the independent
DE settlement correctly. A UTF-8 console-print wrapper failed after successful
firmware builds; actual build exit0 and four success records were retained.
