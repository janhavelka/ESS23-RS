# Finite ESS positioning

For a user-facing ESP32 function, see [moveBy / moveTo](move_example.md).
It submits the same observed move sequence; no console-string parsing is needed.

The regular API and firmware implement these operations without a functional
mode. [Short motion checks](functional_bench.md) use the same preparations as
applications. Activity/completion reports remain separate from acknowledgement,
calibrated feedback and independent electrical/shaft measurement.

Prompt09 adds the installed common intent in
[MoveOperation.h](../include/MotorControlRS/MoveOperation.h) and the concrete ESS
sequence in [Position.h](../include/MotorControlRS/profiles/ess_rs/Position.h).
Prompt10 adds `prepareMoveAbsolute` and `prepareMoveAngle` beside
`prepareMoveRelative`. Common `MotorControlRS` and native `ESS_RS` routes invoke
the same implementation and reuse `nextMove`/`advanceMove`; there is one staging,
trigger and completion sequence. Construction and advancement perform no I/O, read no
clock and allocate nothing. Short bench tests establish drive-reported activity
and stopping for the recorded subset; independent shaft timing remains unmeasured.

## Native commands and remembered intent

`ESS_RS::PositionCommand` holds caller intent: endpoint/binding, word order,
desired native RPM and raw acceleration/deceleration words. Each preparation
copies those settings and the supplied exact 32-bit target bits into a separate
`MoveContext`. Later edits affect future commands only.

```cpp
#include <MotorControlRS/profiles/ess_rs/Position.h>
namespace ESS = MotorControlRS::ESS_RS;

ESS::PositionCommand motor;
motor.target.id = 1;
motor.target.address = 1;
motor.target.generation = 1; // Application's current endpoint binding.
motor.speedRpm = 60;
motor.accelerationTime = motor.decelerationTime = 100; // Native ramp words.

ESS::MoveContext operation;
const auto status = motor.prepareRelative(operation, 7, 100, 1000, 1001000);
// On success, execute nextMove/advanceMove through the existing owner.
// prepareAbsolute has the same arguments and preserves the supplied target bits.
```

This native route does not require prior state observations, host origin or
engineering-unit conversion. By default it stages `0x0021/5`, then yields the
relative/absolute start only after checked, confirmed staging acknowledgement.
Both native intent and observed moves expose `MoveSetup`: `WRITE_ALL` retains
that default, `VERIFY_AND_UPDATE` compares a new `0x0021/5` read and writes only
a changed scalar (FC06), target pair (`0x0024/2`) or full block for mixed changes;
`USE_STORED` explicitly relies on existing drive settings and sends start only.
No read failure yields a write. No failed write yields start or a retry.
See [repeat usage](move_example.md#repeating-a-move) and the
[measured comparison](reports/2026-10-05_repeat_motion_timing.md).
The application uses its existing owner and holds the same-axis reservation
across verification, setup and start, preserving deadlines, uncertainty and explicit stop handling.
Successful start ends with outcome `ACKNOWLEDGED`
and completion `NOT_OBSERVED`; it does not poll for completion without a prior
stationary baseline. Use separate typed state reads for feedback or the
observation-aware preparations below for correlated completion.

Native target bits do not qualify negative-motion encoding, calibration or
starting-speed compatibility. There is no implicit enable, I/O change, save,
stop or retry. `buildStartPosition(address, relative, buffer, capacity)` builds
only the eight-byte FC06 start for the drive's already stored target/profile;
it does not stage parameters or observe the result. These lower-level routes
are API-only. The CLI continues to use `prepareMove*`.

## Observation-aware preparation

Here “fresh” means correctly bound, checked and not invalidated; age is checked
only when explicitly enabled. `maximumAgeUs=0` disables age expiry by default.
The standalone application's matching option is `observationMaxAgeMs=0`;
set it to `5000` to opt into a five-second policy. Missing/contradicted evidence,
future timestamps and generation mismatches still reject. Retained evidence
does not establish uninterrupted motor power or unchanged physical state.

`MoveRequest` contains the existing exact `PositionRequest`, positive native
motor `speedRpm`, and explicit `VERIFIED_CONFIGURED` ramp policy. Preparation
calls `preparePosition`; there is no CLI conversion path. Native relative
requests without endpoint limits need no unrelated origin, gearing or lead.
EXACT is the default; public requests retain the existing explicit rounding and
radian error policies. Zero effective displacement rejects without traffic.
When endpoint preparation consumes an established native reference, an enabled
age policy is checked at the operation's supplied admission time and must cover
the earlier readiness/operation write deadline. A caller can refresh the
reference or choose a smaller readiness/deadline budget; an old cached `nowUs`
cannot authorize a stale endpoint. Unestablished optional feedback remains
irrelevant to native displacement preparation without endpoint limits.

Finite absolute-move preparation preserves the complete target: `720 deg` and `2 turn`
mean two turns from the selected host origin. Native absolute steps need no host
origin or current-position reference when conversion and applicable limits need
neither. The endpoint is known while displacement remains unknown in that case.
A supplied established reference must be fresh and stationary; the admitted
context retains it. Engineering coordinates still require their applicable
origin/scales, and wrapped paths require a fresh native reference. Commanded and
queued relative bases remain unsupported by this ESS operation.

Wrapped preparation sets `PositionRequest::wrapped` and uses the same public
`preparePosition` arithmetic. It accepts motor/load turns, degrees or radians,
a known host origin and fresh multi-turn native reference. `AnglePath` selects
POSITIVE, NEGATIVE or SHORTEST; the exact half-turn tie defaults to REJECT, with
explicit POSITIVE/NEGATIVE alternatives. Matching orientation produces zero for
every path, and the mover rejects that zero without traffic. Limits are checked
on the chosen requested/effective target; failure never chooses another turn.
Radians retain explicit approximation/error bounds and reject unresolved path
or quantization intervals. Ordinary absolute and relative inputs are never
normalized as orientations.

`MovePrerequisites` binds the exact target and host configuration generation.
The caller must supply qualified command-unit semantics (and actual-relative-
basis semantics for relative moves),
word order, native starting-speed relationship, configured ramp words, fresh
enabled/stationary/alarm-free readiness and permission from the actual input
and drive-limit configuration. Plain raw readback is insufficient for unresolved
firmware semantics. Starting speed must be known in compatible native RPM units
and no greater than selected speed; ambiguous raw `0x0020` is not promoted.
The reviewed speed/ramp ranges are 1..3000 RPM and 0..2000 raw ramp values.
These ramp words are reused verbatim, never labelled physical acceleration.
The implemented mathematical target subset is signed32; negative targets also
require explicit qualified two's-complement encoding. A caller must establish
applicable device bounds; the malformed manual range is not qualification.

Serial positioning needs no home switch or output load. Unconnected inputs
remain assigned inputs. An affected limit, stop, release or external trigger
must be accounted for by qualification; this operation changes no assignments,
polarity, soft limits or saved settings. Required I/O changes remain for 15.

The caller owns `MoveContext`, retains it read-only between API calls, and feeds
copied transaction evidence through `ActionEvent`. Preparation snapshots the
request, effective target and prerequisites. Borrowed frame memory is consumed
only during `advanceMove`, with bounded nine-byte reply-prefix copies and a
separate full fifteen-byte verification-read frame. The
application must correlate its retained owner transaction before supplying an
event and must independently qualify FC06 response source against local echo.

Default `WRITE_ALL` tokens:

| Step | Yielded work and requirement |
| --- | --- |
| 0 | FC10 `0x0021/5`: acceleration, deceleration, RPM, target pair in retained word order |
| 1 | Only after checked, qualified, confirmed staging acknowledgement: FC06 `0x0027=0x0001` relative or `0x0005` absolute/wrapped, both finite and noninterrupting |
| 2 onward | Bounded FC03 `0x0006/2` observations (`0x0006/7` with qualified feedback), separated by waits which hold no bus transaction |
| Completion | Fresh RUNNING then stopped/ARRIVED without faults, or two exact stopped endpoint reports under the qualified feedback policy below |

`VERIFY_AND_UPDATE` uses token 0 for the read, optional token 1 for its selected
write, token 2 for start and token 3 onward for observations. `USE_STORED` begins
at token 1, with no setup evidence. `triggerStep` records the selected boundary;
`setupOffset`/`setupCount` describe the actual write (zero count means skipped).
`verificationKnown`/words describe checked readback, separately from desired
words and any acknowledged write. Failed verification publishes no decoded words.

The operation retains one immutable absolute deadline. With age expiry enabled,
staging and trigger transactions also retain the earlier readiness cutoff
(saturating observation time plus maximum age); queueing, UART setup and TX
cannot renew it. With age expiry disabled, writes use the operation deadline. New writes
require time strictly before that cutoff. Qualified physical closure at the
cutoff can be accepted when delivered later, but delayed staging delivery at or
after it cannot authorize a trigger. An expired deferred write settles locally;
accepted setup bytes retain uncertainty. Observation reads use the original
operation deadline. On-time final closure may be delivered later; a late
framing gap cannot become success.
Repeated `nextMove` returns the same token; the application admits it once.
Wrong target/generation/operation/step or malformed event envelopes preserve the
context. Correlated bad frames, cancellation and transport/deadline failures
consume a terminal event and retain original diagnostics. No phase retries.

`setupExecution`, `stagingApplied`, trigger `execution`, `completion` and
`uncertain` have separate meanings. FC10 atomic application is undocumented:
any accepted setup bytes on terminal failure conservatively retain possibly
changed parameters, even when the trigger was not sent. A trigger echo is only
acknowledgement. Old arrival or unqualified target equality alone does not
establish completion. The default policy requires the RUNNING transition; the
qualified feedback alternative below can confirm a move missed between polls. The retained activity and final
evidence prove checked drive reports, not exact internal sample times or
independently measured shaft motion.

The standalone application extends the existing action service loop and axis
reservation; it adds no scheduler or second queue. Admission starts on a
quiescent bus so an earlier producer's queued write cannot cross staging.
Unrelated reads/other-target work may join afterward. Other cooperative ordinary
writers use `admitAxisWrite`; stale target bindings reject before admission,
and accepted independent writes invalidate affected observations and the current
axis's configuration/qualifications. Reservation rejects same-axis target, speed
and configuration writes between staging and trigger. Host configuration rejects
while reserved. Changed configuration readback invalidates move continuations,
including input assignments/polarity and soft/over-limit settings. An admitted
priority stop cancels only unsent continuation work and settles in-flight TX.
Interrupted and stop results remain separate under output/result pressure.
Uncertainty survives result release and host recovery; no queued trigger resumes.

The application drops current native-reference confidence when trigger TX is
accepted. A terminal triggered move invalidates dependent origins/limits until
a newly qualified reference is supplied; arrival flags alone cannot establish
its exact endpoint. Release, accepted or uncertain device clear, reference
expiry when enabled, observed external movement and relevant settings changes also invalidate
coordinate knowledge while retaining historical raw evidence. Host `axis origin`
changes no motor counter and requires idle stationary native reference evidence.
The current board cannot establish that reference from unsigned raw feedback
or an old zero readback.

Host preference and limit edits preserve unchanged coordinate interpretation
through `configureAxis`'s optional retained-reference output, including its
original observation time and age limit. Actual interpretation changes clear
that reference. A same-axis reservation does not hide observed external motion:
only an active move with accepted trigger bytes suppresses its own movement
invalidation until terminal settlement.

CLI grammar is:

```text
move relative <value> <unit> <native|motor|load> <native_rpm> configured [address]
move absolute <value> <unit> <native|motor|load> <native_rpm> configured [address]
move angle <value> <turn|deg|rad> <motor|load> <positive|negative|shortest> <reject|positive|negative> <native_rpm> configured [address]
```

All units (`steps`, `fullsteps`, `counts`, `turn`, `deg`, `rad`, `mm`) share public
preparation for relative/absolute requests, rejecting missing scales only when
needed. Relative commands may add `basis actual|commanded|queued`; unsupported
bases fail before work. Every move may add `round exact|nearest|zero|floor|ceil
<maximum_native_error>` and radians additionally `approx <maximum_native_error>`
before the optional address. Numbers use `parseExactNumber`, explicit frames
and EXACT quantization by default.
These routes use the same common/native API and active host generation. Nine
ordinary correlations plus one stop correlation remain bounded; moves use the
eight ordinary application result slots, with explicit `result`/`release`.
Accepted records use `move-relative`, `move-absolute` or `move-angle`, followed
by one `type=move` terminal. Requested path/tie, resolved target/displacement,
quantization and copied reference provenance are retained. Context lookup is
non-consuming. Maximum output is 4608 bytes, including bounded reference and
numerical provenance.

The standalone free-shaft software ceiling remains250 native increments of
relative displacement, at most60 in the native speed field, a3-second deadline
and64 observations spaced20ms apart. Unreferenced native absolute targets0..250
are admitted only from fresh stopped, zero-speed raw feedback within0..250;
displacement stays unknown. This example envelope does not limit the library.
Read `motion-profile` and current state first. Configuration, active inputs,
negative encoding and conversion prerequisites remain real checks; there is no
analyzer admission flag or implicit motor commissioning.

Python `move-relative`, `move-absolute` and `move-angle` with explicit
`--cleanup-stop normal|fast` perform one attempt,
retained inspection, explicit cleanup stop, checked final state/health, and
local result releases. A malformed session becomes unusable and cannot replay
the move. An interrupted acceptance/result wait also makes the session unusable,
including interruption before the move's accepted record arrives. Cleanup
evidence remains unknown when commands cannot be delivered;
reported non-running does not supply missing independent shaft observation.
See [the fresh prompt09 audit](reports/ess_release_09_audit_2026-10-04.md) and
[implementation report](reports/ess_release_09_2026-10-04.md) for tests,
actual image/bench evidence and outstanding physical gates.
Prompt10 software and current read-only/gate evidence are recorded separately in
[its report](reports/ess_release_10_2026-10-04.md). Physical absolute/wrapped
motion had not run in that historical image; the later short campaign records
native absolute returns. Equivalent-unit shaft comparisons and linear travel
remain NOT RUN;
free-shaft arithmetic is not machine-travel qualification.

## Exact final-position completion

A caller may set `MovePrerequisites::positionFeedbackMatchesCommand` only when
feedback scale, sign and coordinate relation are established for the selected
configuration. Supply a stationary actual-position `AxisReference`; admission
checks its binding, freshness and changed signed-32-bit endpoint. Ordinary native
relative requests still need no unrelated origin/reference.

The same sequencer reads the reviewed seven-word alarm/motion/I/O/position/speed
window. If RUNNING was missed, an acknowledged trigger followed by two consecutive
checked reports of the exact prepared endpoint, ARRIVED, not-RUNNING, zero speed
and no fault establishes completion. A mismatch resets the first witness. No
arbitrary displacement, tolerance, lost trigger acknowledgement or persistent
RUNNING is accepted. The usual RUNNING path also requires zero reported speed
when feedback is available. The window is not documented as an atomic internal
snapshot; the two reports do not claim exact drive sample timing.

`positionConfirmed`, `observedPosition`, `observedSpeed` and
`positionMatchEvidence` preserve this proof. `ActionEvidence` retains up to19
bytes, covering the full reply. JSON exposes the policy, confirmation and both
reports; Python checks CRC, correlation, ordering and endpoint agreement.
Historical nine-byte evidence remains readable. The public default stays off.
Ordinary `moveby`/`moveto` and C++ `moveBy`/`moveTo` enable it after fresh
preparatory reads under the standalone session's existing labelled coordinate
convention. Advanced CLI moves retain their existing observation policy.

The [firmware0x0029 investigation](reports/2026-10-07_position_completion.md)
records short-move success and remaining high-subdivision failures. Exact
matching leaves ambiguous/quantized endpoints unconfirmed; no encoder tolerance
or universal firmware qualification is invented.
