# Standalone runner: FieldCore and ESP32 review

Reviewed on 2026-10-03 against FieldCore-node commit
`063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f`. Its inspected RS485 files had no
local changes. This is source review and guidance for the later E2 adapter;
no firmware was flashed and COM13 was not opened. The native transaction
runner belongs under `examples/common/`. FieldCore continues to own its bus.

This is the version 0.4.0 review. The later [E2 implementation](../e2_probe.md)
uses direct polling without installing the IDF driver and supplies conservative
timing intervals to the extended runner. See its current contract and bench
report for implemented behavior and outstanding external qualification.

## Reuse that saves work

| FieldCore source | Useful behavior | MotorControl-RS application |
| --- | --- | --- |
| [ArduinoRs485Backend.cpp](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/src/rs485/ArduinoRs485Backend.cpp) | UART2, explicit active-high DE/RE, initialization checks and an IDF TX-completion call | Adapt these small board/driver operations when implementing the ESP32 backend. Keep them out of the library core. |
| [Rs485Task.cpp](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/src/rs485/Rs485Task.cpp) | One active transfer, a DE shadow, bounded storage, separate transport observations and parser results | Keep the same ownership. Preserve one terminal result and its evidence until the application consumes it. |
| [Rs485Diagnostics.cpp](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/src/rs485/Rs485Diagnostics.cpp) | Fixed diagnostic rings, drop counts, high-water marks, passive status copies and PSRAM allocation at initialization | Reuse these diagnostic conventions as the standalone CLI grows. Do not import the whole worker/runtime subsystem. |
| [Rs485Devices.h](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/include/TunnelMonitor/contracts/Rs485Devices.h) | Separate bus, device and parser state; traces include state, event, counts, timestamps and sequence numbers | Keep transport failure, codec failure, motor alarms and stale observations separate. |

The existing [E2 board reference](04_co2control_platform.md) remains the pin
authority: UART2, TX47, RX48 and active-high DE/RE21 for the user-confirmed
bench. Product selection and the physical board are separate. Do not import a
FieldCore product environment just to reuse its UART operations.

## What needs different treatment

FieldCore's current owner is built for its selected sensor modules. These
specific details prevent direct reuse as a motor transaction runner:

- Its [transaction contract](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/include/TunnelMonitor/rs485/Rs485OwnerTransaction.h)
  has an eight-byte TX array. Reviewed ESS FC10 requests need up to 21 bytes.
- `processResponse()` removes any initial RX bytes identical to the request.
  A genuine FC06 acknowledgement is identical to that request. Echo treatment
  must therefore be an explicit, qualified adapter policy. An optional-echo
  guess cannot reliably distinguish an acknowledgement from an echo.
- `responseComplete()` uses an expected length or a suffix. An ESS exception
  is five bytes, which can be shorter than the normal reply. Frame collection
  must preserve complete exception frames for the existing checked parser.
- `read()` returns available byte batches without wire timestamps or UART
  error evidence. The worker normally sleeps 5 ms between polls. Those facts
  cannot establish sub-millisecond RTU inter-byte gaps. Sampling a batch at a
  particular time does not prove when each byte arrived.
- The normal 2 ms pre-TX guard and 20 ms inter-request gap are FieldCore
  application settings. They are not ESS response deadlines or transceiver
  setup specifications. Use the [timing audit](09_timing_and_gap_audit.md).
- The selected command kinds are probe, measurement and cached read. Motion,
  uncertain execution and stop priority need their own FieldCore contracts.
  Sensor retry/recovery behavior is not a motor replay policy.

These observations do not require sibling edits in this implementation block.
Later integration should improve FieldCore's existing owner/backend and add a
motor device module. Starting a second owner on UART2 would violate ownership.

## Requirements for the later ESP32 callbacks

The following describes adapter obligations, not additional public core APIs.

| Operation | Required evidence |
| --- | --- |
| Start transmission | Report how many bytes were accepted and any driver failure. Accepting bytes is not physical TX completion. The adapter must send one continuous RTU frame; it must not insert scheduler delays between short writes. |
| Check TX completion | Report success only after the final stop bit. Keep DE asserted while physical TX is pending; expose failure separately from pending. |
| Change direction | One owner controls DE/RE and records its state. Apply documented transceiver setup/hold requirements, then release promptly after TX. |
| Receive | Deliver bounded bytes plus usable timing and UART error evidence. Overflow, parity, framing errors and lost timing evidence must not silently become a valid frame. |
| Read time | Use a monotonic microsecond time source. Keep its epoch and meaning consistent with RX/TX observations; the runner must not read a platform clock itself. |
| Recover | Settle or explicitly abort the physical transmitter, restore receive mode and drain stale traffic under application policy. Recovery does not prove that a timed-out command was not executed. |

FieldCore already calls `uart_wait_tx_done(UART_NUM_2, ticks)` instead of
relying on free FIFO space. The reviewed IDF v5.5 implementation checks the
transmitter state and the TX-done event. It can be polled with a zero-tick
wait; the adapter must map timeout to pending while its own deadline remains
open. `uart_tx_chars()` avoids waiting for FIFO space, but its implementation
still takes the driver TX mutex with an unlimited wait. Exclusive UART
ownership is necessary before treating this as a bounded callback.
[IDF UART implementation](https://github.com/espressif/esp-idf/blob/v5.5/components/esp_driver_uart/src/uart.c#L1356)

The ESP32-S3 UART driver exposes parity/framing/overflow events and receive
timeout configuration. These are useful building blocks, but dequeuing an
event later does not create a precise byte-arrival timestamp. The eventual
adapter needs a reviewed framing strategy and measured timing uncertainty.
The runner requires usable bounds on TX stop-bit and RX start/stop times,
plus a receive observation watermark; version 0.5.0 added explicit uncertainty
intervals. See [the callback contract](../runner.md#adapter-timing-contract).
`uart_wait_tx_done()` establishes current idle but does not by itself supply
that actual completion timestamp. Reusing the call does not complete the
adapter's timing work.
Driver-controlled RS485 half-duplex is another option; FieldCore currently
enables that only in an optional HIL variant. Select and qualify one direction
owner rather than combining driver RTS control with manual GPIO writes.
[Espressif UART guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/uart.html)

The exact framework version resolved by the standalone build must be checked
again during adapter implementation. These IDF v5.5 references explain the
mechanism; they are not qualification of every Arduino package or board.

## Memory and PSRAM

The runner and codecs should allocate nothing. The application provides fixed
storage and decides its placement. This permits native arrays for tests and
PSRAM-backed task-context storage on E2 without an ESP32 dependency in core
headers. Fixed capacity does not require internal RAM or repeated allocation.

FieldCore's [PsramAllocator.h](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/include/TunnelMonitor/core/PsramAllocator.h)
uses `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`. Its RS485 runtime allocates ingress
and trace storage once. Its worker stack and queue control remain static
internal storage. This is a useful placement pattern, not a requirement to
copy its 8 KiB task or its duplicate diagnostic rings into a small example.

Recommended placement for the later adapter:

| Storage | Placement and policy |
| --- | --- |
| Full diagnostic history, retained raw frames, future test results and larger caches | Prefer one bounded PSRAM allocation at initialization. Report allocation failure or deliberately select a smaller documented capacity. |
| Caller-owned TX/RX and task-only runner state | PSRAM is suitable when accessed only from normal task context and the driver contract permits those buffers. Retain storage for the whole transaction. |
| ISR bookkeeping, cache-disabled paths and driver-required storage | Keep internal. Follow the exact driver allocation contract; do not pass arbitrary PSRAM buffers to DMA. |
| Task stacks, synchronization objects and tiny timing state | Keep internal initially. Measure stack use before reducing it; moving a small object saves little compared with moving histories and caches. |
| Immutable register descriptions and lookup tables | Leave in the linked read-only image. Do not copy them into either RAM merely to expose a diagnostic view. |

External RAM can be inaccessible when the flash cache is disabled. ESP-IDF
normally keeps task stacks internal; ESP32-S3 DMA has additional restrictions,
including internal DMA descriptors. These constraints justify retaining a
small internal working set even on an 8 MB PSRAM board.
[Espressif external RAM guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/external-ram.html#restrictions)

Use capability-aware allocation for the intended memory region and retain
free, minimum-free and largest-free-block measurements separately for
internal memory and PSRAM. A free-byte total alone does not show whether the
next required contiguous buffer can be allocated.
[Espressif heap APIs](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/mem_alloc.html)

No PSRAM allocator is needed in the native runner. Actual allocation, fallback
policy, memory measurements and ESP32 timing remain adapter qualification work.

## Diagnostics and later Python runners

Keep a terminal transaction snapshot with raw byte lengths, TX acceptance,
physical TX completion, direction state, RX timing, timeout phase and codec
status. Printing belongs outside the timing path. A host trace identifier is
useful for logs, but is not a Modbus wire transaction identifier.

FieldCore's [framed serial helper](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/scripts/hil_serial_cli.py)
provides bounded command/response collection with explicit end markers. Its
[RS485 stress runner](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/scripts/hil_rs485_stress.py)
records firmware identity, configuration, before/after counters, serial logs
and cleanup. Its [memory probe](https://github.com/janhavelka/FieldCore-node/blob/063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f/scripts/hil_memory_stack_probe.py)
records memory and stack checkpoints. Reuse these small patterns when a real
standalone console exists; their current product commands and sensor fixtures
do not apply to ESS unchanged.

The first motor automation should use bounded non-changing probes and record
timeouts, malformed replies, lost traces, latency distribution, reset/panic
evidence and internal/PSRAM watermarks. Cached health and state watching must
not accidentally generate extra bus traffic. Later motion stress needs
explicit command identity, uncertain-execution handling and a separate stop
path; a failed acknowledgement must never cause automatic motion replay.

Native fake tests establish runner state transitions and bounded behavior.
They do not qualify UART timing, echo wiring, PSRAM under load or the motor.

## Develop the integration reference here

On 2026-10-03 the user requested that the major transport development and
testing happen in this simpler repository, leaving FieldCore read-only for
the session. This is feasible and is the recommended development route.
Keep a working reference and portable test scenarios here; later FieldCore
integration must still validate its actual scheduling, devices and hardware.

A fresh read-only source check used the FieldCore checkout at HEAD
`560f8a234d8d840b4737c61dcb4196f438ca01a1`. The inspected transaction header
still limits TX to eight bytes; `processResponse()` still strips a matching
request prefix; completion still uses fixed length or suffix; the backend
returns untimed byte batches; and the worker still delays 5 ms between cycles.
These observations concern the inspected source, not a new FieldCore build or
test. The earlier pinned source review above retains its original provenance.

The existing MotorControl-RS runner already covers the bounded single-request
state machine, separate TX acceptance/completion, explicit echo, exception
length, fault cleanup and retained diagnostics. Reuse that implementation and
its tests. Keep motor codecs, units and future profile sequences in the public
framework-independent library; keep bus ownership, queues, UART capture,
FreeRTOS and health policy in the standalone application layer.

| Develop and test here | What later remains in FieldCore |
| --- | --- |
| UART capture under realistic task/interrupt/console load; physical TX drain and DE control | Adapt its one existing UART owner/backend to the qualified capture strategy; repeat measurements with its full workload |
| Transaction behavior: reviewed request sizes, FC06 echo/ack distinction, normal/exception replies, late traffic and uncertain writes | Map request/result fields and deadlines without losing information; retain compatibility with existing sensor protocols |
| Small bounded bus-owner reference with multiple producers, explicit admission, queue limits and priority for a pending stop | Connect its command ingress, device module lifecycle and scheduling; verify fairness, cancellation and stop latency in the real runtime |
| Motor profile preparation/sequencing and state interpretation, with native fault injection | Integrate the motor module, product configuration and health presentation |
| Raw traces, memory watermarks, deterministic scenarios and Python campaigns | Run the same cases through its integration adapter, then add product-specific regressions |

Avoid copying the complete FieldCore task and its runtime dependencies here.
That would create a second firmware to maintain. Implement only the ownership
and scheduling behavior needed to exercise the transport and motor contracts.
Later reuse should favor the tested helper where its boundary fits; if a direct
reuse does not fit, port the small mechanism with its regression scenarios.
Do not maintain two independently evolving copies of the transaction engine.
Packaging a shared transport component can be decided at that concrete reuse
point; it is not a reason to put UART ownership into the motor library now.

### First block: capture and scheduling evidence

The polling E2 adapter is a working bench reference, not a proven backend for
FieldCore's worker. At 115200 baud an 8N1 character lasts about 87 microseconds;
5 ms spans about 58 characters. Our adapter deliberately rejects more than one
byte waiting in the hardware FIFO. Increasing the buffer or calling the same
polling code from a slower task cannot reconstruct the missing wire timing.
The four captured-byte slots are not an interrupt-fed receive buffer.
FieldCore's existing UART driver does buffer bytes independently of its worker;
a 5 ms worker interval does not itself establish byte loss. Its current backend
does not expose the timing/framing evidence required by our runner. Reliable
frame-level hardware evidence may be a suitable alternative to per-byte timing;
select that boundary from evidence, rather than requiring one interrupt per byte
or shortening every application task's polling interval.

Start with a load and fault test fixture around the existing adapter/runner:

1. Add deterministic native cases for delayed servicing, UART batch/overflow,
   late and foreign replies, queued requests and bounded result delivery.
   Separate actual wire events from the task's servicing time.
2. Establish an E2 workload fixture with competing task work and controlled
   logging/USB pressure. Measure CPU/service gaps, drops, latency, internal
   memory, PSRAM and stack use. Start with the existing non-changing probe.
3. Review a capture strategy that preserves sufficient hardware timing evidence
   while the owner task sleeps. Driver events or interrupt capture are candidates,
   not a selected implementation: delayed event delivery alone is not a timestamp.
   Extend the adapter/runner contract only if the measured strategy needs it;
   do not fake per-byte timestamps from a batch.
4. Qualify physical TX/RX/DE and framing boundaries with an independent capture.
   Native tests and a soak run cannot substitute for this evidence. Report
   unsupported load/timing cases explicitly until that qualification exists.

Then add the smallest application bus owner needed for multiple request
producers and stop priority. Admission must be bounded, queue-full behavior
explicit, and a device's wait between sequence steps must release the bus.
A pending stop gets the next permitted opportunity after settling the in-flight
transaction; it cannot cut through an RTU frame or override unresolved bus
recovery. Define and measure the latency bound for that case. Local cancellation
is still not a motor stop, and bus failure cannot guarantee a physical stop.

Use fake responders for writes, exceptions, dropped acknowledgements, echoes
and incompatible frames before exercising typed motor operations on the bench.
Keep normal-response correctness, rejection of bad evidence, service latency
and resource limits as separate pass criteria. Existing FieldCore fixed-length
and suffix protocols will need their own regression cases; this RTU runner
must not silently redefine how all existing serial devices complete frames.

The result should be working code, reproducible tests and a short mapping to
FieldCore's ownership contract. It can remove much of the protocol debugging
from integration. It cannot establish FieldCore thread safety, product timing,
shared-device behavior or memory use without subsequent tests in that firmware.

## Expected size of the FieldCore change

The user confirmed the small reference approach: reuse this repository's runner
and tests, with no copy of the FieldCore task. The following is an engineering
assessment from inspected source, not a completed implementation or a time quote.

For the full motion contract, budget a medium-to-large change within the RS485
and device-command subsystem. The existing FieldCore architecture can remain:
one bus owner, device modules, bounded queues, retained results and diagnostics.
Adding read-only motor observations is a smaller step than adding dependable
movement, interruption and uncertain execution handling.

| Area | Inspected behavior and required change | Relative scope |
| --- | --- | --- |
| Request storage | Eight-byte TX limit must accommodate reviewed ESS requests up to 21 bytes. Check every containing queue/structure's memory use. | Small, with regression checks |
| Response handling | Request-identical prefixes are stripped, which can discard a genuine single-register write acknowledgement. Add explicit echo policy and normal/exception framing while preserving existing sensor framing modes. | Moderate |
| UART/backend contract | Current reads return untimed batches; DE changes return no success/failure result. Add usable capture/error evidence, physical TX drain and confirmed or explicitly uncertain direction state. Validate under actual task/interrupt load. | Substantial; hardware evidence determines the final design |
| Commands and results | Current requests are Probe/Measure/ReadLast and readings use float. Add typed motion intent, exact native integer values and separate accepted/acknowledged/completed/unknown outcomes. Carry these through ingress, binding and result delivery without changing sensor meanings. | Substantial contract work across several files |
| Cancellation and scheduling | Current cancellation clears active transport after requesting DE release. Motor operations need transport settlement, distinct local cancellation versus drive stop, pending-stop priority and explicit queued-motion disposition. | Moderate to substantial |
| Retry and recovery policy | The inspected SHZK module owns its retries; the bus owner is not a universal automatic retry engine. Preserve that separation. A motor module must handle lost write acknowledgements without automatically repeating a possibly executed command. | Focused motor policy plus bus-recovery integration |
| Product integration | Add the motor device module, configuration, composition and cached health/state presentation. Test compatibility with existing devices and the complete workload. | Moderate, after transport behavior is proven |

The main behavioral difference is what a request means. A sensor transaction
usually obtains an observation. A motor write may start an action that continues
after the acknowledgement. If the reply to a relative move is lost, repeating
the command can cause a second move. Cancelling the host's wait does not cancel
the drive's movement. Those facts require different operation state and explicit
recovery decisions, even when the wire protocol is still Modbus RTU.

The ESP32 is not being asked to generate the motor's step pulses or close the
drive's internal servo loop. It must deliver commands reliably, retain what is
known about their execution, observe completion/faults and schedule stop within
a defined bus-service budget. A functioning bus is still required for a serial
stop command to reach the drive.

The remaining work here has two separate parts: reliable capture while other
tasks run, and a small bus owner that decides which request may use the wire
next. Existing codecs and the one-transaction runner are reusable foundations
for both. Then typed drive operations add their own sequencing and state rules.
These are real implementation blocks; the current read-only probe does not
already supply them.

Developing those blocks here can turn later FieldCore work into adaptation of
tested mechanisms with known limits. It cannot make integration a header-only
addition or remove tests for concurrency, old sensors, memory and product load.
No reliable line count, effort percentage or completion date follows from this
source review alone. Start with capture/load evidence, then add the bounded owner
and reuse the same scenarios when integration is separately authorized.
