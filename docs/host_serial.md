# Host serial tuple support

Release prompt 19 changes the standalone application's host UART settings.
It does not change ESS registers, save drive parameters or change a selected
endpoint. The implementation remains application support under
`examples/common/`, outside the installed `MotorControlRS` core.

The reviewed intersection is four ESS baud rates, **9600, 19200, 38400 and
115200**, with each of **8N1, 8N2, 8E1 and 8O1**: sixteen tuples. The original
[ESS function manual](vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf),
physical page 69, documents these values. The pinned ESP32-S3 SDK supplies
eight data bits, disabled/even/odd parity and one/two stop bits. Other baud
rates, data lengths and stop/parity combinations are rejected before UART,
GPIO or capture-timer changes. SDK support is separate from wire qualification.

## Application APIs and ownership

[`HostSerial.h`](../examples/common/HostSerial.h) supplies `HostFormat`,
`HostTuple`, `sameTuple`, `bitsPerCharacter`, `formatName`, `reviewedHostTuple`
and the checked `hostTiming(tuple, output)` helper. Invalid timing input leaves
the output unchanged. These types contain no MCU or framework types.

[`Esp32S3Uart.h`](../examples/common/Esp32S3Uart.h) exposes:

| API | Effect |
| --- | --- |
| `supports(tuple)` | Reports the finite reviewed SDK/ESS intersection. |
| `begin(pins, tuple)` | Sets up the single exclusive UART2 adapter. The existing baud-only overload selects 8N1. |
| `reconfigure(tuple)` | Explicit idle host change or restoration, using the same adapter and pins. No motor transmission. |
| `tuple()` | Returns the last tuple whose SDK setup and baud readback succeeded; while blocked it is historical evidence. |
| `configurationBlocked()` | Indicates that another explicit configuration attempt is required. |
| `stats().actualBaud` | SDK readback of the programmed UART divider, rather than the requested nominal rate. |

One application owner calls the adapter. The adapter object and ISR working
storage stay in internal RAM; the object must outlive its port callbacks and
capture timer. It cannot coexist with another UART2 adapter, `HardwareSerial`
or an installed UART driver. Capture lifecycle and cache-off restrictions
remain those in the [runner and adapter guide](esp32_probe.md).

[`RtuBusOwner`](../examples/common/RtuBusOwner.h) reserves configuration with
`beginConfiguration(nowUs)` and releases it only through a successful
`finishConfiguration(timing, nowUs)`. Admission, urgent admission, sequence
creation, recovery and service cannot dispatch while this ownership is held.
Queued/active work, recovery, a runner fault or unsettled DE refuse initial
ownership. Retained terminal results remain available.

The actual standalone callback is `Probe::Host::hostSerial`, with
`Probe::HostRequest` and `Probe::HostSnapshot` in
[`ProbeConsole.h`](../examples/probe_cli/ProbeConsole.h). It also refuses active
profile continuations and enabled finite monitoring. This matters between
individual transactions, when the UART can be idle while an operation still
depends on its admitted tuple.

## Failure, restoration and capture epochs

The host snapshot retains **original**, **requested** and **active** tuples,
`activeKnown`, configuration failure, blocked state, serial generation,
timing policy and actual programmed baud. Original is the session's startup
tuple. Requested records the explicit attempted change or restoration. Active
is confirmed only after adapter setup and owner timing installation succeed;
while unknown, its retained value is historical and `actualBaud` is zero.
Restoration is an explicit attempt to select original; it is not automatic
rollback or a claim that a failed change preserved the previous UART state.

The adapter verifies actual baud within the provisional two-percent nominal
envelope. A failed timer stop/cleanup, UART setup/readback or timer restart
blocks traffic. `clear()` and statistics reset do not repair a configuration
failure. The application retains the configuration ownership after failure,
so other producers cannot transmit against an intermediate tuple.

Explicit repair can discard settled captured/FIFO data belonging to an old or
unknown tuple. Physical TX/RX and DE must still be idle. Ordinary changes
refuse retained/FIFO receive data. If the first application attempt refused
RX data before modifying the adapter, the application's explicit repair owns
the idle `clear()` before retrying configuration. Neither repair path replays
a transaction or sends a motor command.

An idle change stops and cleans up GPTimer capture, configures UART2, resets
old TX/RX observations, then restarts the previous capture period and DE hold
policy. Failed cleanup retains its owned timer stages for explicit repair.
The adapter tracks the valid exclusively owned timer's INIT/ENABLE transitions
in the pinned ESP-IDF 5.5.5 implementation, including an internal error returned
after the transition. The SDK fakes model that ordering. The current board SDK
has power management disabled; synthetic timer failures are native repair
evidence, not proof that those failure paths are reachable on this bench or
that a corrupted handle can be repaired.
Every successful change starts a fresh capture epoch; setup and stopped time
do not count as sampler starvation. `esp_timer` remains the same monotonic
microsecond clock. RX idle evidence must be sampled again; old completion,
direction-release and receive-watermark evidence cannot certify the new tuple.

## Timing policy and limits

8N1 occupies ten bits per character; the other formats occupy eleven.
`HostTiming` recalculates conservative character bounds from frequency
tolerance: minimum is floor(bits × 100000000 / (baud × 102)), maximum is
ceil(bits × 100000000 / (baud × 98)). The stop publication guard uses two
slowest tolerated bits for 8N2, one for other formats, plus two microseconds.
FIFO visibility alone is not documented as final-stop completion. These are engineering bounds,
not independent electrical measurements.

At or below 19200 baud, RTU t1.5/t3.5 use the selected character length.
Above 19200 they use 750/1750 microseconds. Only **115200 8N1** retains the
recorded **304-microsecond first-reply exception**; other tuples use their
ordinary t3.5. Final frame closure still requires t3.5. The exception does not
qualify another baud/format or another drive.

The selected capture period remains 20 microseconds, below the smallest
reviewed minimum character bound of 85 microseconds. Each tuple exposes its
own starvation limit; changing to an eleven-bit format changes that bound.
DE setup and hold are twenty microseconds. TX timeout is at least twenty
milliseconds and at least 32 maximum-length characters plus 10040
microseconds, covering the application's TX capacity at 9600 baud. Response,
request and recovery-guard policy remain 200/500/500 milliseconds; these
cover the reviewed bounded wire lengths and remain host policy rather than
vendor-guaranteed response deadlines. See the
[timing audit](reference/09_timing_and_gap_audit.md).

## Generations and retained observations

Each admitted application record retains its intended host tuple and serial
generation. Terminal and `result` JSON report this as `host_serial`, including
historical results after a host change. Host changes invalidate current device
presence, communication, configuration/state freshness and readiness confidence.
An old unharvested result cannot restore that confidence under a new serial
generation. Its original payload and provenance remain inspectable until release.

Serial generation is separate from endpoint binding and motor configuration
generations. Selecting another host tuple does not rebind the endpoint,
invalidate prepared coordinates solely because of UART selection, erase a
host origin or clear physical execution uncertainty. New checked configuration
reads still compare retained raw settings and invalidate motor assumptions if
they establish actual device changes. The standalone changes one selected
host session; it does not implement a multi-endpoint tuple scheduler.

Cross-read reconciliation uses the retained checked target provenance even
when a host change has cleared current freshness. Both full configuration and
grouped driver/control/I/O reads compare that baseline. Freshness and readiness
still require a current observation; a retained baseline does not establish
either. Changed settings invalidate affected origins, scales and prepared
targets; unchanged settings preserve them.

## CLI, Python and next steps

`host` and `host caps` query the application snapshot. Explicit changes use
`host baud RATE`, `host fmt FORMAT` or `host set RATE FORMAT`; `host restore`
selects original. When active is unknown, supply a complete `host set` or
`host restore`. `config`/`settings` show current known host settings, and
historical operation output keeps its admitted tuple.

The bounded [`bench_probe.py`](../scripts/bench_probe.py) Console API accepts
`command("host", host_args=(...))`. Its `host` mode performs one query or
explicit change. `host-check --baud RATE --fmt FORMAT` performs a baseline
probe, one deliberate mismatch probe, a declared recovery only after the
expected NO_RESPONSE evidence, explicit restoration and one final probe.
Each read is attempted once, inspected and released. Unexpected failures
stop the scenario; restoration failures remain visible. A transmitted read
without a checked reply retains conservative `execution_unknown` evidence;
it does not prove the drive failed to receive the request. No motor-setting
writes or automatic read retries are part of this scenario.
An incorrect host tuple can instead produce malformed bytes or UART errors.
Those results fail this strict NO_RESPONSE scenario. Cleanup attempts restoration
once; a recovery interlock can refuse it. The scenario reports that failure and
retains the mismatched host state. A subsequent diagnostic recovery/restoration
must be explicit. Queries and refusals preserve failure, divider readback and
capability diagnostics; attempted reads include interrupted or lost admissions.

[Prompt20 communication commissioning](ess_communication.md) reuses these
restoration/failure APIs under `BusOwner::beginCommissioning` token ownership.
Normal producers remain excluded until explicit session finish; host recovery
alone does not prove a responding candidate. Prompt22 can reuse the same owner
for bounded discovery; no discovery orchestration is delivered here.

Native adapter/application/fake-console tests establish software behavior.
This document makes no new hardware qualification claim. Alternate tuple
communication, electrical TX/RX/DE and sampling bounds, echo exclusion and
load/soak evidence require separately recorded checks against the actual
image, motor settings and instruments.
