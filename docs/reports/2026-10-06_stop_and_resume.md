# Stop/resume ownership audit, 6 October 2026

The reported sequence failed because the simple-move wrapper used the previous
move's historical uncertainty as an admission lock, even after a separate
checked stop cleared the actual motor interlock. The drive was stopped; this
was application bookkeeping. A second issue left the one reserved stop-result
slot occupied after a successful stop, rejecting subsequent stops.

## Rule and changes

Active work and unresolved physical motion reserve the motor. Retaining a
terminal result does not. New moves still require settled transport and fresh
readiness/position evidence; no uncertain write is replayed.

- A delivered simple session retires when the current interlocks permit new
  work. Its failed move child stays in the existing bounded result records,
  with the same ID, outcome, bytes and uncertainty. No second queue or history
  was introduced. The printed child ID works for inspection and explicit
  release both before and after the next simple command.
- A new admitted stop replaces the previous delivered successful acknowledged/
  observed stop in the reserved slot. Failed/unknown stops move to a free
  ordinary result slot; otherwise admission explicitly reports `RESULTS_FULL`.
  Active/undelivered stop results and failed replacement admissions are not
  erased. Eleven consecutive successful stops leave ordinary slots free.
- A normal or interrupted host motion changes current position, not the fixed
  counter-to-host origin. Current observation confidence is invalidated;
  the next move reads fresh feedback. Release, clear, external movement and
  interpretation changes retain their separate origin-invalidation behavior.
- A small per-record application flag distinguishes our unresolved triggered
  motion from external movement. A checked stop clears this current attribution
  without editing historical result contexts. An old retained uncertain trigger
  cannot explain later movement after a new staging-only failure.
- Human output separates acknowledged command/uncertain completion from truly
  unknown execution. The release hint describes result storage, not a required
  motor-unlocking step. Admission distinguishes result pressure, current axis
  conflict and transport recovery.

Independent ownership and simple-session reviewers checked actual code and
tests. Current FieldCore `Rs485Task.cpp` cancellation/result handling was
inspected read-only; its sensor completion semantics are not imported as motor
stop behavior. The fixes stay in the shared standalone application used by
Arduino and native IDF. Public motor wire/sequence behavior is unchanged.

## Audited dispositions

| Case | Disposition |
| --- | --- |
| Invalid/read-only preparation or unsent cancellation | No motor interlock; delivered session can retire. |
| Cancellation during setup, trigger TX or running | Physical TX settles; original outcome retained; unresolved movement/setup still requires reconciliation. |
| Stage exception, wrong echo, lost reply or deadline | No continuation/replay; transport recovery and/or checked stop remain explicit. |
| Successful stop after interrupted move | Motor interlock clears; next move uses fresh feedback and the existing zero. |
| Failed/unknown stop | Does not unlock motion; its evidence survives another stop attempt. |
| Repeated successful stops | Reuse reserved slot without filling ordinary result storage. |
| Terminal output blocked | Result/session remains until delivery; no silent overwrite. |
| Eight retained failed ordinary results | Explicit `RESULTS_FULL`; inspecting/releasing a terminal result frees capacity. |
| Local reset/recover | Neither proves standstill nor clears physical uncertainty. |
| Release/clear/configuration/external movement | Existing origin/generation invalidation remains effective. |
| Monitoring, commissioning, pending profile restoration | Existing explicit session/admission restrictions remain; no hidden override. |

This is not a claim to exhaust every possible hardware fault. Storage limits,
undelivered output, a failed stop and unrecovered transport are intentional
admission conditions, not stale ownership left by a completed stop.

## Verification and evidence

Native tests exercise the actual shared application under both Arduino and
IDF SDK fakes: all eight stop/cancel boundaries preserve fixed origin but
require new current-position evidence; unsent cancellation; stage exception;
stop followed by absolute move; failed stop; output backpressure; retained
uncertainty through reset/recover; result exhaustion/release; repeated stop
pressure and admission failure; stale historical-trigger attribution.

Full verification uses `scripts/verify.py --mode full`, the pinned IDF SDK and
toolchain environment documented by the preceding firmware-health report.
Final manifests/logs are under `build/ownership_fix/verified`; firmware builds
use `PLATFORMIO_BUILD_DIR=build/ownership_fix/pio`. The application changes add
no independent bus owner, conversion path, retry loop or unbounded storage.

COM13 tests use the ordinary Arduino timer image, address 1, 115200 8N1,
secured free shaft and no external inputs. The finite scenario first moves
180 degrees at 60 rpm/100 ms ramps, interrupts return-to-zero with fast stop,
then commands zero again without releasing the interrupted result. It checks
that historical outcome/uncertainty are unchanged, explicitly releases that
result, repeats fast/normal/fast stop, performs a 90-degree move/return, and
verifies standstill plus exact profile restoration. This is drive feedback,
not independent shaft or electrical measurement.

Failed harness attempts are retained: `stop-resume1` expected obsolete host
settings wording and sent no motion; `stop-resume2` incorrectly demanded
`ok=true` while inspecting the deliberately cancelled result. The latter's
checked stop, standstill and settings restoration succeeded. The helper now
validates cancelled results as evidence instead of treating them as success.
Neither attempt justified replaying an uncertain write.

The corrected `stop-resume3` passed; `stop-resume4` repeats it on the final
human-output revision, SHA-256
`a075517a001657e41c3419d3095f09d7e072274c6490cd0dcbc36e3d97fd6ad2`.
That run passed all 75 checked frames with zero errors, timeouts, capture faults,
RX errors or cache interruptions. Original/interrupted/final raw positions were
61811/62194/61810 (one-increment readback allowance, not a precision claim).
The interrupted result remained cancelled, acknowledged and uncertain before
and after the next move. Original profile `[30,100,100,600,0,50887]` was restored
exactly. Final observed speed/alarm were zero, enabled/nonrunning, DE released.

Internal free/minimum RAM was 336,208/331,048 bytes, PSRAM
8,177,196/8,177,196 bytes, owner stack watermark 2,052 bytes. A local verifier
attempt `final` failed because its PlatformIO build and the upload concurrently
opened the same output image. That failed attempt is retained; the final
verifier runs without an overlapping build/upload.

Final explicit controller reset/read-only zero check passed 20 frames without
errors. RAM-only zero is 61810; `moveto 0 deg` was an already-at-target no-op.
Its result was released, owner/output queues are empty, load/monitor/debug
disabled, watchdog healthy, motor observed enabled/nonrunning with zero
speed/alarm. COM13 is released. No movement or diagnostic fixture is queued.

Final full verifier: PASS, all 20 groups and all 77 registered tests. This
includes Arduino/IDF application tests, generated/reference checks, clean
source/install consumers, four Arduino configurations and IDF/S2 portability
builds. IDF runtime hardware was not reflashed for this application-only
change; both framework paths ran the native scenarios and compiled cleanly.

[Bounded evidence archive](2026-10-06_stop_and_resume_evidence.zip), SHA-256
`c6c09b3d126b7162315424b68c29fbd8dec235ec52c8128dbbfd1e94ac89a8ec`,
contains exact scripts/inputs, image hashes, failed/passing raw attempts,
final-state evidence and verifier logs. Matching images/ELFs remain under
`build/ownership_fix`.
