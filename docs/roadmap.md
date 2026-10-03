# Roadmap to a supported ESS release

Updated 2026-10-03. This is the delivery order and the definition of done.
The [backlog](backlog.md) holds individual features, register questions and
verification tasks. Update both when evidence changes the plan; dates and
future version numbers are not commitments.

## Release scope

The first supported release targets the identified ESS-RS bench model and
firmware, with a framework-independent core, a standalone E2 test application,
and a documented integration contract for FieldCore. It must make useful motor
control available through the same API that the CLI calls. Raw register access
alone does not complete the motor API.

Other serial manufacturers wait for an actual motor and a concrete need.
CANopen remains a separate future library. FieldCore stays read-only during
this session; integrating there is a separate delivery step.

## Milestones and evidence

| Stage | Deliverable | Gate before calling it complete | Current state |
| --- | --- | --- | --- |
| 1. Protocol foundation | Units, complete ESS register ledger, checked FC03/06/10 codecs and non-changing model probe | Native validation, generated-ledger checks, explicit unresolved fields and access limits | Implemented; checked model reads on the bench |
| 2. Observable transport | Runner, independent capture, read-only CLI, load and failure tools | Delayed servicing/overflow/late-reply tests; measured load, memory and timing; no automatic replay | Implemented in 0.6.0; measured E2 load envelope; electrical timing qualification remains open |
| 3. Small bus-owner reference | Bounded request queue, retained results, fairness, deadlines and priority for a pending stop after settling in-flight TX | Multiple simulated clients, full queue, cancellation and starvation tests; read-only hardware regression under load | Next implementation block |
| 4. Typed ESS observations | Identity, firmware/configuration, alarms, readiness and position/velocity observations | Original-manual review; exact model readback; validity/freshness independent of communication health | Not implemented beyond model probe |
| 5. First controlled motion | Explicit enable/release, small relative move and documented stop | Verified units and limits, acknowledgement vs completion, interrupted/lost replies, stop while another operation is active; bench measurements | Not implemented |
| 6. Complete common motion API | Absolute/relative position, step/angle/travel modes, velocity, acceleration, homing and fault clear where supported | Same public API in firmware and CLI; unsupported operations fail before TX; origin/rounding/path policy tested | Contracts and conversions exist; sequences pending |
| 7. ESS native coverage and discovery | Typed documented ESS extensions; bounded non-changing discovery | Coverage matrix for every documented command/field; uncertain firmware behavior marked explicitly; read side effects reviewed | Ledger/codecs exist; typed helpers and orchestration pending |
| 8. Platform and release qualification | Arduino E2 and native ESP-IDF examples, package/install checks, maintenance documentation | Clean consumer builds; repeatable hardware suite, extended soak, resource budgets, documented exact supported models and limitations | CMake/IDF core consumption exists; native IDF application and release evidence pending |

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

- Identify the actual drive model/firmware and confirm encoder, command units,
  default configuration and the meaning of raw model `0x4EEA`.
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

The next concrete deliverable is stage 3's small bus-owner reference. Before
that block begins, use the [capture/load report](reports/2026-10-03_capture_load.md)
to carry forward its measured limits and unresolved qualification work.
