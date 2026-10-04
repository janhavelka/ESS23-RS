# Prompt 13 fresh independent audit

Baseline `784d801`, freshly fetched and synchronized with a clean worktree.
This audit re-read the complete prompt/execution contract and inspected the
actual implementation diff `b4036eb..784d801`, current sources, callers and tests.
The final commit is the commit containing this report. Scope remains prompt 13;
no later prompt, device save/restore or new paired-write form was implemented.
[Current API contract](../ess_driver_settings.md),
[exact console/raw/upload evidence](ess_release_13_audit_2026-10-04.json).
The implementation report retains its separate historical image and campaigns.

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Typed setting reads and seven single-word writes; guarded limit pairs | Installed `DriverSettings.h`, prepare/next/advance/get, checked ESS codecs | Boundary, whole-candidate/no-work, unknown codes, order, installed consumer PASS | Four-window reads PASS; all six setter gates reject before TX | Pair write forms, signed/native limit interpretation |
| Requested/ACK/readback/active and partial progress | `DriverProgress`, `DriverEvidence`, immutable context and effect mask | Lost replies, mismatch, exception, cancellation, full fourteen-step group PASS | Reads and retained inspection/release PASS | Physical activation/persistence and restoration NOT RUN |
| Unconfirmed readback cannot settle a write | `advanceDriver`, prerequisite provenance validation; Python `_check_driver` | Baseline defect reproduced; every read step, partial ACK and forged success regressions PASS; fourteen real console terminals PASS | Normal confirmed reads PASS | Independent electrical/FC06 source qualification |
| Scale/polarity/origin/prepared-operation effects | Application `invalidateDriverAssumptions`, shared `driverConfigEffects` | First CONFIG disagreement with DRIVER invalidates old assumptions; stale request and history regressions PASS | Stored/raw settings unchanged | Physical setting effects remain unqualified |
| Exact target and newer baseline | Axis-only CONFIG/DRIVER cache publication, cross-cache operation-ID guards | Secondary addresses and real interleaved CONFIG1/DRIVER2 completion PASS; historical results preserved | Configured-axis reads PASS | Multi-axis application policy remains outside this single-axis example |
| Input qualifications belong to observed assignments | CONFIG publication revokes qualification on new baseline or changed function/polarity | Both independent changes reject subsequent settings admission/no TX PASS | Actual inputs remain functions1/2/3/0, no reassignment | Commissioned input policy and device homing/reference |
| Absolute deadlines, uncertainty, bounded owner and CLI parity | Existing owner/event mapper/reservations, original deadline and stationary cap | Delayed closure/expiry, pressure, recovery, stop, backpressure and direct/CLI tests PASS | 38 read frames, six zero-TX gates, empty owner PASS | Physical soft-limit movement NOT RUN |

## Confirmed defects and fixes

Five independent reproductions were checked against the actual code and run by
the lead, then covered by registered regressions:

1. A confirmed FC06 ACK followed by an unconfirmed matching FC03 readback
   returned SUCCESS, cleared uncertainty and could advance another grouped write.
   Every successful FRAME now requires `responseConfirmed`; unconfirmed prior
   read provenance also rejects preparation. ACK and uncertainty are retained.
   Parser/timing/deadline error precedence is unchanged. Python uses the same rule.
2. The first CONFIG refresh compared only its own absent old cache, ignoring a
   checked DRIVER baseline. Direction0/subdivision1000 could become1/1600 while
   generation1, scale1000, known polarity and origin survived. Both publication
   paths now compare their shared fields through one small helper.
3. Changed input function or polarity invalidated coordinates but retained
   `driverInputsQualified`. CONFIG now revokes it on changed input settings or
   first baseline establishment; an old policy cannot qualify another update.
4. A secondary-address read displaced the configured-axis cache, hiding later
   external mode/trigger/limit changes. Both prerequisite caches now belong to
   the configured axis; secondary reads keep their own inspectable results.
5. Older CONFIG1 could finish after DRIVER2 and replace the newer baseline,
   invalidating its cache under a newer generation. Publication checks both
   cache operation IDs. The actual owner/wire interleaving regression preserves
   DRIVER2 and still decodes the historical CONFIG1 result.

Configuration-effects/manual, limits/reference and console/correlation reviewers
worked independently from the previous summary. Each reviewed actual code and
failure cases; final cross-reviews covered code they did not author. The lead
verified the findings, integrated fixes and ran all registered tests. Final
review found no further confirmed defect. The correction adds no queue, task,
registry, retry, rollback, generic framework or duplicated conversion.

Original function PDF physical pages28–30,69–70,73,75 were visually rechecked:
400..51200 subdivision, external PV interruption/mode choices, soft-enable0x18,
homing prerequisite and seven-F native limit range remain as transcribed.
The conflicting soft-enable prose, unresolved sign/scale/pair forms and active
setting semantics remain explicit. No vendor bytes, generated enum, register
ledger or FC10 policy changed. Read-only current FieldCore inspection at
`e09d7936dcf334e371ffb978b6ee42b1954657df` confirms the bounded request/work/result
idiom, eight-byte TX and prefix echo handling. Its millisecond observations and
sensor result/retry path cannot substitute for these closure/source bounds;
no FieldCore file or framework type was imported.

## Final verification and storage

GNU15.1 C++11 Release/native assertions and `-Werror`: **30/30 CTests PASS**,
including the final interleaving regression. Python host **160 PASS**, generator
**18 PASS**, operation-inventory negative cases **15 PASS**; version/register
generation and all five reference snapshots PASS. Actual core/console/Python
parity accepts **14 terminal fixtures**, including unconfirmed read and readback,
and rejects forged provenance/success. Installed-only consumer, without example
include paths, **1/1 PASS**. All four PlatformIO environments PASS on the final
sources: units, probe, polling load and timer load. `git diff --check` PASS.

Xtensa14.2 C++11 size/stack inspection was repeated. DriverContext2136,
observation440, prerequisites544, evidence96 and prepared48 bytes are unchanged;
no public storage grows. Per-function `-Os` static stack frames remain settings
prepare576, read prepare112, next96, advance224, get496 and decode112 bytes;
these are not a summed call-chain bound. Application contexts/scratch stay in
PSRAM and required UART/capture state/stacks stay internal. Console bounds stay
128 input bytes,20 tokens,4608 output bytes,eight ordinary retained operations;
stop/recovery reservations remain separate. No extra cache or ownership flag
was introduced. Stale README/future-status statements were corrected.

## Corrected-image COM13 regression

USB303A:1001, serial3C:0F:02:CD:6B:98 was freshly enumerated. Passive preflight
on the previous image passed without recovery or statistics reset. Uploaded
timer firmware **444800 bytes**, SHA-256
`8c31d438777b8b42d054465f3c0f2fc243d0b28acd476c931b77663a5ada557a`;
upload hash verified. Original flash backup and older evidence remain intact.
Board UART2 TX47/RX48/DE21; motor node1,1152008N1,response200ms,reply gap304us,
timer20us/sample guard85us. Workload/owner-delay/console injection remain0/0/0.

The corrected image passed one driver read, identity/configuration, two repeated
state/health checks, ten one-attempt probes, then another driver/state read and
six setter gates. Total **38 FC03 frames/364 RX bytes**, failures/timeouts/
cancellations/capture/UART errors/discarded/echo bytes all zero. Gates reject
positive-limit10, direction2, soft-limit1, direction1, direction1+subdivision399,
word-order1; no admitted operation or physical write results. Inspection/release
passed and repeated passive health reads did not rejuvenate observations.

Raw driver fields `[0,1000,0,0,0,0,0]`, both limit pairs `[0,0]`, model/version
`4EEA/0029`, configured encoder4000, algorithm3, input assignments1/2/3/0 and
stationary raw alarm/motion0/1, I/O0/0, position/speed0 are unchanged. Device
homing-complete is false. No drive setting, motion, save/restore, transport
recovery or local statistics reset was sent.

First driver read spans **28.417ms** including task delivery; individual qualified
closure bounds remain retained. Probe durations5448..5644us, mean5494.7us.
Ending load snapshot owner/capture maximum gaps158/54us, high-water2, no gap
fault; capture15426072/76131051us≈20.26% of one core. Optional trace overwrites616
are bounded diagnostic overwrites. The driver snapshot records121 input lines,
dropped0, output blocks/short writes0; all **126 current-image command lines**
are logged with correlated completions (five follow that driver snapshot).
Load-generated console lines/drops0/0. Internal free/min/largest
336656/331496/278516, PSRAM8269356/8269356/8257524; owner/worker stack headroom
3492/3268 bytes. DE released, pending/retained/reserved0, recovery requiredfalse,
load and monitoring off.

Physical settings changes/readback/restoration **NOT RUN**: independent TX/RX/DE
and FC06 response-source evidence plus commissioned input policy remain absent.
No setting changed, so no restore was needed or claimed. Software-limit motion
**NOT RUN**: pair setters, signed/native mapping, device-homed reference and
fixture are unresolved. Simulation does not qualify these cases. Prompt14 can
reuse the actual installed observations/effects; it must establish its own
homing/reference semantics, with optional input configuration owned by15.
