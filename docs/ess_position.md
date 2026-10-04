# Finite ESS relative positioning

Prompt09 adds the installed common intent in
[MoveOperation.h](../include/MotorControlRS/MoveOperation.h) and the concrete ESS
sequence in [Position.h](../include/MotorControlRS/profiles/ess_rs/Position.h).
`MotorControlRS::prepareMoveRelative` and `ESS_RS::prepareMoveRelative` invoke
the same implementation. Construction and advancement perform no I/O, read no
clock and allocate nothing. Native tests are software evidence; physical moves
and dynamic stopping remain unqualified on the current bench.

`MoveRequest` contains the existing exact `PositionRequest`, positive native
motor `speedRpm`, and explicit `VERIFIED_CONFIGURED` ramp policy. Preparation
calls `preparePosition`; there is no CLI conversion path. Native relative
requests without endpoint limits need no unrelated origin, gearing or lead.
EXACT is the default; public requests retain the existing explicit rounding and
radian error policies. Zero effective displacement rejects without traffic.

`MovePrerequisites` binds the exact target and host configuration generation.
The caller must supply qualified command-unit/actual-relative-basis semantics,
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
| 1 | Only after checked, qualified, confirmed staging acknowledgement: FC06 `0x0027=0x0001`, finite relative and noninterrupting |
| 2 onward | Bounded FC03 `0x0006/2` observations, separated by waits which hold no bus transaction |
| Completion | Fresh post-trigger RUNNING report, then a later checked ARRIVED and not-RUNNING report without alarm/release/limit interruption |

Every transaction uses one immutable absolute operation deadline. On-time final
closure may be delivered later; a late framing gap cannot become success.
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

CLI grammar is:

```text
move relative <value> <unit> <native|motor|load> <native_rpm> configured [address]
profile ess_rs move-relative <value> <unit> <native|motor|load> <native_rpm> configured [address]
```

Numbers use `parseExactNumber`, with explicit frames and EXACT quantization.
These routes use the same common/native API and active host generation. Nine
ordinary correlations plus one stop correlation remain bounded; moves use the
eight ordinary application result slots, with explicit `result`/`release`.
Accepted records use command `move-relative`, followed by one `type=move`
terminal. Context lookup is non-consuming. Maximum output remains 4096 bytes.

The standalone proposed free-shaft software ceiling is ±250 native increments,
at most60 RPM, a3-second absolute deadline and at most64 observations spaced
20ms apart. This is not a qualified physical test envelope. Production
`actionTimingQualified` remains false; command/sign/basis/ramp/input verification
flags also remain false. There is no CLI bypass or implicit motor commissioning.

Python `move-relative ... --cleanup-stop normal|direct` performs one attempt,
retained inspection, explicit cleanup stop, checked final state/health, and
local result releases. A malformed session becomes unusable and cannot replay
the move. Cleanup evidence remains unknown when commands cannot be delivered;
reported non-running does not supply missing independent shaft observation.
See [the prompt09 report](reports/ess_release_09_2026-10-04.md) for tests,
actual image/bench evidence and outstanding physical gates.
