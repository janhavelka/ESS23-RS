# ESP32-S3 standalone motor bench

This Arduino example uses the same application, owner, console and typed motor
operations as the [native ESP-IDF example](esp_idf_probe.md). Start with
[getting started](getting_started.md) or the [console guide](console.md).
There is no separate motion test firmware or raw-register command interface.

The reusable core has no board or SDK dependency. This adapter is ESP32-S3
specific; the pins below describe the recorded bench, not library defaults.
Use the GPTimer build `bench_s3_load_timer` for the measured motion path. Its
load fixture is disabled unless explicitly selected.

Both frameworks use one pinned IDF USB Serial/JTAG driver. Arduino HWCDC
auto-start is disabled. Output is nonblocking in bounded 64-byte chunks.
The owner watchdog checks completed service turns; a restart neither stops the
motor nor replays a command. Retained diagnostics and the USB investigation
are in the [overnight report](reports/2026-10-07_overnight_hil.md) and
[firmware health report](reports/2026-10-06_firmware_health.md).

Current position preparation applies the [shared speed/rate/ramp policy](ess_position.md#default-position-operating-limits).
[Boundary evidence](reports/2026-10-07_position_limits.md) records the latest
image and memory/timing measurements. Earlier [platform parity evidence](reports/ess_release_26_2026-10-05.md)
qualifies its recorded Arduino/native-IDF images, not every later image.
Electrical timing, calibrated shaft measurements and loaded mechanics remain
unmeasured. See [API/CLI coverage](ess_api_cli_coverage.md) for native-family gaps.

Use [named finite Python scenarios](bench_scenarios.md). The default quick
regression is read-only; motion/settings scenarios require explicit selection,
retain failures and keep one connection through their stop/cleanup procedure.

## Load and sleeping-owner capture

`bench_s3_probe` retains owner polling. Two optional build environments add the
same small load fixture: `bench_s3_load_poll` for the baseline and
`bench_s3_load_timer` for background capture. The latter samples UART2 with a
20-microsecond GPTimer alarm and releases DE after observed TX idle plus the
configured hold. A sleeping owner later receives atomic TX/release evidence
and the buffered RX timing records. Both modes share Esp32S3Uart, the runner,
ESS codecs, console and Python harness.

```powershell
.\scripts\pio.cmd run -e bench_s3_load_timer -t upload --upload-port COM13
python scripts/bench_probe.py --port COM13 --log build/bench/my_load.jsonl load --count 100 --interval 0.03 --work-us 2000 --owner-delay-us 5000 --console-bytes 128
```

The fixture adds these explicit host commands; they do not write to a motor:

| Command | Meaning |
| --- | --- |
| `load` | Snapshot current settings and measurement window; no bus traffic |
| `load 2000 5000 128` | Up to 2000 us of busy task work each 10 ms, at least 5000 us between active owner services, 128 diagnostic payload bytes per period |
| `load 0 0 0` | Disable workload, injected delay and diagnostic output |

Settings bounds are respectively 0..5000 us, 0..20000 us and 0..256 payload
bytes. These are input bounds, not a promise that every combination meets the
TX/response deadlines. Tick rounding and competing work can extend the actual
owner service gap. Load changes require an idle owner and settled DE. `stats reset`
and an explicit load change start a fresh fixture measurement window without
clearing faults. `stats reset` keeps the selected workload and last probe result.

[Esp32Load](../examples/probe_cli/Esp32Load.h) owns one priority-2 competing task on
the Arduino owner's core, a fixed 4096-byte internal stack and one protected
265-byte diagnostic ingress slot. It never calls the bus owner or writes USB.
Diagnostic lines start with `# load `; the configured size is payload.
The owner copies these into its bounded output queue when two slots remain
available for command records. All builds allocate a 1024-byte internal USB TX
ring before startup and use SDK timeout zero. One owner writes at most 64 bytes
per loop, retaining partial enqueue offsets and serializing complete lines.
USB enqueue evidence does not prove host receipt after a disconnect.
The harness stops on command/framing failure and never replays a probe.

`console_lines` counts full fixture lines admitted to the owner output queue,
not confirmed host receipt; `console_dropped` counts occupied ingress slots. A load run
must check these counters before claiming it exercised console traffic.
The Python campaign fails if requested work had no competing task iterations
or requested console output produced no complete lines.
It also requires the capture counters, gap limit and boolean gap diagnostic.
Missing or inconsistent evidence stops the harness. A valid sticky sampling-gap
report fails load qualification before the next bus read while leaving
diagnostic inspection available. Recovery remains an explicit operator action.
The Python load campaign sets the requested settings once, repeats probes and
cached diagnostics, and stops nonzero on the first failed probe. It gathers
cached failure evidence when framing is intact. It does not recover, retry,
or silently switch the workload off; do that explicitly after inspection.

`load` reports capture and active-owner gaps, sample/work durations, counters,
worker stack headroom and CPU estimates. `memory` retains owner stack and
internal/PSRAM free/minimum/largest-block values. CPU percentages come from
FreeRTOS idle runtime deltas and are unavailable for windows shorter than
100 ms or longer than one 32-bit runtime-counter revolution. Interrupt time
can be charged to the interrupted task, including idle. `capture_us` measures
the capture section separately, excluding driver dispatch/return overhead;
these measurements must not be added as if they were disjoint CPU categories.

The [initial capture review](reports/ess_release_04_2026-10-04.md) retains the
20-us sampler at about 20â€“21% of one core inside capture. `timer_callbacks`
counts timer alarms separately from aggregate `capture_samples`; neither
measures SDK interrupt dispatch cost. `capture_high_water` reports ring occupancy.

Cache-off operation while capture runs is unsupported: the pinned SDK does not
enable GPTimer cache-safe interrupts, and the callback call graph is not wholly
IRAM-resident. `config` exposes `cache_off_supported:false` and the conservative
85-us sampling gap limit at 115200 baud. Timer-mode gaps reaching that limit
latch a capture fault even with empty FIFO; `sample_gap_exceeded` survives
statistics reset and clears only through explicit idle recovery. Before runtime
flash/OTA/NVS/cache-off or sleep operations, quiesce admission, settle TX/DE
and idle recovery, then stop capture. Restart after normal operation returns.
The standalone has no such runtime service. ISR state/stacks stay internal;
owner buffers, trace and console remain in PSRAM. Electrical timing and physical
cache-off/starvation measurements remain NOT RUN.

The fixed long read uses the same admission, asynchronous result and explicit
release workflow as `probe`. Terminal `type` is `capture_read`; results identify
`capture_read`, `register_start:304`, `register_count:16`, raw TX/RX and
`raw_model:null`. It occupies ordinary read capacity and does not refresh model
cache age. The window is reviewed in function-manual PDF p77 (printed p75).
It is a timing fixture through the existing codec, not typed settings coverage.

```powershell
python scripts/bench_probe.py --port COM13 --log build/bench/long_read.jsonl capture-read
python scripts/bench_probe.py --port COM13 --log build/bench/long_load.jsonl load --capture-read --count 20 --work-us 2000 --owner-delay-us 5000 --console-bytes 128
```

## Build and board

From the repository root:

```powershell
.\scripts\pio.cmd run -e bench_s3_load_timer
```

The environment is in [platformio.ini](../platformio.ini). It extends the bench
units example's pinned pioarduino platform `55.03.311`, Arduino framework,
ESP32-S3 board definition, 16 MB flash, OPI PSRAM and shared USB Serial/JTAG settings. This
application uses the ESP-IDF UART setup and low-level register helpers shipped
with that framework. The reusable MotorControlRS library has no such dependency.

| Connection | Example setting |
| --- | --- |
| Physical board | User-confirmed ESP32-S3 bench, revision 2.0.0 |
| UART owner | Exclusive UART2 adapter |
| RS485 TX / RX | GPIO47 / GPIO48 |
| DE/RE | GPIO21, high to transmit, low to receive |
| Motor bus | 115200 baud, eight data bits, no parity, one stop bit |
| USB console | 115200 baud setting, separate from motor bus |
| Initial probe address | 1; an explicit probe argument accepts 1 through 247 |

The serial tuple is the documented ESS default used as a commissioning
candidate. It is not readback of the connected motor. Board provenance and
the previously running CO2control firmware are recorded in
[the board review](reference/04_esp32_bench.md) and
[bench notes](hardware_bench.md).

After inspecting the current port and retaining the existing firmware/build
information needed to restore it, the example can be uploaded explicitly:

```powershell
.\scripts\pio.cmd run -e bench_s3_load_timer -t upload --upload-port COM13
.\scripts\pio.cmd device monitor -p COM13 -b 115200
```

COM13 is the reported bench port; inspect its current identity before use.
An upload replaces the board's current application. Keep one owner of the USB
port and close the monitor before starting Python. Startup performs no motor
query, scan, configuration write or motion command.

## Code and ownership

| File | Responsibility |
| --- | --- |
| [ProbeApp.cpp](../examples/probe_cli/ProbeApp.cpp) | Own the application buffers, runner, UART, console, retained results, memory snapshots and recovery policy; shared by Arduino and native IDF. |
| [ProbeConsole.h](../examples/probe_cli/ProbeConsole.h) / [ProbeConsole.cpp](../examples/probe_cli/ProbeConsole.cpp) | Parse bounded lines, validate arguments, dispatch commands and retain reply format. [ConsoleText.cpp](../examples/probe_cli/ConsoleText.cpp) renders human text from the same evidence. Platform neutral. |
| [Esp32S3Uart.h](../examples/common/Esp32S3Uart.h) / [Esp32S3Uart.cpp](../examples/common/Esp32S3Uart.cpp) | Set up UART2 and DE, sample the peripheral, preserve timing ranges and report capture/UART errors. ESP32-S3 specific. |
| [RtuRunner.h](../examples/common/RtuRunner.h) / [RtuRunner.cpp](../examples/common/RtuRunner.cpp) | Apply bus admission, TX drain, DE hold, receive framing, deadlines and recovery interlocks using supplied observations. |
| [Codec.h](../include/MotorControlRS/profiles/ess_rs/Codec.h) | Build the model read and check slave, function, length, count, CRC and exception response. |
| [bench_probe.py](../scripts/bench_probe.py) | Correlate console requests, run finite campaigns and save JSONL evidence. |

The application is the only bus owner. The adapter configures UART2 without
installing the IDF UART driver, ISR or driver receive ring. It polls low-level
FIFO, state-machine and error registers directly. No `HardwareSerial`, IDF UART
driver or second owner may use UART2 at the same time. Initialization rejects
an already installed UART2 driver. Use exactly one adapter instance for UART2;
raw adapter instances cannot detect one another automatically.

`Esp32S3Uart::begin(pins, baud)` receives TX, RX, DE and direction polarity
from the application. It does not include the bench board header. The example
passes its explicit `BoardPins.h` preset; another ESP32-S3 application can
supply different valid pins. UART2 remains exclusive; the adapter now supports
the [reviewed host tuples](host_serial.md). It boots at 115200 8N1 and changes
only under a settled owner configuration lease. Alternate pins and active-low
DE have native tests, not bench evidence.

This is a focused bench implementation. Later FieldCore integration must use
or improve FieldCore's existing bus owner; it must not start this adapter beside
that owner. The console and runner remain separate from the installed library.
See the [FieldCore review](reference/10_runner_platform_review.md).

## Timing evidence and limits

The adapter samples before each runner poll using the monotonic microsecond
clock. TX completion comes from physical FIFO/state-machine idle, bounded by
the preceding busy observation and the subsequent idle observation. It does
not equate bytes accepted into a FIFO with completed transmission. DE remains
asserted through the configured hold. In timer mode the capture interrupt
releases DE and retains its timing interval for the later runner poll.

RX capture brackets a byte using previous FIFO-empty evidence, the current
sample and an assumed character-duration range. It returns start/end ranges
with a common uncertainty width. The initial engineering assumptions are 2%
baud tolerance and a stop/publication guard of one bit plus 2 microseconds.
These assumptions require external measurement. The runner uses the full ranges
for framing decisions and reports `TIMING_UNCERTAIN` when a decision straddles
a timing boundary.

The adapter deliberately refuses an RX FIFO batch containing more than one
byte: such a batch has lost the individual inter-byte timing needed here. A
fixed internal queue holds at most 64 captured bytes. Incomplete capture
returns `PENDING`; it does not claim an empty line. Idle/watermark evidence
also uses the RX state machine and pin state. UART receive errors or lost
capture evidence interlock further transactions until explicit recovery.
Silence requires idle observations before and after the FIFO checks, with no
byte consumed in that sample. Recovery requires a fresh sample before it can
provide silence evidence. A snapshot spanning a full minimum character time
fails because the checks could miss an entire character.

The owner services at most 32 console bytes per turn while transactions are
active. Eight PSRAM output lines and bounded pending replies retain terminals
without waiting for USB capacity. Under output pressure, stop has a reserved
admission/reply path and local cancel remains available; other complete lines
can be rejected/dropped without side effects, counted by `drv.input_dropped`.
A stop still needs settled in-flight transport and its own checked outcome.
Diagnostic display loss is distinct from protocol/result loss. See the
[console contract](console.md) for result ownership and repeated-stop behavior.

Polling capture can fail when formatting or scheduler gaps lose timing evidence.
Timer capture is the measured loaded reference; external electrical timing
qualification remains open.

Current application timing choices are:

| Setting | Value and meaning |
| --- | --- |
| RTU inter-byte / inter-frame threshold | 750 / 1750 microseconds above 19200 baud |
| Minimum first-reply gap | 304 microseconds, an explicit ESS bench override (35 bit periods at 115200 baud, rounded upward) |
| DE setup / hold | 20 / 20 microseconds; bench policy pending electrical measurement |
| Bus admission deadline | 100 milliseconds |
| TX deadline | 20 milliseconds |
| Capture-lag deadline | 10 milliseconds |
| Probe response deadline | 200 milliseconds, including final frame gap |
| Absolute admitted probe deadline | 500 milliseconds, including queue/setup/TX/final closure |
| Explicit recovery guard / deadline | 500 milliseconds after physical TX/DE settlement and recovery admission / 2 seconds from admission |
| Observation age expiry | Disabled by default (`0`); optional `ApplicationOptions::observationMaxAgeMs` |

These deadlines are host policy, not measured or vendor-guaranteed upper bounds.
Age expiry can be enabled before startup using the existing application entry
point with the caller's pins and receiver topology:

```cpp
MotorControlRSExample::ApplicationOptions options;
options.observationMaxAgeMs = 5000;
MotorControlRSExample::beginApplication(pins, receiverDisabledDuringTransmit, options);
```

Omitting options keeps expiry disabled. `config`/`status` expose this choice as
`stale_after_ms`; observed ages are still reported. A manual pause alone no
longer expires a checked motion profile or state/identity observation. Missing
evidence, changed generations, recovery and known contradictions still require
renewed evidence. Core `maxAgeUs`/`maximumAgeUs=0` has the same meaning. This does
not prove uninterrupted power or an unchanged physical state, and does not
disable transaction deadlines or required new completion reports. The ordinary
CLI keeps observation-aware preparation; the lower-level
[`PositionCommand`/start-only APIs](ess_position.md#native-commands-and-remembered-intent)
are available to library callers without a separate firmware mode.

The first-reply override is deliberately separate from the recommended 1750
microsecond high-baud inter-frame threshold. Host bus admission and final frame
closure still use 1750 microseconds. `config` exposes `reply_gap_us`, `gap15_us`
and `gap35_us`; record the selected policy with every qualification run. This
bench exception must not silently become the default for other devices or buses.
The selected echo policy is `NONE`; the bench's actual transceiver/receiver
behavior still needs verification. There is no request-identical echo stripping.
The runner's [full contract](runner.md) separates transport framing from checked
profile parsing and acknowledgement from execution.

## Console and correlation

The [console guide](console.md) is the current command reference; `help` and
`help COMMAND` come from the actual dispatch inventory. Reads, motion, settings,
bounded discovery and explicit persistence routes use the public typed APIs.
Nothing scans, moves or writes settings automatically at startup.

Input accepts CR, LF or CRLF. Each line has a 128-byte capacity including NUL.
Overflow, control bytes, extra arguments and invalid numbers reject the whole
line. The output formatter has an 8192-byte bound. Ordinary commands use short
human text; `@1..4294967295` selects correlated JSONL, for example:

```text
@41 version
@42 probe 1
@43 status
@44 health
@45 memory
```

Command correlation IDs and operation IDs are separate. Acceptance is followed
by one asynchronous terminal carrying the original correlation. Duplicate
outstanding IDs fail before admission. The console allows nine ordinary
correlations and one reserved stop; actual bus/frontend admission also depends
on its queue and retained-result capacity. `drv` reports both.

`result ID` inspects without consuming. Ordinary successful human results may
be reclaimed after delivery when a new bus command needs storage. Explicit
machine/API and failed/uncertain results remain retained until review/release.
Eight ordinary frontend records coexist with private monitor/stop records and
separate recovery evidence. A full result store never authorizes overwriting
uncertain execution. Releasing a result is not a motor stop.

`probe ADDRESS` applies to that request; bare `probe` uses the selected address
(default 1, changed locally by `useaddr`). A checked reply establishes a responder;
raw model `0x4EEA` remains unmapped. Failed reads preserve previous valid cache
values and their original qualified observation bounds. `status` and `health`
never refresh those observations.

Transport `FRAME` and parser success are separate results. A bad CRC may have
transport closure but fails checked parsing. A checked Modbus exception is a
device rejection, not a framing fault. Corrupt/mismatched frames or lost capture
require explicit recovery; no automatic retry or late-response reuse occurs.
`timing_valid` describes the adapter evidence; external electrical timing
qualification remains separate.

`recover` cancels old queued work, retains interrupted outcomes, waits for
physical TX/DE settlement and the guard, clears the adapter and establishes
new idle evidence. It sends no motor command, does not establish standstill and
never replays a failed write. A failed recovery keeps the interlock.

## Python campaigns

Python 3.10 or later is required; a real serial port also needs `pyserial`:

```powershell
python -m pip install pyserial
python scripts/bench_probe.py --port COM13 --address 1 --log probe-first.jsonl probe
python scripts/bench_probe.py --port COM13 --address 1 --log probe-stress.jsonl stress --count 100 --interval 0.1
python scripts/bench_probe.py --port COM13 --log probe-watch.jsonl watch --count 60 --interval 1
```

Use a new output filename for each run. The harness refuses to replace existing
evidence. It checks the product/protocol/profile handshake, request IDs, probe
addresses, successful transport/codec/model/length/timing evidence, JSON shape,
line and byte bounds, deadlines, reset/fault messages and uptime regression.
The first error stops a campaign without replay or automatic recovery.

`Console.begin("probe")` returns an admitted handle; local queries and other
admitted handles can interleave. `wait(handle)` retains the terminal by default.
`command("probe")` and `command("recover")` wait and send a separate correlated
`release` acknowledgement. Inspection/cancellation/release take `operation_id`;
they do not reuse the command correlation ID. The host tracks at most eleven
handles: eight ordinary operations, recovery, reserved stop and one local query.
Result quotas remain independent; mismatches poison the session.

`probe` performs one explicit model read. `stress` repeats that same read a
finite number of times, with status/health/memory observations between reads.
`watch` only reads cached host reports and creates no motor bus traffic; age
continues increasing, and presence becomes stale only with age expiry enabled.
`state-health` now
checks raw/decoded alarms, flags, I/O, paired position and speed through the public
state API. Position source/sign/scale and speed sign/units can remain unresolved;
readiness and motion completion are not inferred.

## Memory and verification

The example allocates its bounded `App` once in PSRAM. It includes runner/owner
storage, retained operations, caches, trace and eight output lines of up to
8192 bytes each. The UART sampler and capture working set, driver buffers and
required task stacks remain internal. PSRAM failure reports a boot error; there
is no silent large internal-RAM fallback or per-command core allocation.

Memory sizes depend on the image. Use `memory`, `drv` and `load` for measured
free/minimum/largest blocks and stack headroom; the
[latest boundary report](reports/2026-10-07_position_limits.md) records exact
image measurements. FIFO submission copies into a bounded internal stack array
before its short critical section; that section does not read large PSRAM buffers.
Probe evidence retains up to eight TX and 64 RX bytes with truncation explicit.

Native verification covers runner framing/failure cases, adapter snapshot races,
the actual application loop with a stuck transmitter, checked exceptions and
corrupt replies, console input bounds, cached health semantics, raw result
formatting and fake serial harness failures. These tests share one SDK fake.
Build and automated test results belong in [verification](verification.md).
Hardware runs must separately record firmware, serial tuple, address, raw bytes,
timing bounds, poll gaps, memory watermarks and exact observations.

Remaining qualification includes an independent TX/RX/DE trace; physical final
stop-bit and FIFO-publication bounds; RX state-machine/idle behavior near start
and stop edges; direction setup/hold; echo behavior; responses near framing and
timeout boundaries; and capture behavior under scheduler/USB/interrupt load.
Recorded campaigns cover checked model/settings reads, finite motion and both
stop commands under their named image/load conditions. Independent electrical
timing, mixed physical buses, restart-dependent persistence and FieldCore
integration remain open; those cases are not established by a green build.

The [2026-10-03 bench report](reports/2026-10-03_e2_probe.md) records actual
probes, raw model, timing exception, fault checks, memory and firmware backup.
DE assertion also applies a bounded 20 us adapter guard from the actual GPIO
write before returning; the runner's separate setup wait cannot shorten it.

## Typed identity/configuration subset

Use `read identity [address]`, `read config [address]` or `caps`. The [public read API](ess_reads.md) supplies every preparation/event/decoder; the CLI has no private raw-register sequence. Each admitted frontend read retains one terminal `type:read` record, with original command correlation, a separate operation ID, raw decoded codes and copied per-window TX/RX/closure evidence. `result` is non-consuming and `release` explicit. Eight retained/admitted read/probe operations share the existing frontend quota; a separate recovery record remains available. One 500-ms absolute deadline covers all five configuration windows.

The JSON output capacity is 8192 bytes. Input is 128 bytes including the
terminator, with up to 22 parsed tokens; each turn services at most 32 input
characters and 64 output bytes. Larger operation/cached/console buffers belong to the
PSRAM App; the UART capture and worker stack remain internal. `config` shows
cached observation IDs, targets and binding generations separately from the
active host tuple. Cached identity/configuration evidence survives failed reads
and explicit result release; cached probe health remains separately labelled.

```powershell
python scripts/bench_probe.py --port COM13 --log build/bench/my_typed_reads.jsonl typed-read --kind both
```

This finite scenario checks capabilities, each read once, immutable result inspection, release and local diagnostics. It never retries or recovers automatically. Historical raw configuration, image and resource measurements are in
[the typed-read report](reports/ess_release_05_2026-10-04.md). Configured encoder
4000 and undocumented algorithm 3 do not themselves establish motion readiness.

See [typed state/cache contracts](ess_reads.md#state-observations-and-application-health) and [06 stationary evidence](reports/ess_release_06_2026-10-04.md). `python scripts/bench_probe.py --port COM13 --log build/bench/new-state.jsonl state-health --count 5 --interval 0.1` performs non-changing checks with strict correlation and no retries.


## Typed optional I/O

`io read` exposes four input/two output assignments plus masks;
`io set x0 none` uses the same checked public function-zero setter
as direct code. Fresh explicit I/O and state reads precede a stopped-state
update. The known-unwired application policy qualifies only reviewed passive
transitions, prior-disabled input polarity and unloaded outputs; active external
controls remain gated. No probe/startup/move silently changes assignments.
[The I/O contract](ess_io.md) distinguishes stored readback from acknowledgement,
active settings, wiring, logical state and electrical output behavior.
The finite Python `io read` / `io set FIELD VALUE` commands inspect/release
results and never retry after framing failure; setters require the same current
application prerequisites as direct console callers.
