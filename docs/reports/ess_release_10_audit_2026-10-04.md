# Prompt10 fresh independent audit — 2026-10-04

Baseline: `20dc19e` (`Add absolute and wrapped positioning with explicit zero
clear`), clean `main`, synchronized with upstream before this audit. The final
audit revision is the commit containing this report; its hash and verified push
are reported in the final handoff. No later numbered prompt was executed.

Prompt10, its full execution contract and sequence index were reread. Three
reviewers who did not author the original implementation inspected actual source,
callers, tests and `451a61d..20dc19e`, rather than accepting the previous report.
Root verified the confirmed findings and final diff. Original evidence is retained;
this run has separate [metadata](ess_release_10_audit_2026-10-04.json) and
[evidence archive](ess_release_10_audit_2026-10-04_evidence.zip).

## Confirmed findings and corrections

1. **Host edits detached origins from their expiring reference.** The actual
   console adapter unconditionally cleared `coordinateReference.nativeKnown`
   after `configureAxis`, even for an independent velocity preference. Core
   configuration correctly retained the unchanged origin/limits, but the
   application then stopped checking their witness's expiry. Fresh wrapped
   preparation also became unavailable unnecessarily. An actual application
   regression failed before the correction.

   `configureAxis(current, candidate, evidence, retainedReference=nullptr)` now
   optionally publishes the retained witness under the new generation when
   interpretation is unchanged. The existing private `sameInterpretation`
   comparison governs both origins and reference output. It preserves original
   observation time, age limit, native position and source; changes to scales,
   polarity or their provenance clear confidence. Input/output aliasing is safe;
   failures preserve configuration and output. Existing three-argument callers
   still compile. The application delegates this decision directly, with no
   second interpretation/conversion path.

2. **Reservations hid external movement.** Both external-running and changed
   raw-position checks used `!axisReserved`, which suppressed invalidation during
   stop, enable, pre-trigger staging and retained uncertain conflicts. A real-owner
   fake-responder test reproduced changed position during a pending stop while
   concurrent state observations shared the bus; a running-only variant also
   covers the motion block.

   A bounded `triggeredMove` scan derives the exception from existing active move
   records and accepted trigger TX. Only movement attributable to that active
   move defers invalidation until its terminal settlement. Other reservations
   no longer hide new evidence. No extra queue or ownership flag was added.

3. **A stale public comment described the wrong observation window.** `Actions.h`
   now documents position clear's `0x000A/2`, separately from other actions'
   `0x0006/2`, and includes position clear in its file description.

Additional integrated coverage checks clear ACK followed by a corrupt FC03
position reply: execution remains ACKNOWLEDGED, completion NOT_OBSERVED, coordinate
confidence invalid, and conflict survives explicit result release and recovery.
Exactly two transactions occur; nothing replays. Existing lost-ACK tests retain
UNKNOWN execution. Native simulation is not physical clear qualification.

## Requirements and failure coverage

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Explicit absolute frame, fresh reference, 720° retained | Shared `preparePosition`, `prepareMoveAbsolute`, copied `MoveContext.reference` | Existing axis/position/move-app and installed consumer PASS | Read-only and zero-TX gates PASS | Qualified live command-coordinate relation |
| Wrapped paths, half-turn ties, same angle, no alternate revolution at limits | `preparePosition`, `AnglePath`, `HalfTurnTie` | Existing boundaries plus independent 100,000-case rational oracle PASS | Wrapped preview rejects absent reference without TX PASS | Physical angular path comparison |
| Unsupported/stale relative bases, generations and requested/rounded limits | Existing shared preparation and immutable write budgets | Axis/position/move-app PASS; no-TX rejection preserved | Production motion admission remains gated PASS | Negative encoding and actual-relative basis qualification |
| Host origin has no motor traffic | `setAxisOrigin`, `axisCommand` | Existing actual-app origin/age tests PASS | Origin establishment rejects unsigned/raw-zero evidence PASS | Fresh qualified native reference |
| Explicit zero-only clear, lost ACK and failed observation | `prepareSetDevicePosition`, checked auxiliary ACK and new position pair read | Existing actions plus added corrupt-readback/release/recover test PASS | Clear timing gate sends no TX PASS | Physical no-motion clear and counter effect |
| Independent preferences preserve confidence and original expiry | Optional `configureAxis` reference output and actual console adapter | New core alias/failure/idempotence and app preference/limits/source-change tests PASS | Three host preference edits and pure previews PASS; no drive write | Board cannot establish qualified reference yet |
| Release/external movement/lost pose/settings invalidate knowledge | `invalidateAxisReference`, `serviceCoordinates`, state/action harvest | New stop-plus-state position/running tests; existing own-trigger, release, expiry, settings tests PASS | State/configuration payloads unchanged PASS | Independent shaft/reference measurements |
| All units, common/profile and direct API parity | Existing console parser, common/native mover and exact factor planner | Steps/fullsteps/turn/deg/rad/configured-mm paths and installed headers PASS | ASSUMED1000-scale step/degree/radian previews agree PASS | Physical equivalence; linear mechanism unavailable |
| Stop/reservation/deadline/uncertainty preserved | Existing owner, action and move loops | Full suite incl scheduling, cancellation, pressure and stale completion PASS | No moving stop attempted | Independent usable stop and timing qualification |
| Bounded storage, supplied events, portable installed core | No new public type, task, queue or dynamic allocation | Installed-package consumer, native and four firmware builds PASS | RAM/PSRAM/stack measurements PASS | Moving-path qualification and later soak |

## Independent review and sources

Coordinate/path reviewer audited exact modular math, rational periods, polarity,
ties, rounding, radian bounds and invalidation. A separate Python `Fraction`
oracle checked 100,000 deterministic fractional-scale/gear/origin/path/tie/rounding
cases against the compiled public API. No arithmetic defect was found. Existing
native cases additionally cover signed extremes, multi-turn boundaries, stale
generations, radians and reference/limit failures. The oracle source/log are in
the archive; it introduces no production dependency or speculative test engine.

CLI/direct-API reviewer reproduced and corrected both application defects.
Sequence/manual reviewer independently re-audited the final integrated behavior,
clear failure settlement, own-trigger versus external-movement suppression and
stop/cancellation preservation. Coordinate reviewer independently checked the
reference correction's aliases, atomic outputs and unchanged ages. Final reviews
found no further confirmed defects. Root read the actual diffs and reran checks.

The original function PDF and relevant rendered ESS pages were checked, including
the auxiliary clear instruction and staging/start semantics. SHA-256 remains
`0ca2d7f6b69404ead3ba9af982076c54f86eb16b1c24ba237f344aac079b81eb`.
Generated register/types/access files and vendor bytes were not edited.

Current FieldCore read-only HEAD: `7c356aff59132ab7ab7be6e3ec26a599ede6b99e`.
Actual `Rs485OwnerTransaction.h` and `Rs485Task.cpp` retain bounded request/result,
RequestTransaction/WaitUntil and deadline-before-configure/clear/TX vocabulary.
Deliberate differences remain ESS19-byte staging versus FieldCore8-byte TX,
full checked FC06 echoes versus prefix stripping, same-axis reservation,
qualified closure and retained uncertainty without sensor retry policy. No
FieldCore/framework/product types or edits enter this work.

## Verification and current-image bench

- GCC15.1, C++11 with `-Werror`: all23 CTest suites PASS.
- Python:142 harness cases,8 generator negative cases and13 inventory failure
  cases PASS; version/catalogue/inventory/reference checks PASS.
- Installed core consumer PASS, including optional retained-reference output
  aliasing, with no example include path.
- All four PlatformIO environments PASS: `bench_s3_units`, `bench_s3_probe`,
  `bench_s3_load_poll`, `bench_s3_load_timer`. `git diff --check` PASS.

COM13 USB303A:1001 serial3C:0F:02:CD:6B:98, MotorControl-RS0.6.0/protocol2,
UART2 TX47/RX48/DE21, node1 at1152008N1,20-us timer capture. Preflight inspected
local controls without clearing prior faults/statistics or recovering. Uploaded
corrected timer image418704 bytes; SHA-256
`27dd24d25109a86d38d651e01384b7315cd1d315671f3ce40411ad4de06f88a1`.
Upload verified flash hashes. Original16MiB backup remains
`85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.

Focused campaign28 FC03 frames/288 RX bytes, then the required10 one-attempt
probe regression: total38 frames/358 RX bytes. Zero failures, timeouts, cancels,
capture faults, RX errors, echo or discarded bytes. Four move gates, six action
gates and the finite Python harness reject unqualified actions without TX or
cleanup write. Host scale/basis/preferences are explicit local declarations;
they qualify no wire semantics. Pure1-step,0.36-degree and
0.006283185307179586-radian preparations agree on1 native increment.
Repeated cached reports preserve observation timestamps while ages increase.

Before/after drive reply payloads match: model4EEA, firmware0029, subdivision1000,
encoder setting4000, unknown algorithm3, inputs1/2/3/0; alarm0/motion1, IO0 and
position/speed0. These are checked drive reports, not physical standstill or
exact shaft-coordinate proof. Raw traffic and complete provenance are retained.

Campaign model-probe duration5434–5586us; closure intervals23–44us;
application delivery231–273us after closure. Ending owner/capture maximum
gaps158/56us,85-us capture bound not exceeded, capture high-water2. Internal
free/minimum336104/330944 bytes, largest278516; PSRAM8304172, largest8257524.
Owner/worker stack headroom3572/3268 bytes; owner-loop static frame784 bytes.
App81184, record2016 and console10776 bytes remain in PSRAM; required UART1704
bytes remain internal. Core/context sizes are unchanged, recorded in metadata.
No storage capacity, task or stack increase was needed by these fixes.

Ending load0/0/0, monitoring off, DE released, no pending/retained/reserved work,
axis conflict or recovery requirement; input/output drops0. Read-only cleanup
PASS. Physical move/stop/clear cleanup was not exercised.

## Handoff and genuine limits

Prompt10 software satisfies the audited paths and negative cases above. Physical
absolute/wrapped/equivalent-unit moves, device clear and independent shaft
comparison remain **NOT RUN**. Independent electrical/FC06 response-source
qualification, firmware sign/command-feedback/ramp/input semantics and usable
physical stop evidence are still absent. Raw zero and nominal encoder resolution
do not resolve those facts. Linear travel remains unqualified without an actual
configured mechanism. Production gates remain closed; no defaults or settings
were changed to hide missing evidence.

Prompt11 must reuse existing shared preparation, move/action contexts, original
deadlines and stop/reservation behavior. Use the optional retained-reference
output for host edits; never rejuvenate observations or let a general reservation
conceal external movement. Velocity/ramp qualification belongs to its separately
dispatched prompt. No new operation or implicit motor traffic was introduced.
