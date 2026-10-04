# Bounded RTU admission, scheduling and retained results

Host tuple selection uses `beginConfiguration(now)` and
`finishConfiguration(timing, now)` in the same cooperative owner context.
The lease requires no queued or active work, no transport fault and settled DE;
it preserves unread results and logical endpoint/producer generations.
While held, ordinary/urgent admission returns `CONFIGURING`, service cannot
dispatch, and sequence/recovery entry is refused. Failure keeps the lease until
explicit successful repair. See [host serial support](host_serial.md).

`MotorControlRSExample::Rtu::BusOwner` in
[RtuBusOwner.h](../examples/common/RtuBusOwner.h) is application support outside
the installed library. It owns bounded admission, one active Runner transaction
and retained completions in one cooperative context. Construction initializes
caller-supplied slots without I/O. Logical producers use that context; calls
are not thread-safe. Prompts 01–03 implement and connect this owner to the console.

## Admission and lifetime

The application supplies a Runner with the existing portable callbacks, timing
and exclusive TX/RX buffers, plus `PendingSlot[]`, `ResultSlot[]` and `ProducerSlot[]`
arrays in `BusStorage`. These arrays stay alive and exclusive for the owner's
lifetime. They cannot overlap each other, owner/runner objects or Runner
buffers/trace. Pending/result capacities are 0..65535; zero rejects admission.
Producer capacity is required and is 1..65535. Indices are local to the owner;
slot fields are private while owned.
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

`urgentPendingCapacity` reserves part of the total queue quota;
`urgentResultCapacity` reserves the last result slots exclusively for urgent
work. Ordinary admission cannot borrow either reservation, even when unused.
`admitUrgent` accepts immediate work only (`notBeforeUs <= nowUs`). Urgent queue
or result pressure returns `URGENT_FULL`; zero reservations disable that path.
Unread urgent results keep their slots until release. Admission during pending
recovery returns `RECOVERING`; a settled fault permits admission but blocks dispatch.

IDs contain the originating live owner, result slot and nonzero generation.
Reuse at admission increments generation without wrapping. Wrong owner, slot
or generation cannot inspect/release a result. IDs expire with owner lifetime,
including reconstruction at the same address. `result(id)` is a non-consuming
const terminal view. `release(id)` frees it explicitly and invalidates the view.
Pending/active reservations cannot be released prematurely.

## Service, parser settlement and recovery

`service(nowUs)` polls Runner once, publishes at most one active completion,
expires queued deadlines in one bounded scan and dispatches at most one
surviving request. Compaction repairs owned byte pointers and preserves order.
Owner service caps receive callbacks at `READ_BUDGET` (64), including recovery;
after an active poll finishes, recovery draining waits for the next service.
Exhaustion
proves neither silence nor expiry. No retries, allocation, clock reads, sleeps,
RTOS, board or product types are introduced.

A FRAME enters the admitted `Validator::checkReply` while the active reservation
still prevents another dispatch. Only after classification is the result
published and further work allowed:

| Outcome | Evidence |
| --- | --- |
| `QUEUE_EXPIRED` | Admitted deadline elapsed before dispatch; no accepted TX. |
| `DISPATCH_EXPIRED` | Earlier Runner-handoff deadline elapsed while queued; no TX. |
| `CANCELLED` | Local cancellation; inspect accepted TX and uncertainty. |
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

## Fairness, dispatch and sequence waits

One shared queue preserves ordinary FIFO within each producer. Dispatch chooses
the next eligible producer cyclically after the last ordinary dispatch. A deferred
head blocks later ordinary work from that producer; others can run. `notBeforeUs`
releases the bus during a sequence wait. `dispatchDeadlineUs` bounds the latest
Runner handoff, not physical enqueue; zero normalizes to the absolute request
deadline. Physical setup/TX remain bounded by that original deadline. Expiry
takes precedence over a later queued cancellation; no wait renews either deadline.

Urgent work preserves FIFO within the urgent class and bypasses ordinary heads,
including a deferred head from its own producer. It gets the next permitted
opportunity after active classification and physical settlement. Among P
continuously eligible ordinary producers, a producer gets its next turn after
at most P−1 other ordinary turns, plus intervening urgent turns. This is turn
fairness, not equal airtime. An unbounded urgent flood has no ordinary fairness
guarantee. Priority cannot bypass a fault or erase an uncertain outcome.

`beginSequence(producer, absoluteDeadline, nowUs, SequenceId&)` requires that
producer to have no queued/active work and creates/replaces its optional token.
Every continuation retains the same token and absolute deadline. This is a host
stale-work barrier, not a motor operation ID or axis reservation.
`invalidate(sequence, nowUs)` cancels matching work and invalidates a token even
between steps. `invalidateProducer(producer, nowUs)` cancels all producer work;
applications call it before publishing configuration/rebinding changes.
Independent requests need no token. Generations never wrap; exhaustion prevents reuse.

## Cancellation and explicit recovery

`cancel(RequestId, nowUs)` cancels unsent work without TX. Cancelling a sequence
request invalidates its token and cancels queued continuations; independent
requests from that producer survive. Active TX drains fully. Cancellation is
local workflow control, never a physical motor stop. Repeated cancellation keeps
the first cause/cutoff and cannot alter a retained terminal result. `CANCELLED`
means accepted cancellation, which may still require service. `ALREADY_TERMINAL`
means a terminal or higher-priority completion won; stale/released/foreign IDs
return `INVALID`.

Captured qualified closure before the cancellation cutoff can win even across
read-budget passes. Runner's `cancelCaptured` retains that RECEIVE cutoff;
later closure cannot succeed and straddling closure is `TIMING_UNCERTAIN`.
A cancellation accepted before the request and response cutoffs remains the
controlling cutoff even when evidence is serviced after those later limits.
An earlier request/response expiry still wins when cancellation arrives later.
At/past the request deadline no fresh cancellation cutoff replaces its evidence;
an already accepted earlier cancellation is retained. Results preserve original
context, first cancellation cause and execution uncertainty.

`recover(nowUs, absoluteRecoveryDeadline, uint64_t& id)` reserves one distinct
control-result slot inside the caller-owned owner, cancels queued ordinary/urgent
work using their existing reservations, invalidates all sequences and settles
the active transaction. It preserves every unread request result. Applications
submit fresh work after recovery; old writes never resume. Recovery IDs are
nonzero generations scoped to that live owner, never wrap, and are separate
from request IDs and command correlation.

Service waits for physical TX/hold/DE settlement, drains stale RX through
`Runner::discard` in bounded passes, then requires fully observed t3.5 quiet
starting no earlier than the recovery request. It calls Runner recovery only
before the recovery deadline. `recoveryResult(id)` is non-consuming;
`releaseRecovery(id)` explicitly frees a matching terminal. Pending/unread control
results reject another recovery with `RESULTS_FULL`, independently of ordinary
and urgent pressure. Outcomes are `RECOVERED`, `EXPIRED`, `READ_ERROR` and
`TRANSPORT_ERROR`. Failure keeps dispatch interlocked even when historical
success arrives after recovery expiry. Physical cleanup may continue afterward
without changing the retained recovery outcome.

Sticky adapter faults may require explicit adapter cleanup after TX/DE settle
before a fresh attempt; read errors are surfaced, never silently cleared.
Discarding preserves valid byte-order and watermark bounds across service calls
and recovery retries. Contradictory adapter evidence produces `READ_ERROR` with
`CLOCK_ERROR` and keeps the bus interlocked; cleanup retains the clock epoch.
Modbus RTU has no wire request ID. A bit-identical delayed reply after recovery
may remain indistinguishable from a fresh checked reply. No finite idle guard,
host generation or priority removes that limitation or clears an uncertain
motor operation. Fake tests deliberately demonstrate it.

## Conditional urgent latency bound

Let S bound time between owner service/API opportunities, including bounded
scans/copies and validators; B bound buffered traffic per transaction; C bound
capture lag; X bound physical TX/hold/DE cleanup even after a fault. Let R bound
remaining absolute request budget for each transaction ahead of urgent work.
A conservative settlement allowance is
`T = R + X + C + (ceil(B/64) + 8)*S`. Eight service allowances cover dispatch,
bus wait/setup, TX observation/hold, closure, publication and handoff. The absolute
deadline bounds waits; X covers physical cleanup that cannot be truncated.
Bus, TX and response budgets can shorten T; they cannot extend R.

For admitted urgent work surviving without a recovery-triggering fault, K earlier
urgent requests give a conservative physical enqueue bound
`A + K*T + busTimeoutUs + setupUs + (ceil(B/64) + 3)*S`, with A <= T for current work.
The bus-wait budget includes establishing the required t3.5 idle gap; continuing
foreign traffic instead produces a fault/expiry and invalidates the delivery bound.
K is at most urgent pending capacity minus one. This assumes finite S/B/C/X,
valid evidence, adequate deadlines and no parser/transport fault. Queue capacity
alone cannot bound elapsed time. Prompt 03 must measure application budgets.

A fault requires explicit bounded recovery and fresh urgent submission; recovery
cancels queued urgent work, so the old request has no delivery bound. Full urgent
reservations, expiry, stalled TX/DE, unbounded service/capture gaps or an unresolved
uncertain operation can prevent delivery. No ESS stop command is implemented
here. Fake requests prove scheduling only; physical stop delivery/completion
needs later profile, drive and timing qualification.

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
| BusOwner / BusRequest | 216 / 128 | 144 / 96 |
| PendingSlot (256 frame bytes included) | 392 | 360 |
| Completion (256 raw bytes included) | 496 | 464 |
| ResultSlot (Completion included) | 512 | 480 |
| ProducerSlot / SequenceId | 24 / 24 | 24 / 16 |
| RecoveryResult (included in owner) | 64 | 56 |
| Runner / Result | 384 / 80 | 320 / 80 |

Owner/slots consume `sizeof(BusOwner) + Q*sizeof(PendingSlot) +
R*sizeof(ResultSlot) + P*sizeof(ProducerSlot)`, separately from Runner and TX/RX/trace.
Four pending, six retained and three producer slots use 4928 native or 4536
ESP32-S3 bytes. These are static sizes, not runtime peaks. Xtensa `-Os -fstack-usage`
reports 544-byte constructor, 512-byte admission, 64-byte service/group cancellation,
128-byte recovery, 32-byte collect and 96-byte ESS
reply-validator frames; callee stack use is additional.

[bus_owner_test.cpp](../test/bus_owner_test.cpp) uses real Runner, independent
fake wire times and no trace ring. The
[prompt 01 report](reports/ess_release_01_2026-10-03.md) records exact evidence.
The [fresh prompt 01 audit](reports/ess_release_01_audit_2026-10-03.md) corrects
timeout precedence and relative byte-expiry closure evidence.
The [prompt 02 report](reports/ess_release_02_2026-10-03.md) and
[scheduling tests](../test/bus_scheduling_test.cpp) hand these actual APIs and
lifetime/deadline/parser contracts to prompt 03. The console now exposes `drv`,
`result`, `release`, `cancel` and asynchronous `recover`; see [the probe guide](esp32_probe.md).

Prompt 03 retains TX end/uncertainty and first RX start/maximum uncertainty in
`Result`, so later dispatch cannot overwrite diagnostic evidence. WAIT_BUS
discards are excluded. These diagnostics do not require optional trace storage.
`service(nowUs, recoveryReady=false)` still settles/collects active TX and
enforces the recovery deadline, while postponing drain/reinitialization until
the application completes safe adapter cleanup. The standalone application
uses this gate for explicit 500-ms recovery settlement; no automatic recovery.
The [fresh prompt 02 audit](reports/ess_release_02_audit_2026-10-03.md) corrects
delayed cancellation cutoff precedence and recovery evidence across drain passes.
