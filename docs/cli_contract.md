# ESS23-RS example CLI contract

This defines the future standalone bring-up console. No commands are
implemented yet. It keeps familiar sibling command names while making host
configuration, fresh reads, cached status and motor side effects explicit.
See the [architecture](architecture.md) for codec and application ownership.

## Command ownership and execution

The console parses bounded input and submits work to its example application.
One transport owner drives the bus. A command handler must not start a second
UART transaction while automatic polling or another command is active. Keep
one foreground workflow initially; return an explicit busy result for another
workflow, while cached diagnostics remain available. A stop request requires
defined priority/cancellation handling, not an indefinite wait behind a scan.

Use a SHZK-style command inventory for dispatch, help and documentation. The
native ESP-IDF application implements its own platform I/O and declares its
supported subset; it must not include the Arduino console or fake missing
commands. Shared commands have the same side effects and result meanings.

Reject truncated lines, excess tokens, trailing junk, negative unsigned
values, numeric overflow and unsupported units before submitting work. Use
fixed command/output buffers. Long scans, polling and motor workflows advance
cooperatively with deadlines so the console remains responsive.

## Initial bring-up surface

This table is a design contract for the first implemented console, not a
claim of parity with every historical sibling command.

| Command | Behavior | Motor bus traffic |
| --- | --- | --- |
| `help` | List implemented commands, syntax and effects | None |
| `version` / `ver` | Library, firmware/build identity and platform | None |
| `config` / `settings` | Host target/serial/word-order settings and cached device configuration, each labelled | None |
| `host [baud <rate> \| fmt <format>]` | Show/change host UART settings when idle; invalidate target confidence; accepts only formats supported by the adapter | None |
| `host wordorder <high-low\|low-high>` | Select an explicit host decoding assumption when idle; label it assumed and invalidate decoded cache | None |
| `useaddr <1-247>` | Select host target when idle; invalidate target-specific cache and pending assumptions | None |
| `ping` / `probe` | Read identity at selected address; show raw model/firmware and compatibility confidence | Reads |
| `read identity` | Fresh identity observation | Reads |
| `read status` | Fresh motor flags/alarm/position/speed observation with per-block validity | Reads |
| `read config` | Read supported communication/word-order configuration without changing it | Reads |
| `readreg <register> [count]` | Diagnostic FC03 read within documented readable windows and 16-register limit | Reads |
| `status` | Cached host/transport/motor/workflow state, ages and latest attempt result | None |
| `health` | Cached communication/presence/freshness and motor readiness assessment, with reasons | None |
| `health check` | Bounded identity/status refresh, then health assessment | Reads |
| `stats` | Transport/application counters | None |
| `stats reset` / `reset` | Clear local counters only | None |
| `drv` | Transport phase, deadlines, queue/buffer state and host capabilities | None |
| `diagnose` / `diag` | Bounded read-only checks plus config, health and counters | Reads |
| `auto on [ms]` / `auto off` | Schedule/stop periodic telemetry reads; report accepted cadence | Reads while enabled |
| `cancel` | Cancel the foreground workflow; preserve uncertainty for already transmitted commands | No new command; settle existing bus activity |
| `verbose on\|off` | Enable/disable bounded TX/RX tracing | None beyond scheduled work |
| `recover` | Reinitialize host transport when idle; invalidate communication confidence | None; no motor reset |
| `flush` | Explicit idle-only host RX discard, recorded in diagnostics | None |

Changing target or host serial settings is a local operation and never writes
the device. Either change invalidates selected-device identity, word-order
confidence and readiness. Word order comes from a successful configuration
read or an explicitly documented host assumption; show which. Changing word
order invalidates decoded values; do not decode historical words under a new
assumption. Retain uncertain commands and historical observations under their
original target, serial tuple and decoding context.

Failed refreshes retain last valid data and increase its age. Track validity
and last attempt/success separately for independently refreshed blocks; a
successful identity read must not make an old position or alarm fresh.
Successful identity at an unknown model code proves a responder exists, not
that movement is supported.

`status` and plain `health` are deliberately passive. Some sibling Arduino
commands perform live reads under those names; their behavior is not uniform
across platforms. Explicit `read ...` and `health check` keep this console
predictable and align passive snapshots with FieldCore. `reset` has one fixed
meaning on both platforms: statistics only.

`auto off` stops future polling and lets an in-flight read finish within its
deadline; it does not stop motor motion. `cancel` ends the foreground workflow,
not physical movement. The transport settles queued TX, direction control
and receive framing before allowing another transfer. `recover` neither
enables the motor nor clears alarms nor resends the last command. Do not erase
an uncertain motion outcome when clearing counters or reinitializing the UART.

## Later commissioning and motion surface

Add these commands only alongside implemented and verified operation
contracts. Do not register placeholder handlers in the first console.

| Command family | Required distinction |
| --- | --- |
| `scan`, `baudscan` | Bounded, explicit address/tuple ranges; read-only discovery; no automatic writes or broad scan at boot |
| `setaddr`, `setbaud`, `setformat` | Device writes, distinct from `useaddr`/`host`; show active versus pending values and documented power-cycle requirements |
| `motor enable`, `motor release` | Physical drive state change; unrelated to polling/module inclusion |
| `motor move ...`, `motor speed ...`, `motor home ...` | Explicit signed target/mode/units and resolved parameter contracts; separate parameter setup, start and completion |
| `motor stop`, `motor estop` | Explicit device commands with response/result tracking and bounded scheduling |
| `motor clear-alarm` | Request documented alarm clear; not a promise every alarm is resettable |
| `motor clear-position` | Explicit coordinate change which invalidates application position assumptions |
| `motor save-parameters` | Device nonvolatile write; stopped-state precondition and post-operation verification |
| `motor restore-factory` | Explicit device restore; may invalidate communication/configuration assumptions |

Keep write commands under explicit names. No generic `setup` may silently
reconfigure, enable or move a motor. Avoid bare `save`, `factory` or device
`reset`, which mean different things in existing consoles. If host persistence
is later added, use `host save`, `host load`, `host defaults` and never store a
motion command for replay.

A generic `writereg` escape hatch is deferred. If later needed for bench work,
it must obey the same writable-register, workflow and command-policy rules as
typed operations. Unknown model compatibility does not authorize guessed
writes. Motion arming/confirmation UX and test limits are application choices
to settle with the first real motion workflow; they are not codec features.

## Status display and result contract

Each report names the selected target and distinguishes these fields:

- Host configuration: address, baud/format, word-order value and its source.
- Transport: idle/transmitting/waiting/fault state and last transport error.
- Observation: never observed/fresh/stale, time of last attempt/success and age
  for each independently refreshed block, including its target/serial context.
- Motor: raw status/alarm, decoded running/enabled/released/homed/arrival/limit
  flags, raw position/speed and only those physical units already established.
- Workflow: submitted operation, transmission/acknowledgement state, observed
  completion or failure, and whether physical execution remains unknown.
- Health: communication condition separately from device alarm/readiness,
  with the reason and configured freshness threshold.

Do not print zero as a fresh position before a read. Do not replace motor data
with zero on a failed frame. Do not equate successful UART transmission,
valid write echo and completed movement. Diagnostic reads may refresh motor
state but cannot retroactively prove a lost non-idempotent command did not
execute. Known motor flags remain qualified by their observation age.

Print a concise admission result and one terminal result per foreground
operation. Carry a local operation ID if execution spans console iterations;
this is local correlation, not a Modbus transaction identifier. Report codec
code/detail, raw exception, transport outcome and execution uncertainty without
requiring a user to inspect raw frames. Printing status does not consume a
pending command result. Routine polling output must be rate-limited.

## FieldCore console mapping

FieldCore uses `rs485 status`, `rs485 timing`, `rs485 trace`, `rs485 recover`,
`rs485 result <request_id>` and device-specific `probe`/`read`/`read last`.
Its `rs485 discover` probes configured instances. Future ESS commands should
use its typed routing and retained result mechanism; the standalone console
does not need to copy FieldCore queues, settings service or CLI registry.

FieldCore currently lacks motor command payloads and exact integer motor
results. Adding `ess23 ...` there requires a separate integration change;
creating this library does not register a new product device or CLI command.

## Verification when implemented

Exercise command help/dispatch parity, bounds and numeric parsing, host-only
commands producing no device write, read-only diagnostics, cached reports
producing no transaction, one active bus transaction, responsive cancellation,
stop scheduling and truthful result phases. Test that failed reads preserve
cache, target changes invalidate it, and `reset`/`recover`/startup cannot clear
alarms or replay motion. Arduino and native IDF shared commands must satisfy
the same contract even when their supported command counts differ.
