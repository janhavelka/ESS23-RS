# Standalone RTU transaction runner

First implemented in version 0.4.0, extended with timing intervals in 0.5.0, under
[RtuRunner.h](../examples/common/RtuRunner.h) and
[RtuRunner.cpp](../examples/common/RtuRunner.cpp), namespace
`MotorControlRSExample::Rtu`. The [native tests](../test/runner_test.cpp)
provide a fake UART with independent wire times. The [ESP32-S3 probe](esp32_probe.md)
now adds a hardware adapter and read-only console with explicit qualification limits.
The [bounded bus owner](bus_owner.md) adds fair admission, copied requests,
reserved retained results and synchronous checked-parser settlement around this
same Runner. Release prompt 03 connects it to the responsive standalone console.

## Boundary and responsibilities

The runner is application support in the repository, outside the installed
library core. It collects one RTU transaction at a time. Existing ESS codecs
still build requests and validate reply contents; they do not know about the
runner. FieldCore will keep its own bus owner and can adopt the tested rules
and small adapter mechanisms when integration is requested.

Construction performs no I/O. The caller supplies configured port callbacks,
timing policy, TX/RX storage and an optional trace ring. All storage is fixed
and remains caller-owned for the runner's lifetime. The runner copies each
accepted request into the supplied TX buffer and does not retain the original
request pointer. The object cannot be copied. One owner calls its methods;
there are no tasks, locks, heap allocation, clock reads or automatic retries.

```text
ESS builder -> application TX request -> Runner -> port callbacks
                                                   |
ESS parser <- retained RX + Result <----------------+
```

The application checks `Admission` from `start()`, calls `poll(nowUs)`, then
inspects `result()`. `Reason::FRAME` means a complete envelope was collected.
It does not mean valid CRC, matching identity, successful command or finished
movement. The application must run the corresponding checked ESS parser.
If that rejects the frame, settle the possible real/late reply under the
recovery policy before reusing the bus. An unrelated frame can precede the
expected reply.

## State progression

| Phase | Work |
| --- | --- |
| `WAIT_BUS` | Drain/log old receive activity; require a complete current observation and `t3.5` quiet before asserting DE. Check that physical TX is idle. |
| `SETUP` | Wait the configured transceiver setup time. New/incomplete receive activity blocks enqueue. Submit the whole request once. |
| `DRAIN` | Wait for the physical transmitter, independently of how many bytes the UART accepted. A short write is a failed transaction and is never resumed. |
| `HOLD` | Apply the configured hold after the actual final stop bit. Release DE as soon as that hold is satisfied; no mandatory extra poll is inserted. |
| `RECEIVE` | Collect timestamped bytes, account for explicit local echo and wait for a real frame boundary. |
| `DONE` | Retain the collected frame or a cancellation before transmission. A new accepted request replaces the previous result and raw buffers. |
| `FAULT` | Retain the failure and block new requests. Polling may release DE once physical idle is established, but cannot clear the recovery interlock. |

Each `poll()` has at most 64 receive callbacks. All loops and copies have
explicit limits. Reaching that receive budget is not evidence of an empty
UART, so the runner does not declare silence or a response timeout merely
because the budget ran out. No frame is accepted as soon as its expected
length arrives; trailing bytes and the final silent interval still matter.

## Adapter timing contract

All times use one monotonic `uint64_t` microsecond clock, supplied by the
application. Crossing the 32-bit microsecond boundary is supported. A
regressing clock or contradictory adapter timestamps causes `CLOCK_ERROR`.
Outside an active transaction, a regressing clock blocks admission and adds
a clock-error trace event while preserving the prior transaction result.

| Callback | Required meaning |
| --- | --- |
| `setTransmit(context, enabled)` | Set the one owned direction control; return failure if the requested direction is not established. The port starts physically idle in receive mode. |
| `write(context, bytes, length)` | One bounded attempt to enqueue a continuous RTU frame. Report accepted bytes and failure separately. No scheduler-created gaps between partial chunks are allowed. |
| `txState(context, nowUs, observation)` | Distinguish pending, physical idle and failure. After enqueue, `IDLE` supplies the latest bound on the final stop-bit time, with optional interval width. A polling observation alone is not an exact timestamp. |
| `read(context, nowUs, byte, observedThroughUs)` | Return one ordered wire byte, an error, or `EMPTY`. A byte includes start/stop intervals. On `EMPTY`, the watermark states how far all receive activity has been observed. |

An incomplete character or delayed capture holds the receive watermark back.
It cannot be replaced with the current polling time. Before asserting DE or
enqueueing, the runner requires that watermark to reach the current time.
During reception it uses the watermark to decide whether the final quiet
interval or response deadline has really passed. A stale watermark exceeding
the configured capture-lag budget produces `CAPTURE_TIMEOUT`, independently
of drive response timeout.

Exact silence between received characters is `next.startUs - previous.endUs`.
Counting stop-to-stop time instead would incorrectly include the next
character's wire duration. A gap above `t1.5` and below `t3.5` invalidates a
frame. A gap at least `t3.5` ends the previous frame; a byte from the following
frame is logged as discarded rather than appended to that previous frame.
The first response must follow the configured `Request.replyGapUs` from TX
completion and start after DE release. Zero selects `t3.5`. An explicit shorter
value is a device-specific exception and never shortens admission/final gaps.

RX `uncertaintyUs` defines start in `[startUs, startUs + uncertaintyUs]` and
end in `[endUs - uncertaintyUs, endUs]`. An atomic `TxObservation` defines TX
end in `[endedUs - uncertaintyUs, endedUs]` and optional physical DE release in
`[releasedUs - releaseUncertaintyUs, releasedUs]`. The complete release interval
must follow the latest TX end plus hold. A background adapter can retain these
observations while the owner sleeps; the runner advances to RECEIVE before
reading queued reply bytes. The deadline follows physical release evidence,
not the task wake-up time. In timer mode, the ESP32-S3 adapter reports BUSY until completion and
release evidence are both available at the supplied time. Zero width
retains the exact native-fixture behavior. Each gap must be valid across the
whole range; an ambiguous gap or deadline returns `TIMING_UNCERTAIN`.
`PENDING` supplies no silence evidence; prolonged lack of capture progress
fails with `CAPTURE_TIMEOUT`. The runner rejects impossible ordering, future
end bounds, backward watermarks and contradictory observations.
These checks detect an inconsistent adapter; they cannot prove that a
hardware adapter reported the actual wire correctly.

`Serial.available()` batches and FieldCore's current read callback do not
meet this timing contract by themselves. FieldCore's TX-drain mechanism is
useful, and the implemented [ESP32-S3 adapter](esp32_probe.md) brackets hardware observations.
External validation of its RX sampling assumptions remains open. Review UART events, hardware idle evidence and timing
uncertainty before connecting that adapter. See the
[platform review](reference/10_runner_platform_review.md).

## Timing, frames and echo

`setRtuTiming(baud, bitsPerCharacter, timing)` fills only `gap15Us` and
`gap35Us`, rounded upward in microseconds. It accepts 10-bit and 11-bit
formats. At or below 19200 baud it derives character intervals; above that
it uses 750/1750 us. The caller configures the actual UART format separately.
ESS 8N1 uses 10 bits; the Modbus serial standard uses 11-bit characters.
See the [timing audit](reference/09_timing_and_gap_audit.md) for this distinction.

The caller must explicitly set nonzero bus-admission, TX and capture-lag
deadlines. Setup and hold come from the transceiver/adapter requirements.
Each request supplies its response deadline and normal reply length. The
response deadline starts at physical TX completion and includes the final
framing gap. These are application budgets, not documented ESS guarantees.
A TX deadline does not guarantee that a stuck transmitter released the bus.

Optional `Request.deadlineUs` adds an immutable absolute request/closure limit.
New DE assertion/enqueue requires time strictly before that deadline; expiry in
setup releases DE without TX. Retained on-time TX/release and response closure
are processed before current task time declares expiry. `Result` exposes
`closureEarliestUs`, `closureLatestUs` and `closureQualified` without traces.
The full final idle gap must fit; straddling bounds fail uncertainly.
`REQUEST_DEADLINE` differs from relative response and capture timeout.
When both receive budgets expire, the earlier latest cutoff determines the
reason. Byte expiry retains candidate final-gap bounds for either budget;
uncertainty in a byte's stop time alone cannot mask a definitely late closure.
See [the owner contract](bus_owner.md) for deadline/evidence and lifetime rules.

The raw request must already have passed its profile's builder/validator.
The runner checks storage and basic unicast/function admission, not device
register policy or request CRC. Normal response length comes from the
request expectation; a matching exception function selects five bytes.
The profile parser then checks address/function/CRC/count/echo and preserves
raw exception codes. A bad CRC can therefore have transport reason `FRAME`
and codec result `CRC_ERROR`; those are separate facts.

Echo policy is explicit per request:

- `NONE`: no local copy is expected. A request-identical FC06 reply arriving
  in the valid response interval is preserved for the parser.
- `REQUIRED`: exactly one copy of the request must be observed during its
  transmit interval. Buffered echo can be processed later using its wire
  timestamps. Missing/mismatched echo is an error; a real reply after DE
  release cannot be consumed as the missing echo.

There is no "maybe echo" heuristic. Qualify the electrical/driver behavior
and select the matching policy. Do not strip every request-identical prefix.

## Failures, cancellation and recovery

`Result` retains the reason, start/end time, accepted TX count, physical TX
completion flag, RX length, echo count and RX truncation flag. A received
prefix stays in the caller's RX buffer on failure. It is not a decoded
value. Any accepted TX byte may have reached the drive; even a completely
drained transmitter does not establish acknowledgement or execution.

Cancellation before enqueue sends nothing. Cancellation while TX drains or
holds waits for physical completion and safe DE release. It does not erase
an earlier short-write error. Cancellation after enqueue is local workflow
cancellation, not a motor stop, and requires explicit recovery before reuse.
UART errors and DE failures also remain interlocked. A failed DE operation
leaves `transmitEnabled()` true as a conservative indication of uncertainty.

The owner's `cancelCaptured(nowUs)` uses an immutable RECEIVE cancellation cutoff
while examining retained history across read budgets. Qualified closure before
that cutoff can succeed; later closure cannot, and straddling bounds fail with
`TIMING_UNCERTAIN`. Direct `cancel()` retains its immediate RECEIVE semantics.
Neither method truncates physical TX or renews the absolute request deadline.
Cancellation, request expiry and response expiry use their retained cutoff order
for closure, late bytes and EMPTY evidence. Delayed servicing cannot replace an
earlier accepted cancellation with a later timeout; earlier expiry still wins.

`requireRecovery()` interlocks only a settled Runner, preserves its terminal
evidence and performs no I/O. `discard(nowUs)` is available only while faulted
with DE released. It removes at most 64 timestamped RX items, returns qualified
EMPTY/watermark or pending/error evidence, and preserves Result/raw data.
Budget exhaustion is not a silence proof. BusOwner uses it before explicit
recovery and stores the recovery outcome separately from interrupted requests.
Discard uses the same retained byte/watermark bounds as reception across passes
and recovery retries, rejecting regressions without changing Result/raw bytes.

`recover(nowUs)` checks physical idle and hold time, then establishes receive
mode. Failed recovery keeps admission blocked. Recovery retains the prior
result; a later `start()` drains old data and establishes a fresh idle
interval. It never retransmits the previous command or changes a motor.

The caller must first settle possible delayed replies using qualified timing
evidence or an explicit host recovery policy. Neither `recover()` nor an
idle gap can prove that a still-processing motor will never send a late
same-shaped FC03 response. Firmware reset/abort and stream cleanup, when
needed, belong to the adapter/application. Never silently translate an
uncertain movement into "not executed".

## Diagnostics and memory

The optional trace ring records phases, enqueue count, TX end bounds,
direction changes, every received byte including rejected bytes, echo,
discard, terminal result and recovery. RX events retain both start and stop
times. Ring order is observation order; delayed wire events can have earlier
timestamps than previously logged host phase changes. Oldest entries are
overwritten with an explicit counter. Snapshot access and name helpers have
no transport effects. Counters saturate; `clearStats()` does not clear the
result, buffers, trace or recovery interlock.

Measured with the configured native MinGW compiler and the installed
ESP32-S3 Xtensa compiler; these are `sizeof` results, not stack peaks or
whole-firmware RAM/flash usage:

| Storage | Native 64-bit | ESP32-S3 |
| --- | ---: | ---: |
| Runner object, including copied callbacks/configuration/result/statistics | 360 bytes | 296 bytes |
| One trace entry | 32 bytes | 32 bytes |
| Result alone, already included in Runner | 56 bytes | 56 bytes |
| Example caller TX/RX arrays | 32 + 64 bytes | 32 + 64 bytes |
| Optional 64-entry trace ring | 2048 bytes | 2048 bytes |
| Runner plus those arrays/ring | 2504 bytes | 2440 bytes |

TX and RX capacities are independently bounded at 256 bytes. Traces are
optional and caller-sized; the helper does not allocate a default large ring.
The ESP32-S3 probe allocates its application, buffers and 128-entry trace ring in
PSRAM once at initialization. Prefer this pattern
for larger task-context histories, retained frames and caches. Task-context
TX/RX buffers can use PSRAM where the driver permits it. Keep ISR,
cache-disabled and driver-required storage internal; initially keep stacks
and the small timing object internal too. The adapter keeps its small capture working set internal. Its
[bench report](reports/2026-10-03_e2_probe.md) records runtime memory; native
tests alone make no claim of runtime PSRAM use.

## Verification and next block

The runner suite exercises probe and exception frames through the existing
ESS parser, malformed framing/CRC boundaries, physical TX drain and hold,
partial enqueue, cancellation, echo modes, buffer overflow, timestamp faults,
capture lag, deadlines, recovery and trace overwrite. A deterministic
1,000-transaction soak reuses the same runner and buffers with varied
fragmentation, normal/exception replies and a clock crossing `UINT32_MAX`.

Run the repository's CMake/CTest commands from [README](../README.md).
The runner is also compiled independently with strict C++11 warnings and
the ESP32-S3 compiler. Those checks do not establish UART or motor behavior.

The [ESP32-S3 probe](esp32_probe.md) now supplies the adapter, read-only JSONL console
and Python probe/stress/cached-watch harness. Native SDK fakes compile the
actual adapter source. Bench probes establish a working communication path;
The independent wire fixture and timer load bench now exercise a sleeping
owner, FIFO batches/overflow, late/foreign replies, missing evidence and
interrupt masking. See the [capture/load audit](reports/2026-10-03_capture_load.md).
External TX/RX/DE timing, cache-off operation and motion remain separate
qualification work. Admission/results and fairness/cancellation are implemented
in the [bus owner](bus_owner.md); standalone console integration follows in
prompt 03 under the [roadmap](roadmap.md).
