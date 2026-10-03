# Bounded RTU bus admission and retained results

`MotorControlRSExample::Rtu::BusOwner` in
[RtuBusOwner.h](../examples/common/RtuBusOwner.h) is application support outside
the installed library. It owns FIFO admission, one active Runner transaction
and retained completions in one cooperative context. Construction initializes
caller-supplied slots without I/O. Logical producers use that context; calls
are not thread-safe. Prompt 02 adds scheduling/cancellation; 03 connects the console.

## Admission and lifetime

The application supplies a Runner with the existing portable callbacks, timing
and exclusive TX/RX buffers, plus separate `PendingSlot[]` and `ResultSlot[]`
arrays in `BusStorage`. These arrays stay alive and exclusive for the owner's
lifetime. They cannot overlap each other, owner/runner objects or Runner
buffers/trace. Each capacity is 0..65535; zero explicitly rejects admission.
Larger task-context arrays may use application-allocated PSRAM when suitable.

`admit(BusRequest, nowUs, RequestId&)` checks bounded wire/storage shape and the
mandatory profile request validator before copying bytes and expectations.
The caller may immediately discard a stack input frame and change its original
request. `Expectation` retains target/configuration generation, slave, function,
register start/count and FC06 value. Target metadata is retained, not an axis
operation reservation. Validator function pointers are copied without borrowed
context. Callbacks are bounded, synchronous, non-reentrant and retain no pointers;
returned Status messages have static lifetime.

Pending capacity counts queued work only. Dispatch frees the pending slot after
Runner copies the frame. Result capacity counts queued + active + unread terminal
work. Admission reserves completion storage or returns `RESULTS_FULL`; unread
results are never overwritten. `QUEUE_FULL`, `INVALID`, `EXPIRED` and
`IDS_EXHAUSTED` also reject admission without a terminal result or port I/O.
The output ID stays unchanged on rejection.

IDs contain the originating live owner, result slot and nonzero generation.
Reuse at admission increments generation without wrapping. Wrong owner, slot
or generation cannot inspect/release a result. IDs expire with owner lifetime,
including reconstruction at the same address. `result(id)` is a non-consuming
const terminal view. `release(id)` frees it explicitly and invalidates the view.
Pending/active reservations cannot be released prematurely.

## Service, parser settlement and recovery

`service(nowUs)` polls Runner once, publishes at most one active completion,
expires queued deadlines in a bounded FIFO scan and dispatches at most one
surviving request. Compaction repairs owned byte pointers and preserves order.
Runner still caps receive callbacks at `READ_BUDGET` (64) per poll; exhaustion
proves neither silence nor expiry. No retries, allocation, clock reads, sleeps,
RTOS, board or product types are introduced.

A FRAME enters the admitted `Validator::checkReply` while the active reservation
still prevents another dispatch. Only after classification is the result
published and further work allowed:

| Outcome | Evidence |
| --- | --- |
| `QUEUE_EXPIRED` | Admitted deadline elapsed before dispatch; no accepted TX. |
| `TRANSPORT` | Runner failure/expiry; raw prefix and transport evidence retained. |
| `SUCCESS` | Checked profile parser accepted data/acknowledgement. |
| `DEVICE_REJECTED` | Checked exception; raw exception byte in `validation.detail`. |
| `INVALID_REPLY` | FRAME failed profile validation; recovery interlocked. |

[EssRtuValidator](../examples/common/EssRtuValidator.h) checks requests against
the existing builders and reviewed access/windows, then calls `parseRegisters`,
`parseWriteSingleRegister` or `parseWriteMultipleRegisters` for replies. FC06
request-identical acknowledgements are retained; local echo remains Runner's
explicit policy. Raw writes are not typed/qualified motor operations.

`Completion` retains original context, admission/deadline, expected lengths,
response budget, turnaround/echo policy, raw response, Runner Result and parser
Status (meaningful only for FRAME). `executionUnknown` is conservative when TX
was accepted without checked success/rejection. SUCCESS is not motion completion.
Under continued valid servicing and bounded adapter evidence, each admitted
request publishes one terminal result; repeated polls leave it unchanged.

Runner recovery blocks dispatch. Fault service may safely release DE without
clearing the interlock. `recover(nowUs)` requires no active or pending work;
service can expire pending requests while interlocked. Recovery preserves unread
results and never resumes queued writes. The application first settles late
traffic or uses explicit host recovery, including adapter cleanup when required.
An idle guard cannot identify a bit-identical late RTU response. Prompt 02 adds
cancellation and queue disposition.

## Absolute deadline and closure evidence

`Request::deadlineUs` is required by the owner, copied unchanged from admission
through queue, dispatch, setup, TX and response. Direct Runner users can leave
it zero for existing relative-budget behavior. All times are supplied monotonic
uint64 microseconds in the port's epoch. Admission/service/recovery share
`Runner::checkClock`; regression interlocks without physical I/O or replacing
previous terminal evidence.

New DE assertion/enqueue requires `nowUs < deadlineUs`. WAIT_BUS/SETUP expiry
sends nothing; SETUP releases DE explicitly, with a failed direction action
interlocked. After TX acceptance, deadline failure preserves uncertainty and
physical TX must settle before cleanup/recovery. Safe cleanup is permitted after
expiry. Existing bus/TX/capture limits remain separate and apply as well.

The effective receive closure budget is the earlier of the absolute request
deadline and physical TX-relative response budget, including TX uncertainty.
Neither dispatch nor intermediate waits renew it. Retained autonomous TX/release
and RX history are processed before task time implies expiry. An on-time
response can succeed when serviced later. Capture lag still fails when no
qualified closure evidence exists.

`Result::closureEarliestUs` / `closureLatestUs` retain last stop-bit bounds plus
the full final t3.5. `closureQualified` means the watermark or next frame proves
the whole idle gap, independently of trace storage. Success requires qualified
latest closure <= deadline. A straddling interval is `TIMING_UNCERTAIN`, a
definitely late closure is `REQUEST_DEADLINE` for the absolute limit or
`PARTIAL_RESPONSE` for the earlier relative response limit. When both limits
have elapsed, the earlier latest cutoff determines the reason; uncertainty at
a later limit cannot mask definite expiry of the earlier one. Last-byte receipt
alone is insufficient. Both limits retain candidate closure bounds on byte
expiry, including the full final gap, without claiming closure proof.
Before any response byte, closure bounds remain zero: TX/DE uncertainty is a
transport failure, not evidence of a response closure.
`endedUs` is closure for FRAME and task observation for most faults; it is not
a uniform physical timestamp.

## Memory and next dependency

Measured `sizeof` with native 64-bit MinGW and ESP32-S3 Xtensa:

| Type | Native bytes | ESP32-S3 bytes |
| --- | ---: | ---: |
| BusOwner / BusRequest | 88 / 80 | 44 / 56 |
| PendingSlot (256 frame bytes included) | 344 | 320 |
| Completion (256 raw bytes included) | 424 | 400 |
| ResultSlot (Completion included) | 440 | 416 |
| Runner / Result | 352 / 56 | 288 / 56 |

Owner/slots consume `sizeof(BusOwner) + P*sizeof(PendingSlot) +
R*sizeof(ResultSlot)`, separately from Runner and its TX/RX/optional trace.
Four pending and six retained slots use 4104 native or 3820 ESP32-S3 bytes.
These are static sizes, not runtime peaks. Xtensa `-Os -fstack-usage` reports
464-byte constructor/admit, 48-byte service, 32-byte collect and 96-byte ESS
reply-validator frames; callee stack use is additional.

[bus_owner_test.cpp](../test/bus_owner_test.cpp) uses real Runner, independent
fake wire times and no trace ring. The
[prompt 01 report](reports/ess_release_01_2026-10-03.md) records exact evidence.
The [fresh prompt 01 audit](reports/ess_release_01_audit_2026-10-03.md) corrects
timeout precedence and relative byte-expiry closure evidence.
Prompt 02 reuses these actual APIs, reservation/lifetime rules and deadline/
parser contract. The console advertises owner commands only after prompt 03.
