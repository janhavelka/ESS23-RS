# ESS digital I/O and external controls

The typed I/O operations extend the existing installed
[DriverSettings.h](../include/MotorControlRS/profiles/ess_rs/DriverSettings.h)
settings sequence. They use the same caller-owned `DriverContext`,
`PreparedDriver`, `ActionEvent`, validation, write/readback progress and failure
handling as drive settings. No GPIO, transport, clock, allocation, automatic
retry, save, restore or rollback enters the core.

## Reviewed terminals and values

The original [function manual](vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf)
physical PDF pages 27-28 and 73-74 were visually checked. The ESS appendix,
rather than the generic wider terminal examples, defines four inputs X0-X3
and two outputs Y0-Y1. The register ledger remains the address source of truth;
this table explains its policy rather than defining additional registers.

| Ledger record | Typed operation and supported value |
| --- | --- |
| `INPUT_POLARITY` | Bits 0-3: default or inverted; other bits reserved |
| `INPUT_X0_FUNCTION` through `INPUT_X3_FUNCTION` | Indexed function selection, terminal indices 0-3 |
| `OUTPUT_POLARITY` | Bits 0-1: default or inverted; other bits reserved |
| `OUTPUT_Y0_FUNCTION`, `OUTPUT_Y1_FUNCTION` | Indexed function selection, terminal indices 0-1 |
| `CUSTOM_OUTPUT` | Bits 0-1: logically invalid or valid for the corresponding custom-assigned output; other bits reserved |
| `INPUT_STATUS`, `OUTPUT_STATUS` | Existing read-only state observations, never writable |

All nine configuration fields are documented RW. Input functions 0-17 are
resolved: none, origin, positive/negative limit, release, stop, emergency stop,
position start, speed start, JOG positive/negative, homing start, PT/PV trigger
and binary segment selector PIN0-PIN3. Position, home and PT starts are
edge-operated; speed and JOG are level-operated. PV uses its configured
level/rising-edge policy. These assignments do not establish that an input is
connected, qualified or currently asserted.

Output functions 0-5 and 9-10 are available: none, alarm, running, homing
complete, in-position, brake and the two resolved custom selections. Values
6-8 lack descriptions and reject. The listed `CUSTOM_2` value 11 retains its
unresolved mapping to two ESS outputs and rejects before traffic; reads retain
it raw. A custom-output candidate requires a corresponding resolved custom
function in the full candidate configuration. The bounded implemented control
subset uses Y0/CUSTOM_0 and Y1/CUSTOM_1. Assignments 9 and 10 remain legal on
either documented terminal, but cross-mapped custom actuation is guarded: the
pages specify terminal bits without resolving selector remapping. It does not
make another output terminal exist.

`InputFunction::UNDEFINED` and `OutputFunction::UNDEFINED` are the documented
value zero, meaning the terminal has no function. The API and CLI use the same
checked setter for this explicit disabling action. Inverting polarity and
writing a custom output invalid are different operations. No-function does not
document voltage, de-energization of an attached load, activation delay or
persistence. An echo and matching register readback do not supply those facts.

## Public preparation and retained effects

`prepareDriverRead(..., DriverGroup::IO)`, `nextDriver`, `advanceDriver` and
`getIo` read three gap-free reviewed windows: input polarity plus four
functions; output polarity plus two functions; and custom output. The largest
reply is 15 bytes. Complete publication retains raw unknown functions and
reserved bits with per-window provenance. Failed refreshes leave the previous
published observation unchanged. These reads are not an atomic snapshot.
An observed reserved bit in a selected polarity/custom word blocks its whole-word
update; a masked candidate must not silently clear unexplained existing bits.

For updates, set `DriverRequest::group` to `DriverGroup::IO`, select fields
explicitly and call `prepareDriverSettings` with the exact copied
`DriverPrerequisites`. `prepareInputFunction(request, index, function)` and
`prepareOutputFunction(request, index, function)` select the same checked
engine; bounds and unavailable values leave the request unchanged. Polarity
and custom-output candidates use the corresponding typed fields and masks.
Drive and I/O groups do not mix in a single update.

The entire candidate, previous settings, target, configuration generation,
stationary evidence, actual I/O evidence, wiring and exact external-effect
qualification are checked before the first yielded request. Unknown wiring
does not authorize reassignment. Connected wiring needs the affected external
control or load policy to be established. A fresh current invalid input level
alone does not qualify changing its polarity: the change can reinterpret it
as asserted. Removing an existing limit or stop is an explicit selected
configuration action, never a startup or ordinary-motion side effect.

Each selected field yields one FC06 and one FC03 readback, at most eighteen
transactions. Requested value, checked acknowledgement, readback, possible
effects and execution uncertainty remain separate. Every new write is capped
by the original stationary evidence interval and immutable operation deadline.
Accepted writes invalidate dependent configuration/reference/input knowledge
even when the reply is lost. Historical results keep their original binding.
Cancellation and recovery do not roll back or replay a partial update.

An application may explicitly qualify `allowEchoReadback` for an exact I/O
candidate on known unconnected affected terminals. This setting-only policy
permits a checked, on-time FC06-shaped frame with unconfirmed source to yield
the subsequent non-changing FC03 readback. The original frame remains
unconfirmed, its acknowledgement remains false and its execution remains
`UNKNOWN`; matching confirmed-source readback settles the observed stored
setting, not the origin of that frame. A missing, malformed, mismatching or
unconfirmed readback remains uncertain and prevents the next write. Checked
device exceptions do not enter this handoff. This policy never retries the
write or qualifies motion, immediate activation, electrical timing, persistence
or an attached load. Without the explicit qualification, the existing
confirmed-response requirement applies.

The standalone limits this policy to known unwired, non-actuating input
assignments (none/origin/limits) and unloaded outputs. Input polarity changes
require each changed terminal to have no function in both the observed previous
settings and the candidate. A grouped final no-function candidate alone cannot
authorize inversion while an earlier assignment remains active; explicitly
disable/read back/re-read first. It retains all independent action/motion qualifications.
An observed register path is useful evidence even when oscilloscope and
external switch/load tests are unavailable; it must not be reported as those
physical qualifications.

## Wiring, assignment and observation

The application records connected, unconnected and unknown wiring separately
from the drive's function assignments and logical I/O observations. The bench
has power and RS485 only; all X/Y terminals are declared unconnected. That
does not imply assignments are zero. The hardware manual physical PDF page 9
describes four optoisolated 24 V inputs, a minimum 10 ms pulse and two isolated
outputs; no such switches or loads are attached here. Its power-up prose says
the inputs are unspecified/invalid, while the appendix and actual initial
reads have assignments 1/2/3/0. Use observed settings instead of interpreting
that prose as disabled assignments.

For known unconnected inputs, serial-only operations do not acquire a blanket
home/limit/trigger wiring requirement. They still need their own documented
mode, actual assignments, logical state and readiness policy. Serial enable
versus an asserted external release input remains unresolved. No automatic
reassignment, polarity workaround or precedence assumption resolves it.

## Console and external-trigger handoff

```text
io read [address]
io set x0 none [x1 FUNCTION ...] [address]
io set y0 none [y1 FUNCTION ...] [address]
io set input-polarity MASK [output-polarity MASK ...] [address]
io set custom MASK [address]
```

`none` selects function zero; numeric function values use the same preparation
and unavailable-value checks as the direct API. Input masks are at most 15 and
output/custom masks at most 3. Strict integer parsing, duplicate-field checks,
line/token bounds, retained operation/result storage and command correlation
remain the existing console contract. Host wiring declarations remain host
configuration, separate from device assignments and logical state reads.

Homing methods requiring a switch need that method's verified assignment,
connected input, transitions and fixture. The documented switch-method
capability remains visible while execution is unimplemented/unqualified;
setting a function does not make the method ready. Methods 33/34/35 have no
required external terminal and retain their existing index, readiness,
parameter, completion and reference qualifications.

For prompt 16, PT/PV triggers and PIN0-PIN3 are external-input choices. Their
setters configure the drive; they do not send a trigger or establish a physical
edge. Segment execution requires its own assigned connected trigger, selector
ownership, valid timing and stopped/sequence policy. The manual permits no
invented serial segment-start command. Missing switches/loads leave physical
function tests NOT RUN without blocking independent register-path evidence or
eligible serial-only operations.

FieldCore's current RS485 owner remains a read-only integration reference. Its
measurement payload, request-identical prefix stripping and millisecond
deadlines cannot replace these typed settings results, genuine FC06 echo
handling, closure bounds or no-replay uncertainty policy. No FieldCore or MCU
types enter the public operation signatures.


## Storage and evidence bounds

On the ESP32-S3 ABI, `DriverContext` is2936 bytes, `DriverPrerequisites`704,
`DriverRequest`72, `DriverObservation`464, `IoObservation`352,
`DriverEvidence`96, `DriverProgress`20 and `PreparedDriver`48.
The application places retained contexts, prerequisites, caches and two8192-byte
console buffers in its caller-owned PSRAM block. Capture/driver state and stacks
remain internal. No new queue, task, heap allocation or I/O enters the core.
The existing128-byte input/20-token,32-byte input service and64-byte output
service budgets remain; direct API candidates support allnine I/O fields,
while a CLI candidate must also fit those line/token bounds. The console has nine ordinary
correlation slots and one separate stop slot; application admission is also
bounded by eight ordinary retained operation records. The existing dedicated
stop/monitor records and owner/result bounds apply; inspecting results does not consume them, and release is explicit.
