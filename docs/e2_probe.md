# E2 read-only probe bench

This example connects the ESS codecs, standalone runner and a dedicated
ESP32-S3 UART adapter. Its only motor command is the documented non-changing
model-register read. It provides a small console and finite Python campaigns
for developing and checking that path before adding motion.

The adapter records bounded timing observations. It does not claim exact UART
edge timestamps or completed electrical qualification. `timing_qualified` stays
false until an independent TX/RX/DE trace verifies the capture assumptions.
A successful read alone cannot establish those assumptions, drive readiness,
or manufacturer/model identity.

## Load and sleeping-owner capture

`e2_s3_probe` retains owner polling. Two optional build environments add the
same small load fixture: `e2_s3_load_poll` for the baseline and
`e2_s3_load_timer` for background capture. The latter samples UART2 with a
20-microsecond GPTimer alarm and releases DE after observed TX idle plus the
configured hold. A sleeping owner later receives atomic TX/release evidence
and the buffered RX timing records. Both modes share E2Uart, the runner,
ESS codecs, console and Python harness.

```powershell
.\scripts\pio.cmd run -e e2_s3_load_timer -t upload --upload-port COM13
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

[E2Load](../examples/probe_cli/E2Load.h) owns one priority-2 competing task on
the Arduino owner's core, a fixed 4096-byte internal stack and a USB output
mutex. It has application lifetime. Diagnostic lines start with `# load `;
the configured size is payload, with framing added. JSON and diagnostic lines
are serialized. Each output begins on a fresh line to separate earlier partial
writes. The load build allocates a 1024-byte internal USB TX ring once before
USB startup, allowing the largest diagnostic line to fit. Driver enqueue waits
are bounded; disconnected/back-pressured output can still be lost or partial.
The harness stops on command/framing failure and never replays a probe.

`console_lines` counts full lines accepted by the USB write call, not confirmed
host receipt; `console_dropped` counts skipped/short fixture writes. A load run
must check these counters before claiming it exercised console traffic.
The Python campaign fails if requested work had no competing task iterations
or requested console output produced no complete lines.
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

The [capture/load audit](reports/2026-10-03_capture_load.md) records the actual
tested envelope. The pinned SDK does not enable GPTimer cache-safe interrupts;
flash writes/cache-off operation remain unqualified. ISR state and stacks stay
internal; the existing owner buffers, trace and console remain in PSRAM.

## Build and board

From the repository root:

```powershell
.\scripts\pio.cmd run -e e2_s3_probe
```

The environment is in [platformio.ini](../platformio.ini). It extends the E2
units example's pinned pioarduino platform `55.03.311`, Arduino framework,
ESP32-S3 board definition, 16 MB flash, OPI PSRAM and USB CDC settings. This
application uses the ESP-IDF UART setup and low-level register helpers shipped
with that framework. The reusable MotorControlRS library has no such dependency.

| Connection | Example setting |
| --- | --- |
| Physical board | User-confirmed E2, revision 2.0.0 |
| UART owner | Exclusive UART2 adapter |
| RS485 TX / RX | GPIO47 / GPIO48 |
| DE/RE | GPIO21, high to transmit, low to receive |
| Motor bus | 115200 baud, eight data bits, no parity, one stop bit |
| USB console | 115200 baud setting, separate from motor bus |
| Initial probe address | 1; an explicit probe argument accepts 1 through 247 |

The serial tuple is the documented ESS default used as a commissioning
candidate. It is not readback of the connected motor. Board provenance and
the previously running CO2control firmware are recorded in
[the board review](reference/04_co2control_platform.md) and
[bench notes](hardware_bench.md).

After inspecting the current port and retaining the existing firmware/build
information needed to restore it, the example can be uploaded explicitly:

```powershell
.\scripts\pio.cmd run -e e2_s3_probe -t upload --upload-port COM13
.\scripts\pio.cmd device monitor -p COM13 -b 115200
```

COM13 is the reported bench port; inspect its current identity before use.
An upload replaces the board's current application. Keep one owner of the USB
port and close the monitor before starting Python. Startup performs no motor
query, scan, configuration write or motion command.

## Code and ownership

| File | Responsibility |
| --- | --- |
| [main.cpp](../examples/probe_cli/main.cpp) | Own the application buffers, runner, UART, console, retained probe result, memory snapshots and recovery policy. |
| [ProbeConsole.h](../examples/probe_cli/ProbeConsole.h) / [ProbeConsole.cpp](../examples/probe_cli/ProbeConsole.cpp) | Parse bounded lines, validate arguments, dispatch the supported commands and format JSON records. Platform neutral. |
| [E2Uart.h](../examples/common/E2Uart.h) / [E2Uart.cpp](../examples/common/E2Uart.cpp) | Set up UART2 and DE, sample the peripheral, preserve timing ranges and report capture/UART errors. ESP32-S3 specific. |
| [RtuRunner.h](../examples/common/RtuRunner.h) / [RtuRunner.cpp](../examples/common/RtuRunner.cpp) | Apply bus admission, TX drain, DE hold, receive framing, deadlines and recovery interlocks using supplied observations. |
| [Codec.h](../include/MotorControlRS/profiles/ess_rs/Codec.h) | Build the model read and check slave, function, length, count, CRC and exception response. |
| [bench_probe.py](../scripts/bench_probe.py) | Correlate console requests, run finite campaigns and save JSONL evidence. |

The application is the only bus owner. The adapter configures UART2 without
installing the IDF UART driver, ISR or driver receive ring. It polls low-level
FIFO, state-machine and error registers directly. No `HardwareSerial`, IDF UART
driver or second owner may use UART2 at the same time. Initialization rejects
an already installed UART2 driver.

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

The basic polling application avoids console parsing, JSON formatting, USB output
and sleeps while a probe is active. The optional load fixture deliberately adds
competing task/USB activity and owner sleeps, as described below. Commands received then wait in the USB input path and
are processed after its terminal result. A TX or direction fault can leave DE
asserted or uncertain; the application still publishes the failure and serves
cached commands while the runner continues cleanup. `transmit_enabled` reports
that state, and new probes/recovery remain interlocked until cleanup permits
them. This keeps the first bench implementation small;
it is not the future priority-stop or fully concurrent console design. A long
scheduler interruption may cause a correctly reported capture failure. It must
not be hidden by a guessed receive timestamp or automatic retry.

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
| Explicit recovery guard | 500 milliseconds after the previous result |
| Presence freshness threshold | 5000 milliseconds after the completed probe |

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
| `config` / `settings` | Show host tuple, address, timing deadline and qualification state. Device settings remain unknown. |
| `probe [address]` / `ping [address]` | Read ESS model register `0x0000`, one word: eight-byte FC03 request, seven-byte normal reply or five-byte exception. |
| `status` | Show cached transport/codec result, raw exception detail, DE state, model word and age. |
| `health` | Assess cached communication freshness. Drive readiness, alarms and motion state remain unknown. |
| `stats` | Show local runner and capture counters, including maximum observed poll gap. |
| `reset` / `stats reset` | Clear local counters only. Preserve the result and recovery interlock. |
| `recover` | Explicit host-only RX/error recovery after the configured guard; no motor command. |
| `memory` | Report free/minimum/largest internal and PSRAM blocks and task-stack free high-water mark, in bytes. |

An explicit probe address applies to that request. A later bare `probe` still
uses the default address 1. Cached status and health label the address of their
retained observation with `probe_address`; it is null before an observation is
available. Neither command performs a fresh read.
`ping` uses the canonical command name `probe` in both admission and terminal
records. Successful synchronous `reset`, `stats reset` and `recover` return
`result:"done"`; they do not emit a later completion record.

Every reply is one JSON object per line. A human can enter `probe`; automation
prefixes a decimal correlation ID from 1 through 4294967295:

```text
@41 version
@42 probe 1
@43 status
@44 health
@45 memory
```

For example, admission produces:

```json
{"type":"reply","profile":"ess_rs","id":42,"command":"probe","ok":true,"result":"accepted","address":1}
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

There is one outstanding probe and no automatic retry. Transport/capture faults
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

`probe` performs one explicit model read. `stress` repeats that same read a
finite number of times, with status/health/memory observations between reads.
`watch` only reads cached host reports and creates no motor bus traffic; presence
will become stale if no probe refreshes it. These tools do not yet observe motor
alarms, position, velocity or completion because that read-state API is a later
implementation block.

## Memory and verification

The example allocates its `App` once in PSRAM during startup. It contains the
32-byte TX buffer, 64-byte RX buffer, 128-entry trace, runner, console buffers
and retained application state. Failure to allocate PSRAM reports a boot error;
there is no silent large internal-RAM fallback. No per-command application
allocation is added by the runner, codecs or console.

The UART sampler and 64-entry capture working set remain internal (1680 bytes
for the E2Uart object on ESP32-S3, including the 1536-byte ring).
FIFO submission copies at most 64 bytes to an internal stack array before its
short critical section. Larger PSRAM storage is never read from that section.
Task stacks remain under the framework's allocation rules. Memory snapshots
include largest available blocks as well as free/minimum totals so fragmented
heaps are visible. Console output is capped at 1024 bytes; retained hex is capped
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
The current single-read bench path does not establish shared-bus, long-frame,
motion, stop, persistence or FieldCore integration qualification.

The [2026-10-03 bench report](reports/2026-10-03_e2_probe.md) records actual
probes, raw model, timing exception, fault checks, memory and firmware backup.
DE assertion also applies a bounded 20 us adapter guard from the actual GPIO
write before returning; the runner's separate setup wait cannot shorten it.
