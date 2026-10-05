# Common axis API contract

This is the accepted design for the common axis layer of `MotorControlRS`.
It defines the intended callable library behavior for applications and consoles.
`Units.h` implements displacement, velocity and acceleration conversion with
independent preferences. Prompt 07 adds [exact host target preparation](axis_preparation.md),
origins, reference evidence, quantization and native soft limits through `Axis.h`.
Prompt 09 implements the bounded ESS finite-relative sequence; prompt 10 reuses
it for absolute and wrapped-angle motion with shared `preparePosition` arithmetic.
Prompt11 adds [finite serial velocity](ess_velocity.md), shared exact rate
preparation, native ramp snapshots and the existing priority stop.
Physical qualification and general profile ramp mapping remain open. See
[encoder and units evidence](reference/06_encoder_units.md) for the initial API.
The [architecture](architecture.md) defines the three layers and ownership,
the [profile contract](profile_contract.md) defines complete family access,
and the [CLI contract](cli_contract.md) maps commands to these same operations.

ESS-RS is the first implementation target, under `MotorControlRS::ESS_RS`.
Leadshine iEM-RS supplies a documented contrasting design case, not a second
supported backend. See the [feasibility review](reference/03_multi_vendor_feasibility.md)
for evidence and limitations. Common operations must never imply that every
selected drive implements them.

This is also the behavioral baseline for a separate future CANopen motion
library. Share unit meaning, capability rejection, acknowledgement/completion
distinctions, stop intent and observation validity. Protocol transactions,
network state and native features remain specific to each library. Matching
function names alone does not establish matching behavior. Extract common
types/code only when both implementations demonstrate a concrete need.

## Callable surface and ownership

Use the following function names as the planned common vocabulary. This is
a behavior contract, not a declaration of final C++ signatures or layouts.
Each fallible call returns `Status`. Preparation, conversion and codec payload
outputs remain unchanged on call failure, with the documented output-count
exception. `advanceOperation` separately records valid failure events and
operation outcomes as specified below. Unsupported operations fail before
any transaction is yielded.

| Operation | Planned callable behavior |
| --- | --- |
| Select and inspect | `getCapabilities`, `getAxisConfig`; report the selected profile and caller-owned configuration |
| Discovery capability and presence | `getDiscoveryCapabilities`, `prepareProbe`; inspect per-profile discovery methods and prepare the smallest supported non-changing presence query |
| Configure interpretation | `validateAxisConfig`, `configureAxis`; validate and replace host-side scale/reference configuration while idle |
| Convert quantities | `convertPosition`, `convertDisplacement`, `convertVelocity`, `convertAcceleration`; checked, side-effect-free conversions with quantization reports |
| Observe | `prepareReadIdentity`, `prepareReadState`, `prepareReadConfiguration`; prepare supported reads, with explicit requested fields |
| Position | `prepareMoveAbsolute`, `prepareMoveRelative`; prepare a drive-internal trajectory in an explicit coordinate frame |
| Wrapped orientation | `prepareMoveAngle`; resolve an angular path and prepare its resulting multi-turn target |
| Continuous velocity | `prepareVelocity`; prepare the requested signed velocity, ramp policy and any required refresh contract |
| Reference search/return | `prepareHome`; require a supported, explicit homing or home-return method |
| Coordinate change | `setAxisOrigin` for host interpretation; `prepareSetDevicePosition` for an explicit supported device counter change |
| Stop | `prepareStop`; require explicit stop behavior and pending-device-command policy |
| Drive power state | `prepareEnable`, `prepareRelease`; change drive state independently of polling or module selection |
| Fault action | `prepareClearAlarm`; request a documented clear action and preserve the original alarm evidence |
| Optional effort control | `prepareTorque`, `prepareCurrent`; available only for profiles with the corresponding documented control mode and units |
| Execute a finite step | `advanceOperation`; process one supplied event and yield bounded work, a wait requirement or a result |

The common layer implements units, limits and operation semantics. The
selected profile implements family sequencing and the codec validates frames.
The [discovery contract](discovery_contract.md) separates minimal presence
probing from identity reads and bounded application-owned scans. Not every
profile has a suitable non-changing probe; absence or unresolved semantics
is an explicit capability result before transmission.
The CLI parses input and calls these functions; it must not contain another
implementation of angle resolution, conversion, limit checks or motion
sequencing. FieldCore can call the same surface without console text.

All data is fixed-size or caller-provided with explicit capacities. A
preparation function validates the request and populates a caller-owned
`OperationContext`; it performs no UART access, motor action or waiting.
Contexts contain only bounded owned values and explicit state. Any reference
to immutable profile tables must have static lifetime; transaction/input
buffers are borrowed only for the duration of a call.

The application owns axes, configurations, contexts, cached observations,
transport, scheduling, time, deadlines, retries and physical interlocks.
No default constructor, selection, configuration, conversion or cached query
causes motion, drive enable, homing, persistence or communication changes.

## Optional drive I/O

Serial motion must support installations with no external drive I/O connected.
The application may declare a terminal unconnected; the profile exposes the
documented no-function assignment when the drive supports disabling that
terminal. Wiring, configured function, polarity and observed signal level are
separate facts. An unconnected declaration changes no register, and a disabled
function is neither an unknown observation nor proof of an electrical output
level. Do not infer physical wiring from a status bit.

Require external signals only for an operation that actually depends on them:
for example a selected switch-based homing method or an external segment
trigger. Missing or disabled required signals reject that operation before
writes. Ordinary serial positioning, velocity and state reads have no blanket
requirement for origin/limit switches, external enable or output wiring; their
actual device readiness and configured application limits still apply.
Profiles must account for active input functions that can inhibit or trigger
motion. A serial enable acknowledgement does not establish that it overrides
an asserted hardware release, stop or limit input.

Disabling a drive terminal is explicit device configuration, never an implicit
step in a move, startup or recovery. It must not silently remove an application
interlock. Outputs needed by a configured brake or other external function
have the same operation-specific dependency rules. The core performs no host
GPIO access and assumes no particular controller board or attached sensors.

## Units and coordinate frames

A numeric value always carries a unit and a coordinate-frame identifier.
Raw device fields retain their register width, signedness, unit and source.
Common integer storage must preserve at least signed 64-bit positions; a
profile requiring more precision must reject an inadequate representation,
never truncate it. Exact native counts are not routed through floating point.

| Quantity | Meaning |
| --- | --- |
| Command steps (`steps`) | One configured drive command-position increment, including its current microstep/electronic-gearing interpretation |
| Motor full steps (`fullsteps`) | Mechanical full steps of a stepper motor; available only with an established full-steps-per-motor-revolution value |
| Encoder counts (`counts`) | Increments of one identified feedback source; its decoding multiplier and coordinate relation must be explicit |
| Turns, degrees, radians | Rotation of the configured load axis; one load turn equals 360 degrees or 2 pi radians |
| Travel, initially millimetres | Translation of the configured machine axis, requiring an established travel-per-load-revolution relationship |
| Native position | The profile's exact command or feedback register value, explicitly outside host-origin/engineering-unit conversion |

`steps`, `fullsteps` and `counts` are not synonyms. A servo need not have a
full-step concept. A command step need not equal an encoder count or a fixed
fraction of a motor turn after gearing changes. Source-specific encoder
counts may be used as a requested motion unit only when their validated,
policy-accepted scale and reference can be mapped to command coordinates;
otherwise they are observation-only and that request is rejected.

Engineering angles refer to the load shaft, even when motor and load are
direct-coupled. A raw-native operation or a deliberately configured motor-axis
frame supplies motor-shaft interpretation; do not silently switch the meaning
of degrees between calls. Commanded and measured positions remain separate
quantities after conversion.

Absolute positions use the selected host origin. Relative displacement uses
the same scale and positive direction but no origin offset. The request must
identify whether relative motion is based on the drive's actual position,
commanded position or queued trajectory endpoint. The profile reports its
supported basis. It must not substitute one basis for another or turn a
relative request into an absolute target from stale cached feedback.

## Scale and reference configuration

`AxisConfig` contains explicit, independently valid metadata:

- Command increments per motor revolution, as a positive rational number.
- Motor revolutions per load revolution, as a positive rational gear ratio.
- Motor full steps per revolution, when known and applicable.
- Each feedback source's identifier, coordinate basis, polarity and positive
  rational count scale. Its basis may be motor revolution, load revolution
  or linear travel; require an explicit conversion path to command coordinates
  rather than assume every encoder is mounted on the motor.
- Travel per load revolution, as a positive rational length with explicit
  units, when applicable.
- Positive-axis polarity, separately represented as +1 or -1.
- Host zero mapped to an exact native command coordinate, and separately to
  each feedback coordinate when that relationship is established.
- Coordinate/configuration generation, origin validity and reference source.
- Explicit soft limits, motion limits, rounding policy and allowed error.

Each scale/reference value records whether it came from an operator
assumption, device readback or a qualified model configuration. An explicit
operator-provided scale is usable after mathematical/configuration validation
and acceptance by application policy; it remains labelled as an assumption,
not hardware verification. This does not permit guessing unresolved device
wire units, widths or signedness. Missing, unresolved or policy-unaccepted
conversion metadata rejects the operation. An absent scale is unknown, not
one. Explicit 1:1 ratios are valid for direct coupling.
All rational denominators and positive scale numerators must be nonzero.
Use reduced integer ratios and checked arithmetic; do not silently approximate
a gearbox ratio to an integer. Backlash compensation, elastic deflection,
slippage and nonlinear mechanisms are outside this affine scale conversion.

Let `C` be command increments per motor revolution, `G` motor revolutions
per load revolution, `s` the polarity and `O` the native command coordinate
at host zero. For a load-axis absolute position `x` in turns:

`nativeTarget = O + s * C * G * x`

For a displacement, omit `O`. Degrees divide by 360; radians divide by
2 pi. Travel first divides by configured travel per load revolution.
Full steps first divide by full steps per motor revolution and therefore
do not multiply by `G`. Encoder-count conversion uses its declared motor,
load or travel basis, polarity, scale and separately established reference;
include the gear/lead conversion only where that basis requires it. Feedback
origins are not assumed equal to command-register zero. Reverse conversion
follows the same explicit bases.

For example, 3200 command steps per motor revolution and a 5:2 motor/load
gear ratio produce 8000 command steps per load revolution. A relative load
rotation of 90 degrees therefore represents 2000 command steps before
polarity. These are illustrative configuration values, not ESS defaults.

`configureAxis` replaces a validated host configuration atomically while
the axis is idle. It changes no device setting. Scale, polarity, origin,
profile or target changes advance the relevant logical generation and
invalidate prepared operations and derived observations. Transient application
UART tuple selection has a separate transport generation: serving another
endpoint does not itself invalidate prepared work for the original motor.
Deliberate rebinding and device communication changes still invalidate the
affected endpoint confidence. Preserve raw
historical observations with their original generation instead of relabelling
them. Device electronic-gearing changes use typed family operations and
invalidate this configuration until explicitly reconciled.

## Conversion, quantization and limits

Conversions and preparations return the requested quantity, effective native
quantity, effective converted quantity and conversion/quantization error.
They must never silently clamp, saturate, wrap or discard a fractional step.
An explicit rounding policy belongs to the request or validated configuration:

| Policy | Meaning |
| --- | --- |
| `EXACT` | Accept only an exactly representable result within the stated numeric precision; default for integer/rational requests |
| `NEAREST` | Round to nearest representable value, with halfway cases to the even integer |
| `TOWARD_ZERO` | Discard fractional magnitude toward zero |
| `FLOOR` | Round toward negative infinity |
| `CEIL` | Round toward positive infinity |

The chosen policy and maximum permitted error apply to the final native
quantity. Request sign and origin addition do not change rounding semantics.
Preparation must report the actual motion displacement after rounding; in
particular it must not hide a requested small movement that quantizes to zero.

Use exact rational/integer arithmetic for command counts, rational turns,
degrees and travel wherever possible. Cancel common factors before multiplying
and check intermediate/final overflow, sign reversal and origin addition.
Reject NaN, infinity, zero denominators, invalid units, missing scale and
out-of-range values without yielding work. Decimal console input must preserve
the same precision contract as direct API input.

Radian conversion necessarily uses a specified approximation to pi. Publish
its precision/error bound and require a permitted conversion error; do not
label an approximate radian conversion mathematically exact. If numeric
uncertainty crosses a rounding or limit boundary, reject the ambiguous result.
Exact rational turns/degrees remain available for exact angular targets.

Check both the requested physical target/path and the effective quantized
target/path against configured limits; rounding is not a way around a limit.
Check native range, speed, acceleration/deceleration, optional jerk and
profile-specific quantization independently. Limits use the active coordinate
generation. Multi-turn soft limits require an established multi-turn reference.
Relative endpoint checking requires trustworthy current/basis information;
when a configured limit cannot be evaluated, fail the request rather than
silently omit that check. Application interlocks remain external and may
reject an otherwise valid preparation.

## Position and angle semantics

`prepareMoveAbsolute` preserves its full signed position, including turn
count. A 720-degree target means two load turns from host zero; it is not
normalized to zero. `prepareMoveRelative` preserves the requested signed
displacement, including full turns. These operations never choose a shortest
angular path implicitly.

`prepareMoveAngle` explicitly requests a wrapped orientation. Normalize its
orientation into one turn and choose a congruent multi-turn target using a
current, referenced position supplied by the caller. The request chooses
`POSITIVE`, `NEGATIVE` or `SHORTEST` direction. For `SHORTEST`, an exact
half-turn tie uses an explicit `REJECT`, `POSITIVE` or `NEGATIVE` tie policy;
the default is `REJECT`. A quantization/precision interval that cannot resolve
the comparison is also rejected.

An already matching orientation means zero movement for all three path
policies. A forced extra revolution must be requested as relative motion or
an explicit multi-turn target. The shortest operation chooses the smallest
angular displacement, then checks the complete path and limits. If that path
is forbidden it fails; it does not silently substitute a longer revolution.
The caller can submit an explicit alternative target/direction.

Wrapped resolution requires an established current turn count, known origin
and an observation accepted as fresh by the application's policy. A lone
single-turn encoder value does not establish a multi-turn absolute position.
Reject preparation or revalidate before transmission if its starting-reference
assumptions change. Publish the resolved multi-turn target and direction with
the operation result so the CLI and application see exactly the same decision.

The implemented `PositionRequest` selects this behavior with `wrapped=true`,
`AnglePath` and `HalfTurnTie`; `preparePosition` is the one common arithmetic
path. Ordinary absolute preparation does not normalize. Exact turns/degrees
normalize before scale multiplication and support rational native-per-turn
scales with checked fixed storage. Wrapped requests require MOTOR/LOAD frame,
angular units and a fresh native reference with matching requested basis.
Approximate radian intervals crossing equality or half-turn selection reject;
they do not become resolved ties through a caller-selected direction.

Pure absolute preview may calculate an endpoint without current evidence and
then reports displacement as unknown. The ESS finite absolute executor also
permits an unwrapped target without a current-position reference when conversion
and applicable limit checks need none. Native steps bypass host origin;
engineering coordinates still require their established origin and scales.
Wrapped-angle execution requires established stationary ACTUAL command-coordinate
evidence. Any reference supplied to either executor must be stationary ACTUAL
evidence; the executor copies a consumed reference into the operation and checks
its freshness at supplied admission time. Its age budget must cover
readiness-capped staging/trigger deadlines. Observation
work retains the immutable overall operation deadline; expiry of the consumed
starting reference after an on-time trigger does not cancel that observation
sequence. Zero effective displacement is reported by pure preview and rejected
by the executor before traffic. These software paths do not qualify unresolved
drive sign, coordinate-source, electrical or ramp semantics.

## Velocity, ramps and effort

Velocity is signed in the selected frame. Supported input families are
steps/s, fullsteps/s, source-specific counts/s, turns/s, degrees/s, radians/s,
rpm and configured travel/s. Here rpm means load revolutions per minute;
a profile converts it to motor rpm when needed. Acceleration/deceleration use
nonnegative magnitudes in steps/s², fullsteps/s², source-specific counts/s²,
turn/s², deg/s², rad/s² or configured mm/s². An explicit rpm/s input means
change in load rpm per second: 1 rpm/s = 1/60 turn/s². rpm/s² is not an
acceleration unit and must not be accepted in an acceleration field.
Direction is carried by the velocity/position request, not by a negative
acceleration-time parameter.

Common ramp requests state acceleration, deceleration and optionally jerk,
or explicitly request the verified device-configured ramp. Zero does not
mean an undocumented automatic default. A profile translates native forms
such as time-per-speed-change and reports the effective settings. If a ramp
form, unit or operating mode cannot be represented, reject it. S-curve,
on-the-fly target changes and blending are optional capabilities, not implicit
features of a speed or position request.

A velocity request does not promise a final position. Setting speed to zero
is not substituted for `prepareStop` unless the requested stop contract
explicitly permits that profile mapping. Keepalive-dependent velocity/jog
reports its required refresh interval and consequences of a missed service
deadline; the application must admit that schedule before starting. This is
profile-specific: it must not infer an ESS serial jog watchdog from
Leadshine's documented repeated-trigger requirement.

Torque uses an explicit basis such as N m or percent of rated torque;
conversion between them needs an established rated torque. Current uses A
and is a distinct control capability, never renamed torque. Torque/current
mode, limits and stopping behavior must have resolved profile semantics and
an implemented mapping, with hardware qualification reported separately.
Tuning, I/O mapping, path programs, persistence and every other documented
family command remain reachable through the complete typed family surface
in the profile contract, even without a common equivalent.

## Homing and coordinate changes

`prepareHome` names the actual method: reference-switch search, limit-based
search, index search, absolute-encoder home return or another explicitly
supported method. Parameters include direction, rates, offsets and applicable
limits where the method needs them. Unsupported methods are rejected; return
to a known zero is not silently substituted for a reference search.

`setAxisOrigin` changes host coordinate interpretation without moving the
drive or rewriting its counters. It requires an idle axis and established
native-coordinate evidence; it advances the coordinate generation and
invalidates derived targets/limits until reconciled. It never sets a physical
homed flag merely because the operator chose a zero.

`prepareSetDevicePosition` changes the drive's documented position/counter
coordinate without intentional motion. It requires an implemented operation
with resolved profile semantics, appropriate stopped-state evidence and
explicit acknowledgement/verification handling. It invalidates command and
feedback origin relations affected by the write. A lost acknowledgement can
leave the coordinate change unknown; it must not be hidden as a successful
host-origin change.

`invalidateAxisReference` is the implemented pure host confidence-loss operation:
it clears both origins, derived limits and supplied reference confidence while
preserving unit scales/preferences, then advances the coordinate generation.
Exhausted or disabled generations never wrap or revive; confidence still clears
when invalidation returns `GENERATION_EXHAUSTED`. Applications invalidate on
actual release, uncertain clear, newly observed external movement, stale/lost
reference or relevant interpretation changes, preserve admitted/historical
evidence and require fresh qualification before new dependent preparation.
The ESS `prepareSetDevicePosition` subset supports explicit zero clear only,
with caller-qualified documented semantics/readiness and checked acknowledgement
plus a new zero-pair observation. Arbitrary nonzero requests reject before work;
neither a lost acknowledgement nor raw zero alone establishes a host origin.

Successful homing establishes only the reference and completion evidence
defined by that method/profile. Device restart, release, encoder resets,
configuration changes or movement without observable feedback may invalidate
position confidence according to the profile. Never infer homing completion
from an old latched flag or from a successful trigger echo alone.

## Capabilities, observations and outcomes

Capabilities report three independent dimensions: documented device support
and semantic certainty for the selected model/firmware, implementation
coverage of its common/native mapping, and hardware qualification evidence.
Do not collapse them into one supported Boolean. An implemented documented
operation may still be hardware-unqualified; application policy decides
whether that state is admissible and reports it explicitly. A hardware claim
must identify its actual tested scope.

A request can additionally fail because a required scale/reference is missing,
unresolved or not accepted by policy, or because a live precondition is
unsatisfied. These are distinct structured reasons; callers must not inspect
message strings to distinguish them. Operator-supplied mechanical configuration
does not replace unresolved wire semantics or an absent command mapping.

State reports preserve per-field validity, source, raw value and configuration
generation. The caller records observation/attempt times and age. Fields
include actual and commanded position, actual and commanded velocity, drive
enabled/released/running state, reference status, limits, alarms and optional
effort feedback. Unsupported, unknown and stale values are not zeros or false.
A commanded-position register is not measured feedback, including on an
open-loop drive. Separate-read reports do not claim an atomic snapshot.

Keep codec validation `Status`, transport outcome, device state, motion
progress and application health separate. Operation evidence can establish
admission, transmission, acknowledgement, device queueing, execution and
completion only when the profile provides that evidence. A prepared context
is not proof that any byte was sent. UART transmission success is not a
drive acknowledgement, and a drive acknowledgement is not arrival.

If a write or trigger may have reached the drive but its effect cannot be
established, retain an outcome of unknown execution plus the last confirmed
phase and affected configuration/position assumptions. Later successful reads
can establish present state without proving whether a lost relative command
executed. Rebinding, counter reset, host recovery or cancelling local work
must not erase this result; retain its original axis, target and profile.

## Bounded operation sequences and stop priority

An optional `OperationContext` supports sequences shared by all consumers:
configuration reads, checked parameter staging, explicit trigger, handshake,
completion observation and documented refresh requirements. A profile may
complete a command in one transaction or require several. The context must
describe that difference rather than claim that one generic frame implements
every move.

`advanceOperation` consumes a caller-supplied event: response bytes,
transmission evidence, transport failure, observation, elapsed-time/deadline
event or local cancellation. It validates event correlation against operation,
target/profile and configuration generation. One call performs bounded work
and yields at most one transaction, a wait/service requirement, or an outcome.
It never spins, reads a clock, sleeps, drives GPIO or calls transport.

The call's validation result and the operation outcome are separate. An
invalid argument, malformed event envelope or event for another operation
rejects the call without changing the context or published result. A valid,
correlated timeout, cancellation, transport failure or received frame that
fails codec validation is an event to process: update the context and retained
failure/uncertainty result while preserving decoded payloads. Successful event
handling may therefore return call `Status::code == OK` while yielding a failed
or uncertain operation result. Preserve the underlying codec/transport error
in that result. Do not apply the unchanged-payload rule to suppress legitimate
failure-state transitions.

The caller retains transaction descriptors/buffers until the bus owner has
consumed them, supplies time with a documented wrap-safe domain, schedules
reads and deadlines, and reports missed service obligations. Profiles bound
setup step counts, frame sizes and temporary storage. Long-running motion,
polling and periodic refresh span calls and need application deadlines; they
must not become unbounded work inside one call. Capacity exhaustion is an
explicit error before a new side effect is requested.

Do not advance to a trigger after failed or uncertain prerequisite writes.
Retain partial-configuration evidence. Replay permission is defined for the
specific operation/phase; the sequencer cannot automatically retry a timed-out
motion command. Any policy-approved retry is explicitly driven by the
application and does not weaken uncertainty handling.

Only one ordinary operation occupies an axis initially. A stop is a separate
priority operation and must be admissible while movement, homing, jog or an
observation wait is active. Invalidate unsent continuation/trigger/refresh
work from the interrupted operation and retain its result separately. The
application resolves the current bus transaction to a safe framing boundary
and schedules stop next under its bus policy. Never transmit a second frame
in the middle of another device's transaction. A local wait cancellation
cannot by itself prove the motor stopped.

The stop request identifies controlled deceleration, profile-specific quick
stop or another supported behavior, and whether pending device commands must
be discarded or preserved. Distinguish drive release from stop. Reject an
unsupported requested combination instead of silently changing queue or torque
behavior. Stop has its own acknowledgement and observed-stop result; it does
not retroactively make an uncertain interrupted operation successful.
An unreachable drive and shared-bus contention prevent a universal stop-time
guarantee; no software API implies an independent hardware emergency stop.

## Acceptance checks when implemented

Native tests must verify exact integer/rational round trips, negative and
overflow boundaries, sign/origin transforms, gearing/travel conversion,
radian precision boundaries, every rounding policy, path/target limit checks,
wrapped-angle ties and complete-turn behavior. Missing, unresolved or
policy-unaccepted conversion metadata must reject a request without emitting
work. A valid, explicitly accepted operator assumption must retain its source
label and must not acquire a hardware-qualified label by passing these tests.

Exercise the same common requests through CLI parsing and direct API calls.
They must produce identical effective quantities, rejections and operation
steps. Use profile test doubles or documented frame vectors to cover partial
setup, trigger rejection, lost acknowledgements, invalid-event calls preserving
context, valid failure events advancing it, stale generation/events,
keepalive obligations, stop priority and uncertain results retained across
cancellation/rebinding. These checks qualify software contracts; they do not
replace motor/model/firmware hardware qualification.

Cover serial operation with external I/O unused, explicit terminal disabling,
and rejection of operations whose selected method needs missing signals.
Changing only the caller's wiring declaration must emit no device write;
disabled assignments must not be treated as unknown or active input functions.
