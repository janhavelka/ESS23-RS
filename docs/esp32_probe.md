# ESP32-S3 standalone motor bench

For interactive use, see the [console guide](console.md): type `help` or `?` for
grouped commands and `help COMMAND` for syntax. Bare commands produce readable
text; `@ID` commands preserve machine JSONL for the existing Python tools.

The [native ESP-IDF consumer](esp_idf_probe.md) now compiles this same application
with small startup/USB adapters. Its read-only smoke and resource evidence are
in [prompt25's report](reports/ess_release_25_2026-10-05.md).
[Prompt26](reports/ess_release_26_2026-10-05.md) records matched software capture,
loaded console/owner, finite motion/stop and reversible settings qualification;
electrical/independent physical evidence remains separate. The build commands
below select Arduino.

The [fresh prompt24 audit](reports/ess_release_24_audit_2026-10-05.md) verifies
strict raw probe bytes and interrupted/failed session ownership, plus322 new
COM13 frames and repeated finite move/stop restoration on the unchanged image.

Use [finite named Python scenarios](bench_scenarios.md) for repeatable checks.
Default quick is read-only; selected motion/settings retain one connection
through stop, standstill and restoration. [Prompt24 evidence](reports/ess_release_24_2026-10-05.md)
records strict parser/failure coverage and461 new checked frames on the unchanged
ordinary timer image, retaining every failed experiment.

Prompt23 adds callback-aware help/capabilities, callable typed group aliases, local `useaddr`, fault-independent monitor control and explicit `motion-profile forget`. [The final command/result handoff](ess_api_cli_coverage.md) and [current verification](reports/ess_release_23_2026-10-05.md) supersede earlier probe-only capability descriptions below. Historical measurements remain dated evidence, not current exclusions.

The [fresh23 audit](reports/ess_release_23_audit_2026-10-05.md) adds local
`wiring` declarations after rebinding and preserves uncertain restoration
evidence/reservations through explicit read-only reconciliation. It fixes UART
configuration ownership through terminal harvest and distinguishes unresolved
native semantics from unsupported operations and missing prerequisites.

Prompt22 adds [bounded ESS discovery](ess_discovery.md), minimal public probes and retained
scan evidence with explicit recovery/restoration. [Verification](reports/ess_release_22_2026-10-05.md) records
COM13 address/tuple scans, budget limits and unchanged motor settings/state.

Prompt21 adds [explicit save/factory restore](ess_persistence.md) and bounded
read-only persistence snapshots through the existing commissioning owner.
[Final-image checks](reports/ess_release_21_2026-10-04.md) pass with unchanged
settings and no writes/restart. Actual durability and factory restoration remain
unqualified without a motor-restart and recommissioning procedure.

The ordinary firmware exposes [actions and motion checks](functional_bench.md)
and [runtime debug observation](traffic.md) with `debug off|raw|decoded`. No separate functional image or
analyzer admission flag exists. The user owns the declared wiring; firmware
checks command prerequisites and software transport evidence.

Prompt19 adds [host-only serial selection](host_serial.md), `host caps`,
`host set RATE FORMAT` and `host restore`, with retained tuple/generation
diagnostics. The finite Python `host-check --baud 9600 --fmt 8N1` exercises an
expected mismatch, explicit recovery and original restoration without changing
the drive. The [fresh audit](reports/ess_release_19_audit_2026-10-04.md) records
all sixteen host setups and restored 115200 8N1 probes, with reproduced malformed
mismatch traffic left unresolved. A strict host-check can fail and restoration
can be refused until an explicit diagnostic recovery; no automatic replay occurs.
Other motor tuples stay unqualified. Timing values below describe the default
tuple unless stated; 8N2 uses a two-stop-bit publication guard.

The [fresh prompts 17/18 audit](reports/ess_release_17_18_audit_2026-10-04.md) fixes
settings freshness, copied provenance, partial-refresh invalidation and strict
console evidence validation. All 45 native suites, installed consumption and
four firmware builds pass. The final COM13 image passes 104 frames and restores
input filter `2?3?2` and lock delay `200?201?200`; physical effects remain unqualified.

Prompt18 adds [typed tuning](ess_tuning.md) and a bounded Python `tuning GROUP
read|set` route. [Implementation-image evidence](reports/ess_release_18_2026-10-04.md)
records twenty native reads and input-filter stored restoration. Physical tuning
effects and general standalone write qualification remain explicit gates.

The [fresh16/17 audit](reports/ess_release_16_17_audit_2026-10-04.md) records the
corrected closure-certainty/help image, all48 indexed reads and exact lock-delay
stored restoration in114frames. Physical effects and unresolved source semantics
remain separate from those checked register paths.

Prompt17 adds [control settings](ess_control_settings.md) and the Python `control read|set` scenario. [Implementation-image evidence](reports/ess_release_17_2026-10-04.md) records66frames, all-field readback and delay200-to201-to200 restoration; mode/encoder/current effects and electrical FC06 source remain unqualified.


Prompt14 adds [homing API/console routes](ess_homing.md) and `home methods`. Real execution remains behind method/native/auxiliary/reference prerequisites; the current-image checks are [read-only and zero-TX evidence](reports/ess_release_14_2026-10-04.md).

Prompt11 adds [finite serial velocity](ess_velocity.md) and a bounded Python
velocity scenario; production action/ramp/sign gates remain closed. See the
[current handoff](reports/ess_release_11_2026-10-04.md) for read-only regression
and zero-TX checks; no physical velocity or stop was qualified.
The [fresh audit](reports/ess_release_11_audit_2026-10-04.md) records the corrected
host harness and updated help image, with repeated read-only checks.

Prompt10 adds shared relative/absolute/wrapped-angle console and Python routes
and zero-only device position clear; see [finite positioning](ess_position.md).
Their original [historical evidence](reports/ess_release_10_2026-10-04.md)
records read-only regression, pure step/degree/radian equivalence and zero-TX gates.

Prompt 08 adds [typed actions and priority stop](ess_actions.md) to this console.
The ordinary application uses the configured echo/receive contract for checked
acknowledgements; observed completion remains separate. No analyzer admission
gate or test-mode flag is required.

Prompt 07 adds [host axis configuration and pure target previews](axis_preparation.md)
through the same installed public API; these commands generate no motor traffic.

Prompts 05â€“06 implement bounded [typed identity/configuration/state reads](ess_reads.md), common/profile routes, passive per-block status/health and finite opt-in observation polling. The [linked inventory](reference/ess_rs_operations.json) keeps native/hardware evidence and read/write/action obligations separate. Model/firmware compatibility, units, readiness and motion remain unqualified; these reads perform no writes.

This example connects the ESS codecs, application BusOwner, standalone runner and a dedicated
ESP32-S3 UART adapter. It supports documented reads and explicit typed motor operations through the regular library API. It provides a small console and finite Python campaigns
for developing and checking that path before adding motion.

The adapter records bounded timing observations. It does not claim exact UART
edge timestamps or completed electrical qualification. `timing_qualified` stays
false until an independent TX/RX/DE trace verifies the capture assumptions.
A successful read alone cannot establish those assumptions, drive readiness,
or manufacturer/model identity.

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
owner service gap. Load changes require an idle owner and settled DE. `reset`
and an explicit load change start a fresh fixture measurement window without
clearing faults. `reset` keeps the selected workload and last probe result.

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

The [current capture review](reports/ess_release_04_2026-10-04.md) retains the
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
.\scripts\pio.cmd run -e bench_s3_probe
```

The environment is in [platformio.ini](../platformio.ini). It extends the bench
units example's pinned pioarduino platform `55.03.311`, Arduino framework,
ESP32-S3 board definition, 16 MB flash, OPI PSRAM and USB CDC settings. This
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
.\scripts\pio.cmd run -e bench_s3_probe -t upload --upload-port COM13
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

The owner services at most 32 console bytes while TX/RX is active, including
iterations deferred by the load fixture. Output never waits for USB capacity.
Eight PSRAM output lines plus one pending Console line preserve terminal output.
When that pending slot is blocked, only local `cancel` is executed; other complete
input lines are dropped without side effects and counted in `drv.input_dropped`.
A cancel acknowledgement may also be dropped, but its original terminal result
remains reserved. This keeps local cancellation admissible under USB pressure;
no physical stop is implemented. New bus/recovery admissions wait for output
capacity. The host fails closed after missing/framing records and never replays.
Polling capture can still fail explicitly when formatting or scheduler gaps
lose wire evidence. Timer capture is the measured loaded reference; external
timing qualification remains open.

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
| Presence freshness threshold | 5000 milliseconds from qualified frame-closure earliest bound |

These deadlines are host policy, not measured or vendor-guaranteed upper bounds.
The first-reply override is deliberately separate from the recommended 1750
microsecond high-baud inter-frame threshold. Host bus admission and final frame
closure still use 1750 microseconds. `config` exposes `reply_gap_us`, `gap15_us`
and `gap35_us`; record the selected policy with every qualification run. This
bench exception must not silently become the default for other devices or buses.
The selected echo policy is `NONE`; the bench's actual transceiver/receiver
behavior still needs verification. There is no request-identical echo stripping.
The runner's [full contract](runner.md) separates transport framing from checked
profile parsing and acknowledgement from execution.

## Console

Input accepts CR, LF or CRLF. The fixed input capacity is 96 bytes including
the terminating NUL. Overflow, non-ASCII/control input, extra arguments, numeric
overflow and invalid addresses reject the entire command before bus admission.
There are no raw writes, motion operations or automatic scans in this build.

| Command | Effect |
| --- | --- |
| `help [command]` | Show callable commands or one command's syntax and effects. |
| `version` / `ver` | Report product, profile, library version and console protocol version. |
| `config` / `settings` | Show host tuple, address, timing deadline and qualification state. Complete typed configuration is separately cached with its original target/generation; it never replaces the observed active host tuple. |
| `probe [address]` / `ping [address]` | Read ESS model register `0x0000`, one word: eight-byte FC03 request, seven-byte normal reply or five-byte exception. |
| `read state [address]` / `profile ess_rs state [address]` / `health check [address]` | Three reviewed non-consuming windows through `prepareState`/`getStateBlock`. |
| `monitor [off\|<interval_ms> <count>]` | Passive query, cancellation or finite100..60000ms/1..1000 attempts; disabled at startup. |
| `status` | Cached transport, model and per-block observations with separate attempt/success/age. |
| `health` | Assess cached communication freshness. State blocks show raw/decoded alarms and flags with independent ages; drive readiness remains unknown. |
| `capture-read [address]` | Fixed non-consuming FC03 read of `0x0130/16` settings words; eight-byte request, 37-byte normal reply or five-byte exception. Timing fixture only; no model-cache update or interpreted speed values. |
| `stats` | Show local runner and capture counters, including maximum observed poll gap. |
| `reset` / `stats reset` | Clear local counters only. Preserve the result and recovery interlock. |
| `recover` | Explicit host-only RX/error recovery after the configured guard; no motor command. |
| `drv` | Phase, queued/reserved/retained counts, capacities, absolute deadline, capture mode, input/output dispositions and timing bounds. |
| `result [operation-id]` | Non-consuming pending or terminal view; omitted ID selects latest admission. |
| `cancel [operation-id]` | Local cancellation of selected/latest probe; physical TX settles; no motor stop. |
| `release <operation-id>` | Explicit release of an already delivered retained terminal; stale IDs fail. |
| `memory` | Report free/minimum/largest internal and PSRAM blocks and task-stack free high-water mark, in bytes. |

An explicit probe address applies to that request. A later bare `probe` still
uses the default address 1. Cached status and health label the address of their
latest transmitted attempt with `probe_address`; it is null before an attempt
is available. `model_address` labels the last checked successful `raw_model`
and its age, separately from the latest attempt/error. Failed reads preserve
that value and its original observation bounds. Neither command performs a fresh read.
`ping` uses the canonical command name `probe` in both admission and terminal
records. Synchronous `reset` and `stats reset` return `result:"done"`.
Recovery emits an accepted reply and one separate `type:"recovery"` terminal.
It cancels all old queued work at admission, retains interrupted results,
waits for physical TX/DE and the guard, explicitly clears the adapter, then
lets BusOwner discard stale traffic and establish fresh idle evidence.
Recovery failure never resumes old work; recovery has its own retained slot.

Protocol 2 separates `@id` command correlation from monotonically increasing
`operation_id` (no wrap/reuse within an App lifetime). Four ordinary queued
requests plus one active share eight reserved/retained result slots; a ninth
correlation and separate result belong to recovery. One urgent pending/result
reservation remains unavailable to ordinary probes; no stop handler exposes it.
Every admitted probe/recovery produces one automatic terminal. `result` does
not consume or regenerate that event; `release` explicitly frees storage.
Unread results never expire or get overwritten. Duplicate outstanding command
IDs fail before admission; callers wait for the terminal before reusing IDs.

Cache age uses immutable qualified closure bounds, separately from terminal
delivery and recovery settlement. Output pressure does not defer harvesting
completed observations. Unsent cancellation does not change a cached
observation. Failed reads retain the last valid model and its age; age is null
before a successful observation with qualified closure bounds. Driver
`model_operation_id` attributes observation and delivery timestamps to that
successful request independently of latest admission/attempt IDs.
Successful recovery invalidates confidence once, independently of output, and
an unread recovery result does not erase newer observations.

Ordinary commands such as `probe` produce readable multiline replies. Automation
prefixes a decimal correlation ID from 1 through 4294967295 to retain one JSON
object per line, including that operation's asynchronous terminal reply:

```text
@41 version
@42 probe 1
@43 status
@44 health
@45 memory
```

For example, admission produces:

```json
{"type":"reply","profile":"ess_rs","id":42,"command":"probe","ok":true,"result":"accepted","address":1,"operation_id":1}
```

An accepted probe later emits one `type:"probe"` terminal record with the same
ID. Keep admission and completion separate. Terminal fields include transport
reason, codec result, raw exception detail, raw model word, bounded TX/RX hex,
elapsed time and timing uncertainty. `codec:"NOT_CHECKED"` means transport did
not reach checked parsing. The raw model is null on failure. A matching parsed
reply establishes `identity:"responder_only"`, not a confirmed ESS model.

`timing_valid` describes the completed frame under the adapter's stated bounds.
`timing_qualified:false` in cached status/config describes the outstanding
external measurement. Those statements are different. A bad CRC can have a
transport result of `FRAME` and a codec result of `CRC_ERROR`.

There is one active bus transaction and bounded queued probes, with no automatic retry. Transport/capture faults
and corrupt or mismatched frames require explicit host recovery before another
request. A fully checked Modbus exception is a completed device rejection: its
code remains visible, but it does not require host recovery. Recovery resets
the host capture path and invalidates cached presence confidence; its 500 ms
guard cannot prove a still-processing drive will never send a late reply.

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
`watch` only reads cached host reports and creates no motor bus traffic; presence
will become stale if no checked communication refreshes it. `state-health` now
checks raw/decoded alarms, flags, I/O, paired position and speed through the public
state API. Position source/sign/scale and speed sign/units can remain unresolved;
readiness and motion completion are not inferred.

## Memory and verification

The example allocates its `App` once in PSRAM during startup. It contains the
32-byte TX buffer, 64-byte RX buffer, 128-entry trace, runner, owner, five pending
slots, nine bus result slots, eight ordinary frontend records, one private
polling and one reserved stop record, state caches, console buffers
and eight4609-byte output lines. The current prompt10 image uses81184 bytes for
the ESP32-S3 App, including
all that storage. Driver capture state (1704 bytes), load fixture (4816 bytes,
including its 4096-byte stack), SDK buffers and owner stack remain internal.
Failure to allocate PSRAM reports a boot error;
there is no silent large internal-RAM fallback. No per-command application
allocation is added by the runner, codecs or console.

The UART sampler and 64-entry capture working set remain internal (1704 bytes
for the Esp32S3Uart object on ESP32-S3, including the 1536-byte ring).
FIFO submission copies at most 64 bytes to an internal stack array before its
short critical section. Larger PSRAM storage is never read from that section.
Task stacks remain under the framework's allocation rules. Memory snapshots
include largest available blocks as well as free/minimum totals so fragmented
heaps are visible. Console JSON output is capped at8192 bytes; retained hex is capped
at eight TX and 64 RX bytes with an explicit truncation flag.

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
The current bench has checked seven-byte model and 37-byte fixed-window replies
under the recorded load scenarios. Other frame sizes/windows, independent
electrical timing, shared-bus, motion, stop, persistence and FieldCore integration
qualification remain open.

The [2026-10-03 bench report](reports/2026-10-03_e2_probe.md) records actual
probes, raw model, timing exception, fault checks, memory and firmware backup.
DE assertion also applies a bounded 20 us adapter guard from the actual GPIO
write before returning; the runner's separate setup wait cannot shorten it.

## Typed identity/configuration subset

Use `read identity [address]`, `read config [address]`, `profile ess_rs identity [address]`, `profile ess_rs config [address]`, `caps` or `profile ess_rs caps`. The [public read API](ess_reads.md) supplies every preparation/event/decoder; the CLI has no private raw-register sequence. Each admitted frontend read retains one terminal `type:read` record, with original command correlation, a separate operation ID, raw decoded codes and copied per-window TX/RX/closure evidence. `result` is non-consuming and `release` explicit. Eight retained/admitted read/probe operations share the existing frontend quota; a separate recovery record remains available. One 500-ms absolute deadline covers all five configuration windows.

The JSON output capacity is8192 bytes. The tested full-width move record is4287
bytes; input is128 bytes including terminator/20 tokens,32 input characters and64
output bytes per loop. Larger operation/cached/console buffers belong to the
PSRAM App; the UART capture and worker stack remain internal. `config` shows
cached observation IDs, targets and binding generations separately from the
active host tuple. Cached identity/configuration evidence survives failed reads
and explicit result release; cached probe health remains separately labelled.

```powershell
python scripts/bench_probe.py --port COM13 --log build/bench/my_typed_reads.jsonl typed-read --kind both
```

This finite scenario checks capabilities, each read once, immutable result inspection, release and local diagnostics. It never retries or recovers automatically. Actual raw configuration, current image and resource/latency measurements are in [prompt05](reports/ess_release_05_2026-10-04.md); configured encoder4000 and unknown algorithm3 do not establish motion readiness.

See [typed state/cache contracts](ess_reads.md#state-observations-and-application-health) and [06 stationary evidence](reports/ess_release_06_2026-10-04.md). `python scripts/bench_probe.py --port COM13 --log build/bench/new-state.jsonl state-health --count 5 --interval 0.1` performs non-changing checks with strict correlation and no retries.


## Typed optional I/O

`profile ess_rs io read` exposes four input/two output assignments plus masks;
`profile ess_rs io set x0 none` uses the same checked public function-zero setter
as direct code. Fresh explicit I/O and state reads precede a stopped-state
update. The known-unwired application policy qualifies only reviewed passive
transitions, prior-disabled input polarity and unloaded outputs; active external
controls remain gated. No probe/startup/move silently changes assignments.
[The I/O contract](ess_io.md) distinguishes stored readback from acknowledgement,
active settings, wiring, logical state and electrical output behavior.
The finite Python `io read` / `io set FIELD VALUE` commands inspect/release
results and never retry after framing failure; setters require the same current
application prerequisites as direct console callers.
