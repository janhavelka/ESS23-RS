# Standalone runner: FieldCore and ESP32 review

Reviewed on 2026-10-03 against FieldCore-node commit
`063ca063a50bcb8afb2b3b59f9af0f3f89bfc82f`. Its inspected RS485 files had no
local changes. This is source review and guidance for the later E2 adapter;
no firmware was flashed and COM13 was not opened. The native transaction
runner belongs under `examples/common/`. FieldCore continues to own its bus.

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
The implemented runner requires actual TX stop-bit and RX start/stop times,
plus a receive observation watermark; see [the callback contract](../runner.md#adapter-timing-contract).
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
