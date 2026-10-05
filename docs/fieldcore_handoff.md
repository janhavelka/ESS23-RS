# FieldCore RS485 motor integration handoff

This is a proposal for a separately authorized integration. FieldCore was read
only; no motor module, product wiring or working integration is claimed. Current
working sources were inspected on 2026-10-05 against its `develop` reference
`d19758865852f4e215ea05c6572d060edc4f0c66`. Recheck those sources before integration.
MotorControl-RS stays independently consumable through its installed headers and
`MotorControlRS::MotorControlRS` CMake target.

The candidate's supported subset and remaining physical/native-feature gaps are
in the [qualification matrix](reports/ess_release_29_2026-10-05.md) and
[API/CLI coverage](ess_api_cli_coverage.md). Compile compatibility does not close
those gaps. Board/UART/pins and external-input policy belong to the later
application selection; the standalone COM13 bench is evidence, not a FieldCore
product specification.

## Existing boundaries and proposed mapping

| Current source boundary | Motor integration responsibility |
| --- | --- |
| [Device commands/results](../../FieldCore-node/include/TunnelMonitor/contracts/DeviceMeasurement.h), lines 25–113 | Current `DeviceCommand` supports only Probe/Measure/ReadLast; results contain float readings and one generic terminal status. Add a firmware-owned typed motor command/result path. Preserve exact native integers, `Status`, raw alarms, write execution, completion, uncertainty and provenance separately. Do not encode them as float measurements or alias firmware types into MotorControl-RS. |
| [Device binding](../../FieldCore-node/include/TunnelMonitor/rs485/Rs485DeviceBinding.h), `start/poll/takeStep/acceptObservation/takeResult` | A future motor module owns installed `ESS_RS::ReadContext`, `ActionContext`, `MoveContext` and required typed extension contexts. Call prepare/next/advance functions from that module; rejected preparation yields no traffic. Use public `prepareProbe` for minimal presence and separate identity/config/state operations for richer reads. |
| [Module steps](../../FieldCore-node/include/TunnelMonitor/rs485/Rs485OwnerTransaction.h), `Rs485ModuleStep` | Action/move TRANSACTION maps to copied owner work and WAIT to `WaitUntil`; context termination publishes one typed result. `PreparedRead` has no work-kind enum: a successful `nextRead` yields its frame, while `ReadContext` state identifies termination. Correlate each work item by operation ID/step/target/generation and admit it once even if `next*` is inspected repeatedly. Keep the axis reservation across stages, observations and waits. |
| [RS485 task](../../FieldCore-node/src/rs485/Rs485Task.cpp), `poll` line 653, `startReadyModuleTransaction` line 950 | Keep this owner and its round-robin binding selection. Waits release bus ownership; other devices can run. Extend its work/results for motors and urgent stop, rather than placing another owner or `RtuRunner` on its UART. |
| [Backend](../../FieldCore-node/include/TunnelMonitor/rs485/Rs485Backend.h) and [Arduino implementation](../../FieldCore-node/src/rs485/ArduinoRs485Backend.cpp) | Continue owning UART/DE and explicit board configuration here. Add the transport timing/error evidence needed by the installed event contract. Standalone adapter/owner classes remain example references, not installed dependencies. |
| [Runtime ingress](../../FieldCore-node/src/rs485/Rs485RuntimeIngress.h) and [diagnostics](../../FieldCore-node/src/rs485/Rs485Diagnostics.cpp), `drainWorkerDeviceResults` line 523 | Preserve bounded worker ingress and terminal conservation. Motor contexts and retained raw evidence need explicit lifetime/consumption rules in this firmware layer; do not add a library service, product registry or parallel bus task. |

FieldCore's `RequestId` is `uint32_t` in `contracts/Command.h`; the RS485 CLI
allocates its current IDs below `0x80000000` in `Rs485Cli.cpp:427`. Carry the
firmware request identity independently from the library operation ID and step.
Associate each admitted transaction with the exact operation ID, step,
`ReadTarget` (ID/address/binding generation), immutable parser expectations and
operation context. Retire/reject stale identities across cancellation, recovery,
rebinding and ID reuse. Library generations are not wire transaction IDs.

Current owner admission (`Rs485Task.cpp:430`) and runtime ingress reserve one
command per device until its exact terminal is consumed. This is a useful
foundation for same-axis exclusion, but ordinary exclusion must allow a separate
urgent stop to interrupt an active move. Retain the interrupted move result and
the stop result independently. `takeDeviceResult` currently consumes the owner
result; the runtime copies it into tracked ingress storage. Preserve that
conservation and make motor result lifetime explicit for all callers.

## Required owner/backend changes

1. **Capacity and request validation.** Current transaction TX capacity is eight
   bytes and RX capacity 192 (`Rs485OwnerTransaction.h:9`). ESS FC10 needs up to
   21 bytes (six-word window); finite position staging needs 19. Reviewed FC03
   replies need at most 37 bytes and exceptions five. Increase copied TX storage
   and audit every admission/copy/trace bound; retain bounded RX/echo storage.
   Validate each request and reply with its own copied expectations and the
   checked profile parser, including address/function/count/CRC/write echo.

2. **Echo and framing.** `processResponse` (`Rs485Task.cpp:1169`) strips any
   initial RX bytes identical to the request. That discards a genuine ESS FC06
   acknowledgement. Introduce an explicit per-request/adapter echo policy and
   retain ambiguity: identical bytes alone cannot set
   `ActionEvent::responseConfirmed`. A policy may allow the supported action to
   continue with read-only observations while execution remains UNKNOWN; do not
   replay the write or relabel an echo as acknowledged. `responseComplete`
   (`:1305`) currently completes at `>= expectedLength` or a suffix, with no RTU
   exception/gap closure mode. Add an ESS RTU collection path that preserves
   five-byte exceptions, trailing/partial data and qualified frame closure;
   completion is not parser success. Keep existing FixedLength sensor behavior
   and VibWire's `RS485_PREFIXED` ASCII/CRLF Suffix mode (`VibWireDeviceModule.cpp:301`).

3. **Physical timing and failures.** Backend `flush` (`ArduinoRs485Backend.cpp:168`)
   already calls `uart_wait_tx_done`; preserve that physical completion check.
   `read` returns byte batches without wire intervals, watermarks or UART error
   provenance. The owner uses milliseconds, the worker normally sleeps 5 ms
   (`Rs485Diagnostics.cpp:117/736`), and write/flush can each block for a bounded
   backend step (up to 600 ms). Those facts cannot prove RTU byte-gap timing or a
   motor stop service bound. Add conservative microsecond evidence and bounded
   service, continuous RTU transmission, overflow/parity/framing diagnostics, physical TX settlement and
   actual DE setup/hold checks. Map `Rs485BusObservation` to `ReadEvent` only
   after retaining correlation and real evidence: `qualified`, closure bounds,
   `txAccepted`, `executionUnknown`; `ActionEvent` separately needs `txComplete`
   and source confirmation. Converting a millisecond timestamp into microseconds
   does not create timing qualification. Apply each yielded transaction deadline
   as well as the immutable operation deadline; move readiness can expire sooner.

4. **Cancellation and urgent stop.** `processCancelWork` (`Rs485Task.cpp:749`)
   currently releases DE and clears active transport; runtime disable has a
   similar path in `applyPendingConfigs`. A motor cancellation must first settle
   accepted physical TX and retain its uncertainty/possible reply. Local cancel
   sends no motor stop. Prepare an explicit normal/direct stop with the installed
   API; cancel unsent continuations, reserve urgent request/result capacity, and
   dispatch at the next safe bus boundary without stale staging after the stop.
   Current queues/results/bindings are bounded at eight, with no reserved stop
   lane. Preserve sensor fairness and test stop under both queue and result
   pressure. Queue semantics and deceleration remain explicit profile policies.

5. **Recovery and no uncertain replay.** `processRecoveryWork`
   (`Rs485Task.cpp:798`) currently releases DE, clears active transport, fails
   queued requests, invalidates modules and retains one recovery result. Extend
   physical TX/drain settlement before reinitialization. Preserve interrupted
   motor outcomes and uncertainty, advance logical generations, invalidate old
   queued/sequence work and require new explicit admission. Existing sensor
   `retryOrFinish` paths belong to SHZK/VibWire; a motor module must not inherit
   their automatic retries. Host recovery and read-only reconciliation are not
   a stop, drive reset, motion replay or proof that a write never executed.

Normal per-transaction `configureBaud` (`Rs485Task.cpp:1012`, backend `:77`)
selects a host UART for the next device. It does not change drive registers or
invalidate every motor reference merely because another device uses another
baud. Current formats admit five baud rates and 8N1 only. Expand host tuple
support explicitly when needed; keep unsupported tuples a pre-TX error.
Logical drive communication changes use `ESS_RS::CommunicationContext`, exclusive
commissioning, retained before/requested candidates and read-only confirmations.
Save/restart, activation and route-back requirements remain separately qualified.

Cache identity/configuration/state by exact target and generation, with per-block
observation age and failed-refresh retention. `StateObservation` is not an atomic
snapshot or proof of signed/calibrated feedback. Keep codec errors, motor alarms,
execution/completion and application health independent. Maintain
`AxisConfig::generation` and `AxisReference` confidence; interpretation changes,
release/lost confidence and possibly applied relevant settings invalidate
dependent origin/limits/prepared targets immediately. Discovery and presence do
not establish readiness. `InputWiring::UNCONNECTED` does not disable assigned
drive inputs; do not silently rewrite them during setup.

## Regression and compiler evidence

Use current [owner fake tests](../../FieldCore-node/test/native/test_owner_rs485/test_rs485_task_fake.cpp)
and `FakeRs485Backend`, preserving the existing full/partial sensor echo tests,
FixedLength and CRLF suffix completion, different-baud devices, retry policy,
queued cancellation and recovery/result conservation. Add motor cases for FC10
21-byte requests, five-byte exceptions, FC06 ambiguity, truncated/overlong/CRC
errors, mixed RTU/ASCII traffic, per-request parser context, delayed/stale events,
same-axis staging/waits, stop under pressure, partial TX/flush failure, physical
settlement on cancel/recovery, retained uncertain results and stale queued work
after recovery. Record service/timing and memory bounds before any live motor
integration claim. Passive observation copies owner bytes without consuming RX
or blocking transactions when its output is full.

Current generated FieldCore profiles pin pioarduino `55.03.311` and select
`-std=gnu++17`; `scripts/apply_cxx_flags.py` appends `-fno-rtti`. The installed
motor core requires C++11; its strict C++11 and C++17/no-RTTI source/install
consumers and S3/S2 core builds are part of [verification](verification.md).
A separately staged Xtensa consumer can include both installed motor headers
and current FieldCore owner contracts with GNU C++17/no RTTI (and the stronger
no-exceptions option), without editing or building FieldCore. Such a check proves
source/language compatibility only: it does not implement the missing motor
commands, change the eight-byte contract or qualify that owner/backend.
Exact candidate compiler commands/results belong to the prompt30 evidence.

Size caller-owned contexts, saved settings, transaction/result slots and traces
for the selected firmware. Current FieldCore diagnostics allocate larger runtime
buffers in PSRAM and keep its 8192-byte worker stack separately; retain actual
platform-required internal memory for driver/ISR state. Measure high-water marks
and service cost rather than importing standalone bench memory totals.

Later integration and release publication require separate authorization. After
every integration push, including documentation-only pushes, check CI for the
exact commit and resolve every required failing job before reporting completion;
the root owns commits/pushes and exact-commit CI closure.
