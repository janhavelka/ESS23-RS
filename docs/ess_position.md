# Finite ESS positioning

The [short functional bench](functional_bench.md) adds an explicit bounded native
experiment and retains uncertain FC06 execution separately from drive-reported
activity/completion. Its evidence does not establish physical angle, calibrated
feedback, electrical timing or the full positioning family.

Prompt09 adds the installed common intent in
[MoveOperation.h](../include/MotorControlRS/MoveOperation.h) and the concrete ESS
sequence in [Position.h](../include/MotorControlRS/profiles/ess_rs/Position.h).
Prompt10 adds `prepareMoveAbsolute` and `prepareMoveAngle` beside
`prepareMoveRelative`. Common `MotorControlRS` and native `ESS_RS` routes invoke
the same implementation and reuse `nextMove`/`advanceMove`; there is one staging,
trigger and completion sequence. Construction and advancement perform no I/O, read no
clock and allocate nothing. Native tests are software evidence; physical moves
and dynamic stopping remain unqualified on the current bench.

`MoveRequest` contains the existing exact `PositionRequest`, positive native
motor `speedRpm`, and explicit `VERIFIED_CONFIGURED` ramp policy. Preparation
calls `preparePosition`; there is no CLI conversion path. Native relative
requests without endpoint limits need no unrelated origin, gearing or lead.
EXACT is the default; public requests retain the existing explicit rounding and
radian error policies. Zero effective displacement rejects without traffic.
When endpoint preparation consumes an established native reference, its age is
checked at the operation's supplied admission time. Its freshness must cover
the earlier readiness/operation write deadline. A caller can refresh the
reference or choose a smaller readiness/deadline budget; an old cached `nowUs`
cannot authorize a stale endpoint. Unestablished optional feedback remains
irrelevant to native displacement preparation without endpoint limits.

Finite absolute-move preparation preserves the complete target: `720 deg` and `2 turn`
mean two turns from the selected host origin. It requires a fresh stationary
actual-position reference whose command-coordinate relation is established.
The admitted `MoveContext` owns a copy of that reference. Native absolute steps
need no host origin; absolute motor/load engineering coordinates use the
configured host origin. Commanded and queued relative bases remain unsupported
by this ESS operation; their observability cannot be inferred from feedback.
The pure `preparePosition` arithmetic preview may calculate an ordinary
absolute endpoint without current-position evidence when its conversion and
limits need none. Such a preview does not establish displacement or authorize
the finite move, whose preparation requires the fresh actual reference.

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
only during `advanceMove`, with bounded nine-byte reply-prefix copies. The
application must correlate its retained owner transaction before supplying an
event and must independently qualify FC06 response source against local echo.

| Step | Yielded work and requirement |
| --- | --- |
| 0 | FC10 `0x0021/5`: acceleration, deceleration, RPM, target pair in retained word order |
| 1 | Only after checked, qualified, confirmed staging acknowledgement: FC06 `0x0027=0x0001` relative or `0x0005` absolute/wrapped, both finite and noninterrupting |
| 2 onward | Bounded FC03 `0x0006/2` observations, separated by waits which hold no bus transaction |
| Completion | Fresh post-trigger RUNNING report, then a later checked ARRIVED and not-RUNNING report without alarm/release/limit interruption |

The operation retains one immutable absolute deadline. Staging and trigger
transactions also retain the earlier readiness cutoff (saturating observation
time plus maximum age); queueing, UART setup and TX cannot renew it. New writes
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
acknowledgement. Old arrival, target equality, or a tiny move entirely missed
between polls does not establish completion. The retained activity and final
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
expiry, observed external movement and relevant settings changes also invalidate
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
profile ess_rs move-relative <value> <unit> <native|motor|load> <native_rpm> configured [address]
profile ess_rs move-absolute <value> <unit> <native|motor|load> <native_rpm> configured [address]
profile ess_rs move-angle <value> <turn|deg|rad> <motor|load> <path> <tie> <native_rpm> configured [address]
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

The standalone proposed free-shaft software ceiling is ±250 native increments of displacement,
at most60 RPM, a3-second absolute deadline and at most64 observations spaced
20ms apart. This is not a qualified physical test envelope. Production
`actionTimingQualified` remains false; command/sign/basis/ramp/input verification
flags also remain false. There is no CLI bypass or implicit motor commissioning.

Python `move-relative`, `move-absolute` and `move-angle` with explicit
`--cleanup-stop normal|direct` perform one attempt,
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
motion, equivalent-unit shaft comparisons and linear travel remain NOT RUN;
free-shaft arithmetic is not machine-travel qualification.
