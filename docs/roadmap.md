# Roadmap to a supported ESS release

Updated 2026-10-04. This is the delivery order and the definition of done.
The [backlog](backlog.md) holds individual features, register questions and
verification tasks. Update both when evidence changes the plan; dates and
future version numbers are not commitments.

The [ESS release prompt set](prompts/ess_release/README.md) turns these stages
into 30 ordered implementation blocks with tests, independent audits and
evidence handoffs. Prompts 01-15 have implementation and available verification
dispositions. [Prompt12](reports/ess_release_12_2026-10-04.md) consolidates the
four reviewed FC10 windows into generated policy and records all paired-write
proof gaps. Its [fresh audit](reports/ess_release_12_audit_2026-10-04.md) fixes
generator address consistency and repeats native/build/read-only checks.
No new physical write qualification is implied. [Prompt13](reports/ess_release_13_2026-10-04.md)
implements typed single-word settings and raw limit reads; its
[fresh audit](reports/ess_release_13_audit_2026-10-04.md) corrects response-source
settlement and axis prerequisite caches. Prompt14 adds bounded methods33/34/35. [Prompt15](reports/ess_release_15_2026-10-04.md)
adds typed optional I/O and selected stored-setting disable/readback/restoration
evidence; external physical function qualification remains open. Prompt16 waits
for its separate dispatch.
[Prompt10](reports/ess_release_10_2026-10-04.md) adds absolute/wrapped
coordinates and device zero-clear while physical comparisons remain gated.
Its [fresh audit](reports/ess_release_10_audit_2026-10-04.md) fixes host-reference
retention and external-motion invalidation, with independent reviews and another
38 read-only frames on the corrected image. Physical motion/clear remains NOT RUN.
The [relative-position handoff](reports/ess_release_09_2026-10-04.md) supplies finite staging/trigger/completion APIs while physical moves remain gated. The [action/stop handoff](reports/ess_release_08_2026-10-04.md)
has native reservations, uncertainty and priority stop, plus read-only COM13 and
zero-TX gate evidence. Physical actions and motion require independent timing/echo
qualification; no live move or dynamic-stop claim is made.
The [fresh prompt 08 audit](reports/ess_release_08_audit_2026-10-04.md) preserves
uncertainty for undocumented exceptions, checks action evidence chronology and
adds stop HOLD/partial-TX/late-fault regressions. Native/package/build and 38-frame
read-only verification pass; the physical qualification gates remain open.
The [fresh prompt 02 audit](reports/ess_release_02_audit_2026-10-03.md) corrects
cancellation cutoff precedence and recovery evidence continuity; hardware owner
integration now has [prompt 03 evidence](reports/ess_release_03_2026-10-03.md).
The [fresh prompt 03 audit](reports/ess_release_03_audit_2026-10-04.md) corrects
cache/result/deadline edges and repeats native/build and read-only timer campaigns.
The [fresh prompt 01 audit](reports/ess_release_01_audit_2026-10-03.md) corrects
two timeout-reporting defects with native and current-image regression evidence.
The [fresh prompt 05 audit](reports/ess_release_05_audit_2026-10-04.md) verifies
the typed read implementation, corrects stale API descriptions and repeats
native/package/build and unloaded COM13 checks on the unchanged image.
Preparing a prompt does not complete its milestone.
The historical [source re-audit](reports/2026-10-03_promptset_reaudit.md) adds
explicit gates for absolute deadlines, queued work across recovery, same-axis
staging ownership, honest observation age and long-frame evidence. Historical
board names remain in evidence reports; current adapters/prompts use platform
names and no unrelated product/bus requirements.

## Release scope

MotorControl-RS is a framework/platform-independent library for RS485 motors
from selected manufacturers. The first supported profile targets the identified
ESS-RS model/firmware. A standalone application on the available ESP32-S3 bench
qualifies that profile through the same public API that firmware consumers and
the CLI call. Raw register access alone does not complete the motor API.

The standalone RS485 workflow is a working reference for future integrations:
profiles yield transaction/wait work, one application bus owner schedules it,
and checked observations/results return to the caller. FieldCore can later use
that reference to extend its RS485 task and add a new motor device. No FieldCore
motor device or product wiring is assumed today; unrelated buses/products are
outside this library. Board pins, USB and memory placement stay in examples.

External motor I/O is optional. Support serial-only operation with known unwired
inputs and explicit documented no-function assignments. An unwired terminal is
not automatically disabled; only operations that actually need an external input
require it. ESS I/O disable/configuration implementation belongs to prompt 15.

Other serial profile implementations wait for an actual motor and a concrete
need; the common API is reviewed against contrasting documented families.
CANopen remains a separate future library. FieldCore stays read-only during
this session; integrating there is a separate delivery step.

## Milestones and evidence

| Stage | Deliverable | Gate before calling it complete | Current state |
| --- | --- | --- | --- |
| 1. Protocol foundation | Units, complete ESS register ledger, checked FC03/06/10 codecs and non-changing model probe | Native validation, generated-ledger checks, explicit unresolved fields and access limits | Implemented; checked model reads on the bench |
| 2. Observable transport | Runner, independent capture, read-only CLI, load and failure tools | Delayed servicing/overflow/late-reply tests; measured load, memory and timing; no automatic replay | Implemented in 0.6.0; measured ESP32-S3 bench load envelope; electrical timing qualification remains open |
| 3. Small bus-owner reference | Bounded request queue, retained results, fairness, deadlines and priority for a pending stop after settling in-flight TX | Multiple simulated clients, full queue, cancellation and starvation tests; read-only hardware regression under load | Prompts 01–03 implemented/native/build PASS; actual owner console and read-only loaded/interleaved timer bench PASS; no physical stop or electrical qualification |
| 4. Typed ESS observations | Identity, firmware/configuration, alarms, readiness and position/velocity observations | Original-manual review; exact model readback; validity/freshness independent of communication health | Prompts 05–06 implement typed reads and separate health; bench SKU is user-confirmed ESS23-RS20; wire-code/firmware interpretation, feedback source/sign/units and motion qualification remain unresolved |
| 5. First controlled motion | Explicit enable/release, small relative move and documented stop | Verified units and limits, acknowledgement vs completion, interrupted/lost replies, stop while another operation is active; bench measurements | Exact preparation in 07 and bounded action/stop software in 08 pass; physical actions remain gated, first move belongs to 09 |
| 6. Complete common motion API | Absolute/relative position, step/angle/travel modes, velocity, acceleration, homing and fault clear where supported | Same public API in firmware and CLI; unsupported operations fail before TX; origin/rounding/path policy tested | 07–11 implement exact preparation, origins, finite relative/absolute/wrapped positioning, zero clear and finite serial velocity/shared stop; bounded homing33/34/35 implemented in14; acceleration mapping, external-switch methods and physical qualification remain open |
| 7. ESS native coverage and discovery | Typed documented ESS extensions; bounded non-changing discovery | Coverage matrix for every documented command/field; uncertain firmware behavior marked explicitly; read side effects reviewed | Ledger/codecs and typed identity/config reads exist; complete operation inventory derives remaining obligations; broader helpers/discovery pending |
| 8. Platform and release qualification | Arduino and native ESP-IDF examples, package/install checks, maintenance documentation | Clean consumer builds; repeatable hardware suite, extended soak, resource budgets, documented exact supported models and limitations | CMake/IDF core consumption exists; native IDF application and release evidence pending |

Stages 3 and 4 can progress while independent electrical captures are being
arranged. Motion qualification must use reviewed transport evidence and the
actual drive configuration. No stage silently promotes a software test into
a hardware claim.

## What every implementation block includes

1. Implement one usable behavior with simple bounded ownership. Reuse the
   existing codecs, runner, capture and console rather than copying FieldCore.
2. Add tests for meaningful error and interruption paths, including uncertain
   execution for writes. Keep command acknowledgement separate from motion.
3. Run a short COM13 hardware regression whenever the feature reaches the
   board. Add a direct feature test where applicable. Record unavailable or
   unperformed tests; do not silently substitute native tests for them.
4. Retain latency, failures, service gaps, internal RAM, PSRAM and stack
   observations when resource use or scheduling changes. Longer stress runs
   are separate qualification work, not required after every documentation edit.
5. Audit the changed code and affected contracts, fix stale documentation,
   update this roadmap/backlog, then commit and push the tested block.

## Remaining release gates

- Bench model ESS23-RS20 and nominal encoder specifications are established
  ([sources](reference/11_ess23_rs20_identity.md)); resolve firmware/algorithm3,
  command/feedback semantics and a universal mapping of raw model `0x4EEA`.
  Physical direction/accuracy remain qualification work.
- Capture independent TX/RX/DE waveforms. Validate timestamp bounds, final stop
  bit/DE hold, receiver turn-on, short turnaround and framing under load.
- Establish an interrupt/cache-off policy. The current stock timer build is
  not qualified through flash writes. Measure whether its CPU cost is suitable
  for the eventual application before treating it as a production selection.
- Test stop/interruption and uncertain write execution without automatic replay.
  Cover drive alarms, loss of communication and stale feedback separately.
- Account for every documented ESS native operation as implemented and tested,
  firmware-dependent, unavailable on the selected model, or explicitly excluded
  with a reason. An undocumented register gap is not a missing implementation.
- Run an extended soak and disconnect/power-cycle campaign after functional
  behavior stabilizes. Quick regressions do not replace this work.
- Check public API stability, examples, package metadata, source provenance and
  release notes. State exactly what was qualified; do not claim universal motor
  support or industrial certification.

Prompt 04 is disposed in [the capture review](reports/ess_release_04_2026-10-04.md):
retain 20-us capture at measured 20–21% core cost, enforce starvation failure,
and extend checked read-only evidence to the fixed 37-byte reply. Independent
electrical timing remains NOT RUN; cache-off capture is unsupported. Independent
05–07 read-only/pure work is now implemented; dependent physical actions retain
their timing, settings, units and stop prerequisites.
Its [fresh audit](reports/ess_release_04_audit_2026-10-04.md) closes a host
diagnostic-validation gap; the unchanged image passes repeated read-only tests.

Prompt06 delivers [typed state, separate health and finite polling](reports/ess_release_06_2026-10-04.md). Stationary state reads pass; algorithm3, position source, signed encoding/physical units, motion completion and electrical timing remain unresolved.

The [fresh06 audit](reports/ess_release_06_audit_2026-10-04.md) verifies current code and fixes feedback configuration confidence and strict cached-state validation. No moving operation or settings write was added.

[Prompt07](reports/ess_release_07_2026-10-04.md) delivers exact target preparation,
reference/limit checks and host-only configuration through the installed API
and console. Software and read-only COM13 checks pass; native origin evidence
is unresolved on this drive, and no motion, drive setting or travel qualification
is implied. Prompt08 is the next separately dispatched action/stop block.

The [fresh07 audit](reports/ess_release_07_audit_2026-10-04.md) corrects numerical
and exact-parser boundary defects and repeats software/package/build and read-only
host API checks. Requested relative displacement and endpoint limits remain
independent; zero radians retain exact integer references without approximation.

Prompt09 [relative-position contract](ess_position.md) and [handoff](reports/ess_release_09_2026-10-04.md) implement the staging/trigger/observation chain with retained uncertainty and same-axis stop interruption. Hardware movement remains gated; independent electrical/physical evidence is not replaced by native tests.

The [independent prompt09 audit](reports/ess_release_09_audit_2026-10-04.md)
also enforces readiness during queued/deferred writes and current-time reference
validation; Python interruption and correlation checks preserve unknown cleanup.
Physical positive/negative moves and dynamic stop remain unqualified.

[Prompt10](reports/ess_release_10_2026-10-04.md) implements preserved multi-turn
absolute targets, explicit wrapped paths, shared finite staging/trigger and
zero-only device clear, with reference invalidation and native/CLI parity.
Software/package/firmware checks and current read-only COM13 regression pass.
Equivalent-unit physical motion and independent shaft/origin/clear proof remain
NOT RUN; linear travel requires a real configured mechanism. Prompt 11 retained
the unresolved ramp/sign/model prerequisites described below.

Prompt 11 implements [finite signed velocity](ess_velocity.md) using shared
exact preparation, native ramp snapshots and the existing priority stop.
Its [fresh audit](reports/ess_release_11_audit_2026-10-04.md) fixes strict host
evidence and safe idle-interruption cleanup, and checks actual C++ console
outcomes against Python. Native/package/firmware and read-only COM13 checks
pass; exact firmware speed/ramp semantics and physical stopping remain NOT RUN.
Prompt 12 [reviews every writable pair](ess_pair_writes.md) and generates the
four-window FC10 policy from the same ledger. No new span or typed setter was
justified; home-order, limits/stored-record forms and physical writes remain
open. Native/package/build and read-only regression pass.

Prompt13 implements [typed driver settings](ess_driver_settings.md), checked
ACK/readback/partial progress and shared settings-effect invalidation. Native,
installed package, firmware and read-only COM13 checks pass; actual settings
changes/restoration and software-limit movement remain NOT RUN. Pair setters
stay guarded. Prompt14 now implements the bounded33/34/35 subset; prompt15 remains separately dispatched.

Prompt14 [homing delivery](reports/ess_release_14_2026-10-04.md) reuses checked staging/trigger and priority stop with fresh transition/zero proof, explicit35-method coverage and conservative reference age. Native/package/firmware and current-image read-only/gate checks are separate from physical homing qualification, which remains NOT RUN. External input methods await15 and actual fixtures; collision and nonzero offset semantics remain unresolved.

Prompt14 [fresh audit](reports/ess_release_14_audit_2026-10-04.md) corrects historical feedback invalidation and host outcome checks, with native/package/firmware and corrected-image read-only/gate PASS. Physical homing prerequisites remain open; prompt15 remains separately dispatched.
