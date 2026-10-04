# MotorControl-RS standalone CLI contract

Prompts 05–06 implement bounded [typed identity/configuration/state reads](ess_reads.md), common/profile routes, passive per-block status/health and finite opt-in observation polling. The [linked inventory](reference/ess_rs_operations.json) keeps native/hardware evidence and read/write/action obligations separate. Model/firmware compatibility, units, readiness and motion remain unqualified; these reads perform no writes.

This defines the planned standalone console for the general `MotorControlRS`
library and its family profiles, beginning with `MotorControlRS::ESS_RS`.
The [probe/load console](esp32_probe.md) implements the current read-only subset;
the full command surface below remains a contract for later implementation.
The current protocol-2 subset also implements `drv`, non-consuming `result`,
explicit `release`, local `cancel [operation-id]` and asynchronous host recovery.
Finite admission/retention/output limits and measured evidence are in the
[probe guide](esp32_probe.md) and [prompt 03 report](reports/ess_release_03_2026-10-03.md).
The [fresh audit](reports/ess_release_03_audit_2026-10-04.md) verifies retained
value/attempt attribution, deadline-gated recovery and immutable result inspection.
The fixed `capture-read [address]` timing fixture reads reviewed settings window
0x0130/16 through the existing public codec and bus owner. It preserves model
cache evidence and returns raw frames; it does not implement typed settings
operations or the planned general command surface. See
[prompt 04](reports/ess_release_04_2026-10-04.md).
Checkout naming is independent of the CLI. The [architecture](architecture.md),
[axis contract](axis_contract.md) and [profile contract](profile_contract.md)
define the public operations that the console exposes.

## Public API and console boundary

Every device command calls the same public common-axis or typed profile API
available to upper firmware. The console has no private register writes,
second conversion implementation, hidden motion modes or CLI-only sequences.
Host utilities use application services; host axis configuration uses the
public validation/configuration functions. The console never reaches behind
the bus owner to perform a transaction.

Use one bounded command inventory for dispatch, help, side-effect descriptions
and API coverage checks. Reusable tokenization, semantic dispatch and result
formatting are framework-neutral; example adapters supply terminal I/O,
transport, time, persistence and application policy. Arduino, native ESP-IDF
and native fake-transport consumers exercise the same command semantics.
Native ESP-IDF is a first-class independent consumer and needs neither Arduino
nor FieldCore. Equivalent builds expose the same common and profile commands;
help reports deliberate build exclusions and actual adapter limitations.

A planned command is not an implemented handler. Help advertises only callable
commands in the selected build/profile, while inventory/capability reports
also identify documented but unresolved, unimplemented or unqualified items.
Those states are distinct from a drive capability known to be unsupported.

## Admission, ownership and interruption

One transport owner serializes every transaction on a UART. Start with one
foreground operation per selected axis; ordinary conflicting work returns a
structured busy result. Cached reports remain available while it runs. All
multi-transaction operations advance cooperatively through the public
`advanceOperation` contract using caller-owned state and supplied events/time.

A supported stop request is admissible while movement, homing, polling or a
native operation is busy. It supersedes queued ordinary work and receives
priority at a bounded, safe frame boundary; it does not cut off an in-flight
frame or bypass direction/framing settlement. Retain the interrupted
operation's evidence and the stop's result separately. Stop behavior and
whether pending device commands are retained or discarded are explicit public
request fields. A profile emergency-stop command uses the same priority path,
with its own documented effects; software cannot guarantee physical stopping
when communication fails.

`cancel` cancels unsent work and local waiting. It does not command a motor
stop, reverse a transmitted action or erase execution uncertainty. Cancelling
the superseded operation must not cancel an already admitted stop. The owner
settles pending TX, DE/RE and RX boundaries before another transfer.

Before starting, the application admits required refresh/handshake deadlines
against its bus schedule and reserves capacity for stop handling. Reject work
whose service requirements cannot be met. `auto off` affects optional
telemetry polling, not a running operation's required keepalive. A missed
deadline is reported; it is not proof that the drive stopped.

Use fixed input/output capacities and bounded work per console iteration.
Reject truncated input, excess tokens, trailing junk, numeric overflow,
negative unsigned values, NaN/infinity, unsupported units and unresolved
conversion before yielding transactions. Preserve integer/rational precision
instead of routing all numbers through floating point.

## Selection, configuration and observations

The following is the intended command vocabulary. Optional arguments and
profile-specific fields are defined by the public operation descriptors when
implemented. Aliases have identical effects on every platform.

| Command | Public operation or application behavior | Motor bus traffic |
| --- | --- | --- |
| `help [command]` | Actual command syntax, API mapping and effects | None |
| `version` / `ver` | Library/build, profile and platform identity | None |
| `profile list` | Compiled profiles, scope, implementation and qualification | None |
| `profile select <profile>` | Explicit idle-only local profile binding | None |
| `caps` | Implemented `readCapabilities`; probe/identity/config support, later capabilities remain planned | None |
| `useaddr <profile-address>` | Idle-only local target selection using the profile address rules | None |
| `config` / `settings` | Host settings, `getAxisConfig` and cached device settings, separately labelled | None |
| `host [baud <rate> \| fmt <format>]` | Show/change host serial tuple when idle, subject to adapter capability | None |
| `axis config show` | `getAxisConfig`, including scale/reference sources and missing fields | None |
| `axis config set <fields...>` | Build a complete candidate, `validateAxisConfig`, then idle-only `configureAxis` | None |
| `ping` / `probe` | `prepareProbe`; smallest supported non-changing presence query, with explicit identity confidence | Minimal query |
| `read identity` | Implemented `ESS_RS::prepareIdentity`/`getIdentity`; raw identity with unresolved model/firmware mapping | Reads |
| `discover [manufacturer <id> \| profile <id>] [bounds...]` | Application-owned bounded scan through reviewed probe/identity operations | Non-changing queries |
| `read state` | Implemented `ESS_RS::prepareState`/`getStateBlock`; three independently timed non-consuming blocks | Reads |
| `read config` | Implemented `ESS_RS::prepareConfig`/`getConfig`; bounded read-only motion-prerequisite subset | Reads |
| `status` | Cached host, transport, motor and operation state with ages | None |
| `health` | Cached presence/freshness and motor-readiness assessment | None |
| `health check` | Implemented state refresh; canonical `read-state` correlation, then separate cached assessment | Reads |
| `stats` | Transport/application counters | None |
| `stats reset` / `reset` | Clear local counters only | None |
| `drv` | Transport phase, deadlines, queue/buffer state and adapter capabilities | None |
| `diagnose` / `diag` | Bounded public non-consuming observations plus host diagnostics | Reads |
| `auto on [ms]` / `auto off` | Start/stop optional telemetry polling; report admitted cadence | Reads while enabled |
| `result [operation-id]` | Retained operation evidence/result, without consuming it | None |
| `cancel` | Cancel unsent work/local waiting; settle existing activity | No new device command |
| `verbose on\|off` | Bounded TX/RX tracing | None beyond scheduled work |
| `recover` | Idle-only host transport reinitialization; invalidate communication confidence | None |
| `flush` | Explicit idle-only host RX discard, recorded in diagnostics | None |

Address syntax and limits belong to the selected protocol/profile. Do not
impose Modbus numeric addressing on every serial drive. For ESS, the reviewed
initial scope uses unicast addresses 1-247; other profiles may require a
character address or an additional axis/module selector. Selection is local:
no automatic scan, guessed configuration write, enable or mode change occurs.
Optional discovery is an explicit bounded read-only operation over reviewed
protocol/tuple ranges; responding on RS485 does not establish compatibility
or safe mixed-protocol coexistence.

The [discovery contract](discovery_contract.md) defines query selection,
manufacturer grouping, address/tuple/deadline/request/result bounds, partial
results, cancellation and host-setting restoration. `profile list` is local;
`probe` performs one minimal supported query, with no hidden identity/status
sweep or automatic retries. A profile with no suitable probe returns a clear
unsupported/unresolved result before TX. `read identity` remains explicit.
The default discovery scope is the selected profile/current tuple, not an
unbounded protocol scan. Findings do not automatically rebind the selected
axis or write device settings. Probe success refreshes only the evidence
actually observed, not stale position, alarms, homing or motion readiness.

Classify observation effects, not merely the wire function. Some profiles have
read-to-clear completion/event fields. Generic reads, discovery, diagnostics,
health checks and optional polling must not silently consume those fields.
The profile declares destructive reads and a single designated consumer for
each handshake/event; explicit typed consumption or the owning operation
collects it once and publishes the retained observation to other consumers.
If only a consuming source can supply a requested field, generic observation
reports that restriction instead of issuing it behind the owner's back.

`axis config set` validates the entire replacement before publishing it and
advances the relevant configuration generation. It may preserve unspecified
fields from the current configuration in that candidate, but never partially
applies invalid input. Host scale, gearing, lead, polarity or reference settings
do not change drive subdivision, electronic gearing or encoder configuration.
Device configuration changes use explicit typed profile operations and
invalidate/reconcile affected host assumptions. Display operator assumptions,
device readback, qualified configuration and absent values distinctly.

Protocol decoding assumptions also belong to the profile. For example,
`profile ess-rs codec ...` may select an explicit local word-order assumption;
it is not a universal serial setting or a device word-order write. Show its
source and invalidate affected decoded values when it changes.

Rebinding profile/address or changing host serial settings requires settled
outstanding bus work and no active operation. Invalidate selected-device
identity, decoding/reference confidence and readiness as appropriate. Retain
historical raw observations and uncertain results under their original
profile, target, tuple and configuration generation. Never reinterpret old
register words using a newly selected word order or assign an old operation's
result to another motor.

## Common motion and coordinate commands

These commands map to the planned public axis functions. Their options must
represent public request fields; the CLI may not implement extra conversion,
angle selection or trajectory logic. Unsupported or unresolved requests fail
before any parameter write or start trigger.

| Command | Public operation | Meaning |
| --- | --- | --- |
| `motor move absolute <value> <unit> [options...]` | `prepareMoveAbsolute` | Signed unwrapped target in an explicit frame, preserving full turns |
| `motor move relative <value> <unit> [options...]` | `prepareMoveRelative` | Signed displacement with an explicit supported actual/commanded/queued-endpoint basis |
| `motor move angle <value> <deg\|turn\|rad> via <positive\|negative\|shortest> [tie <reject\|positive\|negative>] [options...]` | `prepareMoveAngle` | Wrapped orientation resolved to a multi-turn target using fresh referenced position |
| `motor velocity <value> <rate-unit> [options...]` | `prepareVelocity` | Signed continuous velocity with explicit ramp/service requirements |
| `motor home <method> [options...]` | `prepareHome` | A named supported reference-search or home-return method |
| `motor stop [options...]` | `prepareStop` | Explicit stopping and pending-device-command policy; may interrupt busy work |
| `motor enable` | `prepareEnable` | Enable the drive according to profile semantics |
| `motor release` | `prepareRelease` | Release drive power according to profile semantics |
| `motor alarm clear` | `prepareClearAlarm` | Request documented alarm clear, preserving prior alarm evidence |
| `axis origin <value> <unit> [options...]` | `setAxisOrigin` | Change host coordinate interpretation without bus traffic or movement |
| `motor position device <value> <native-unit> [options...]` | `prepareSetDevicePosition` | Explicit supported device-coordinate write without intentional movement |
| `motor torque <value> <effort-unit> [options...]` | `prepareTorque` | Optional documented torque mode; never inferred from current control |
| `motor current <value> A [options...]` | `prepareCurrent` | Optional documented current mode, distinct from torque |

Expose the complete common unit surface when the selected profile and
configuration can represent it:

- `steps`: configured command-position increments.
- `fullsteps`: motor mechanical full steps, only when the motor scale is known.
- `counts`: a named encoder source with established scale and coordinate
  relation to the command position; otherwise observation-only.
- `turn`, `deg`, `rad`: configured load-axis rotation.
- `mm`: configured linear travel, requiring known travel per load revolution.

Every position identifies its coordinate frame. Show the selected frame in
configuration and operation output; when omission is allowed in console syntax,
it means the explicitly configured default frame, not an inferred shaft.
Command and feedback coordinates remain separately identified. Relative
requests state their supported basis; stale feedback cannot silently turn a
relative operation into an absolute one.

Rate inputs include `steps/s`, `fullsteps/s`, source-qualified `counts/s`,
`turn/s`, `deg/s`, `rad/s`, load-axis `rpm` and configured `mm/s`.
Acceleration/deceleration use `steps/s2`, `fullsteps/s2`, source-qualified
`counts/s2`, `turn/s2`, `deg/s2`, `rad/s2` or configured `mm/s2`, where `s2`
means seconds squared. Explicit `rpm/s` means change in load rpm per second
and converts to turn/s2 by dividing by 60. Reject `rpm/s2` in acceleration
fields; it is a different physical quantity. Apply the public ramp semantics.
The CLI calls the same `convertPosition`,
`convertDisplacement`, `convertVelocity` and `convertAcceleration` used by
preparation. A profile's native ramp-time parameter is not relabelled as an
acceleration without a verified conversion.

Request options expose public frame/basis, speed, ramp, limit, rounding and
permitted-error fields. `EXACT` is the default for integer/rational requests;
explicit alternatives are `NEAREST`, `TOWARD_ZERO`, `FLOOR` and `CEIL`, with the
axis contract's tie rules. Radian conversion declares its approximation/error
bound and requires a permitted error; it is never described as mathematically
exact. Admission/result output includes requested and effective native values,
conversion/quantization error and any zero displacement after rounding.

For example, `motor move absolute 2 turn` and `motor move absolute 720 deg`
describe the same unwrapped target in the same configured frame.
`motor move relative -90 deg` requests a displacement.
`motor move angle 0 deg via shortest` selects an orientation using current
referenced turn count, rather than requesting absolute zero.
Its half-turn tie defaults to rejection; limits and uncertain references do
not silently select another revolution. These examples illustrate planned
syntax, not implemented or universally supported commands.

`axis origin` requires the axis contract's idle/reference evidence and
changes only host interpretation. It neither homes the motor nor rewrites its
counter. `motor position device` requires an implemented operation with resolved
no-motion counter semantics and invalidates affected origin relations.
A profile that only supports
clearing a counter to zero rejects other values; it must not substitute a
movement or host-offset change. Lost write acknowledgement retains coordinate
uncertainty.

## Complete typed profile access

The common surface is not the ceiling of supported family functionality.
Every implemented public profile device operation must be reachable through
a typed `profile <family> ...` command. For example, `profile ess-rs ...` routes to
`MotorControlRS::ESS_RS`. The explicit family must match the selected profile;
it does not silently rebind a motor or infer a protocol from a command.
Pure codec helpers such as CRC calculation do not require individual commands.

The [profile contract](profile_contract.md) owns the complete ESS inventory,
register evidence and coverage states. The following groups organize CLI
access and effects without duplicating that register inventory:

| ESS command group | Access and effect classification |
| --- | --- |
| `profile ess-rs help` / `inventory` | Implemented typed commands plus explicit documented gaps; local |
| `profile ess-rs identity` / `state` / `feedback ...` | Identity, raw status/alarms, I/O, position and speed; reads |
| `profile ess-rs codec ...` | Host decoding assumptions and provenance; local |
| `profile ess-rs communication ...` | Explicit device address/baud/format reads or writes; show active versus pending values and power-cycle requirements |
| `profile ess-rs driver ...` | Direction, subdivision, device word order and native driver configuration; classify each read/write |
| `profile ess-rs position ...` / `speed ...` / `jog ...` | Native motion parameters and documented serial actions; separate configuration, start and observation |
| `profile ess-rs home ...` / `limits ...` | Homing method/parameters, software limits, over-limit and fixed-length interruption settings |
| `profile ess-rs segments ...` | Multi-position/multi-speed and shared start-speed configuration; expose documented trigger requirements |
| `profile ess-rs io ...` | Reviewed input/output functions, explicit disabled/none assignment, polarity and custom output control |
| `profile ess-rs tuning ...` | Closed-loop, encoder, current, lock, filter, deviation, arrival, current-loop and LA settings |
| `profile ess-rs auxiliary ...` | Explicit enable/release, stop/emergency-stop, alarm/position clear, save and factory restore |
| `profile ess-rs parameter <typed-name> ...` | Remaining reviewed parameter descriptors with typed values, ranges, prerequisites and effects |
| `profile ess-rs reg read <register> [count]` | Diagnostic FC03 read within reviewed non-consuming readable windows and the ESS 16-register limit |

These are groups for later concrete commands, not blanket handlers accepting
arbitrary payloads. Every leaf identifies a public operation and its exact
validation, volatility/persistence, coordinate effects and state prerequisites.
A typed parameter command is acceptable only with a reviewed public descriptor;
an unrestricted `writereg` escape hatch does not count as command coverage.
Unknown/unresolved descriptors fail before writes. Full serial command access
does not invent actions that the device exposes only through external I/O.
In particular, the current ESS manual describes multisegment execution as
external-input-triggered: the CLI exposes documented configuration, not an
invented serial `startSegment` action. Model-specific I/O counts likewise come
from the ESS inventory, not generic prose for other product families.

The typed I/O commands must expose an explicit `none`/disabled function for
each supported terminal. For ESS this maps to the existing native
`InputFunction::UNDEFINED` or `OutputFunction::UNDEFINED`, value `0`; it is not
a polarity change. Selecting unused external I/O is a valid setup for serial
motion. Host wiring declarations are local configuration and must be shown
separately from drive assignment readback and raw/logical levels. An
unconnected terminal must not silently acquire a disabled assignment, and a
disabled output must not be reported electrically inactive without evidence.
Commands whose selected mode needs external signals report the missing
prerequisite before writes; other serial commands remain available. Neither
startup nor a normal move rewrites I/O assignments. These commands call the
same typed profile API as any application; their implementation is still
pending.

No startup, `setup`, `diagnose`, `recover` or host configuration command may
silently enable, move, home, clear alarms, save parameters, restore defaults or
change device communication. Use explicit device operation names and report
persistence and post-write verification. If host persistence is added, use
`host save`, `host load`, `host defaults`; never persist a command for replay.
Application interlocks and any commissioning confirmation policy apply equally
to common and profile routes. They are not codec features or a reason to hide
native commands only on ESP-IDF.

## Status, health and retained outcomes

Each report identifies the profile, target and configuration generation and
separates the following information:

- Host configuration/capabilities: serial tuple, frames, scales, origins,
  decoding assumptions and their sources or missing prerequisites.
- Transport: idle/transmitting/waiting/fault phase, deadlines and last error.
- Observations: per-block validity, source, last attempt/success and age,
  retaining the original target/tuple/configuration context.
- Motor: raw alarm/status/unknown bits and supported decoded flags; actual
  versus commanded position/velocity, with native and qualified physical units.
- Operation: admission, transmission, acknowledgement, queue/execution/completion
  evidence where observable, plus unknown execution and original context.
- Health: communication/presence/freshness separately from motor alarm/readiness,
  including reasons and configured thresholds. Monitoring `disabled` does not
  mean drive windings released.

`status` and plain `health` are passive. Explicit `read ...` and `health check`
refresh observations. This intentionally resolves inconsistent live/cached
behavior among [sibling consoles](reference/02_ecosystem_review.md) and matches
passive FieldCore snapshots. Failed reads retain last valid values and advance
the latest attempt error; they do not publish zeros. A successful identity
read does not refresh position/alarm age or prove motion compatibility.
Separate reads do not claim an atomic motor snapshot.

Print a concise admission result, a local operation ID for asynchronous work
and one terminal result per foreground operation; bounded progress events may
precede it. IDs are host correlation, not invented wire transaction IDs.
Retain codec code/detail, raw device exception/alarm, transport result and
execution uncertainty. `status`/`result` printing does not consume results.
Routine telemetry output is rate-limited.

A validated write acknowledgement is not motion completion. Later readable
state does not prove that a lost non-idempotent command never executed. Do not
automatically retry relative movement or replay commands after recovery.
Implemented `monitor off` disables finite observation polling and cancels its local continuation while physical TX settles; it is not a motor stop. Other planned automatic modes remain unimplemented. `recover` only reinitializes host transport while idle; it does
not clear alarms, enable the motor or resend work. `reset` always means local
statistics only and does not erase uncertain operations or coordinate state.

## Independent consumers and verification

Standalone operation has no dependency on FieldCore queues, settings, headers
or CLI registry. A future FieldCore adapter uses its existing bus owner and
retained-result routing, mapping common/profile operations into typed product
requests. Its current `rs485 status`, `timing`, `trace`, `recover`, `result` and
device `probe`/`read`/`read last` provide integration conventions, not limits on
this library's command coverage. The documented
[integration gaps](reference/02_ecosystem_review.md#fieldcore-integration-gaps)
remain separate work.

When implementation is requested, verify help/dispatch/public-API coverage,
including every implemented typed profile device operation. Test rejected requests
produce no transaction, exact large integer parsing, source-specific count
conversion, multi-turn versus wrapped-angle targets, and both coordinate-change
operations. Exercise host-only commands without device writes; preserve cache
and uncertainty across failed reads, configuration changes, reset and recovery.
Verify one UART transaction, bounded cancellation, stop interruption, required
service deadlines and no automatic motion replay. Equivalent Arduino/native
ESP-IDF consumers must pass the same semantic command cases; report actual
adapter serial/echo/timing capabilities separately.

Include the no-external-I/O setup in API/CLI parity tests. Verify explicit
no-function writes and readback, local-only wiring declarations, and rejection
of switch/trigger-dependent requests when their required terminals are unused.

The [ESS implementation reference](reference/01_implementation_reference.md)
and original vendor PDFs remain authoritative for native behavior. Console
contract tests, firmware builds and physical motor qualification are distinct.
See [verification](verification.md) and the linked bench reports for completed
checks; the planned full motion/I/O console remains unimplemented.
