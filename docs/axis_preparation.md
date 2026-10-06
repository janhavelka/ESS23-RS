# Exact host target preparation

Observation age expiry is optional: `AxisReference::maximumAgeUs=0` disables
elapsed-age rejection by default. Positive budgets enable the age checks
described below. Required evidence, binding/generation, future timestamps and
known invalidation remain checked; elapsed time never proves continuous power.

[Axis.h](../include/MotorControlRS/Axis.h) provides pure preparation, host configuration and supplied reference evidence. It performs no I/O, allocation, clock access, motion or device settings change. An arithmetic preview does not execute a motor command. The separate [ESS position operation](ess_position.md) now stages and triggers finite relative, absolute and wrapped-angle requests through the same preparation; its electrical, sign, reference and ramp qualifications remain explicit prerequisites.

`AxisConfig` contains the existing `UnitConfig`, exact target/binding and independent host generation, inclusive native range, optional native soft limits, caller-declared relative basis policy and separate command/encoder-zero relations. Unknown gear, lead, encoder or origins are legal until used. Scales retain their source; operator configuration stays `ASSUMED`. `AxisReference` supplies correlated generation, explicit idle/stationary and optional established native-coordinate evidence with observation time, current time and maximum age. The library checks those supplied facts; the application qualifies their source and observes the drive.

`PositionRequest` retains an exact `Rational` (signed 64-bit numerator, positive unsigned 64-bit denominator), spatial unit, frame, absolute/relative choice, explicit relative basis, generation and rounding/error policies. `wrapped`, `AnglePath` and `HalfTurnTie` explicitly distinguish orientation selection from ordinary multi-turn position. `preparePosition` publishes a complete `PreparedTarget` only on success. Rejected preparations/configuration/origin/parser calls preserve their output or current configuration; explicit confidence invalidation has the separately documented fail-safe effect below. `Status::detail` is an `AxisError`; details `100 + UnitError` preserve disjoint shared unit failures. No error requires inspecting a message string.

## Frames and metadata

| Frame/unit | Interpretation and requirements |
| --- | --- |
| Native steps | Raw signed command increment coordinate. Relative requests need supported basis policy and range, with no unrelated origin/gear/lead/reference. |
| Motor turns/degrees/radians | Explicit motor rotation; command scale needed, load gear not needed. |
| Load turns/degrees/radians | Load rotation; command scale and motor/load gear needed. |
| Motor/load steps | Host positive-axis command increments; command polarity applies, no gearing or turn scale needed. |
| Full steps | Established full-step and command scales; gearing only when crossing motor/load bases. |
| Encoder counts | Identified source, count scale, polarity and its motor/load/linear basis; gear/lead only across the actual conversion path. Absolute counts additionally require the separately established encoder-zero/command relation. |
| Millimetres | Load travel; command, gear and lead needed when converting to command coordinates. No machine travel is inferred from free-shaft rotation. |

Native absolute targets bypass host origin. Other absolute targets use a known host origin or the separate encoder-zero relation. Relative displacement never acquires an origin offset. ACTUAL, COMMANDED and QUEUED are distinct; unsupported policy bits fail explicitly. A supplied established reference can report an endpoint/displacement. Endpoint soft limits require fresh matching reference/basis; an unavailable limit check fails. Requested and effective relative displacement must satisfy native range independently of the endpoint. Starting reference, requested endpoint and effective endpoint are also checked against applicable limits; there is no clamping or automatic inward-recovery exception.

## Absolute positions and wrapped orientations

Ordinary absolute preparation preserves all turns: `720 deg` means two selected-frame turns from the host origin. `wrapped=true` instead requires an absolute angular request in an explicit MOTOR or LOAD frame, a known host origin and fresh multi-turn native evidence whose basis matches the request. Native-frame, linear and relative wrapped requests reject before arithmetic. A single-turn/raw unsigned feedback value does not establish that reference.

`POSITIVE` and `NEGATIVE` follow the selected frame's positive direction, including command polarity. `SHORTEST` chooses the smaller displacement; an exact half-turn rejects by default or follows an explicit `HalfTurnTie::POSITIVE`/`NEGATIVE`. An already matching orientation produces zero displacement for every path. An extra revolution requires an ordinary relative or multi-turn absolute request. The selected requested and quantized endpoints, starting point and applicable limits are checked; a forbidden path never switches revolution or direction. Pure preview reports a displacement quantized to zero; the finite ESS executor rejects it without yielding a write.

Exact wrapped turns/degrees normalize before scale multiplication while retaining the original rational input. Rational native-per-turn scales are supported using bounded modular arithmetic; native references beyond binary64 precision remain exact. Reduced denominator/period products must fit fixed unsigned 64-bit storage. Radians use the same explicit approximation policy and interval quantizer as ordinary preparation. Intervals crossing orientation equality or a shortest half-turn reject as ambiguous, even when a tie direction was supplied; exact turns/degrees can express those decisions without numeric uncertainty.

Pure absolute preview can compute an endpoint without a current reference; it then cannot claim displacement. `prepareMoveAbsolute` also permits an unwrapped absolute target without a current-position reference when conversion and applicable limit checks need none. Native steps bypass host origin; engineering coordinates still require their established origin and scales. `prepareMoveAngle` requires established stationary ACTUAL command coordinates. Any reference supplied to either executor must be stationary ACTUAL evidence. The executor copies a consumed reference, checks its age at supplied admission time and requires its freshness to cover the immutable readiness-capped staging/trigger budgets. Later observation requests retain the overall operation deadline. A timely trigger does not become invalid merely because its starting-reference age expires during motion; application host confidence is invalidated independently, while the admitted context keeps its original evidence.

## Exactness, quantization and errors

Integer/rational conversions reuse the same private factor selection as `Units.cpp`. Bounded factor cancellation precedes multiplication; origin/endpoint addition and sign reversal are checked. Reduced products must fit unsigned 64-bit storage and signed integral portions must fit signed 64-bit storage. Some mathematically finite ratios exceed this fixed representation and return overflow rather than approximate. Native integers beyond binary64's exact integer range remain exact.

EXACT is the default and rejects fractional native results. NEAREST uses ties-to-even; TOWARD_ZERO, FLOOR and CEIL are explicit alternatives. Absolute rounding applies to the final native target after origin addition; relative rounding applies to displacement. Both requested and effective values are checked, so rounding cannot bypass a limit. Error allowances are finite, nonnegative native increments. Exact fractional errors compare directly with binary64 allowances using a bounded integer expansion; the console's rational policy allowance is converted conservatively toward zero, never widened by parsing.

`PreparedTarget.effectiveNative` is the authoritative absolute target or relative displacement. `requestedNative` is an exact mixed quantity: endpoint when known, otherwise displacement. Endpoint/displacement validity is separate; `zeroDisplacement` is meaningful only with `displacementKnown`. An absolute preview without current coordinate evidence does not claim an actual displacement. The result retains the original request and target/configuration generation. The caller retains the corresponding configuration/provenance rather than relabelling the result after replacement. Display rounding error is a signed binary64 value; the retained exact requested mixed quantity and effective integer preserve its exact mathematical relation.

Nonzero radians use explicit approximation and quantization allowances and retain their original rational input or supplied floating value. Pi is approximated by the shared documented tau constant; conservative binary64 arithmetic/error bounds exclude uncertainty in operator scales. NaN, infinity, conversion underflow, excessive precision and ambiguous rounding/limit intervals reject. A normal converted displacement cancelling an origin onto zero is valid and retains its approximation bound. Approximate endpoint/origin arithmetic must fit the bounded binary64 precision contract; excessive or boundary-ambiguous cases reject. Approximate results expose `approximateRequestedNative` plus error bound and `exactArithmetic=false`; no truncated value is published as an exact requested fraction. Zero radians use exact integer origin/endpoint arithmetic with zero numerical error and `exactArithmetic=true`; no approximation allowance is needed. Ordinary frame, scale, source, origin, basis and limit checks still apply. Exact turns/degrees remain available.

Position, velocity and acceleration preferences remain independent `UnitSettings` fields. Existing `convertVelocity` and `convertAcceleration` support steps/s2, deg/s2, rad/s2 and rpm/s. No native drive ramp-time encoding follows from those conversions.

## Host changes and console

`configureAxis` validates a complete candidate against current binding/generation and fresh idle/stationary evidence, replaces it atomically and advances generation. Interpretation changes invalidate command/encoder origins and dependent soft limits. Origins cannot be smuggled through configuration replacement; `setAxisOrigin` additionally requires established native-coordinate evidence, advances generation and invalidates encoder relations/soft limits. A changed device interpretation or host recovery invalidates application host generation while retaining original raw observations. Generation exhaustion rejects reuse rather than wrapping. Application invalidation at exhaustion latches generation zero for the remaining context lifetime; a diagnostic configuration query can report zero, but preparation and changes reject it.

`invalidateAxisReference(config, reference)` handles caller-established loss, release, device clear or interpretation changes without traffic. It clears command/encoder origins, derived soft limits and supplied native/stationary/idle confidence while preserving scales and unit preferences. It advances generation normally. At `UINT32_MAX` or an already disabled generation zero, it returns `GENERATION_EXHAUSTED` and still clears confidence; it never wraps or revives a disabled axis. The application rejects dependent prepared work and retains historical observations/results under their original generations. ESS clear is a separate explicit zero-only device action, not a host-origin change or an arbitrary counter write.

`configureAxis(current, candidate, evidence, &retainedReference)` optionally
publishes the reference under the new generation when coordinate interpretation
is unchanged. Preferences, limit edits and idempotent declarations preserve its
original observation time, age limit and provenance; they do not refresh it.
Interpretation changes clear the reference along with origins and derived
limits. The output may alias `evidence`; failed calls leave both outputs
unchanged. The console uses this same decision, so changing a rate preference
cannot disable later reference-expiry invalidation.

The existing console exposes synchronous, correlated host replies without reserving bus operation IDs:

```text
axis config
axis config set command|gear|fullsteps|lead|encoder-scale <positive-rational|none>
axis config set encoder-id <uint32>
axis config set encoder-basis motor|load|linear
axis config set polarity|encoder-polarity -1|1
axis config set relative-bases <0..7>
axis config set position-unit steps|fullsteps|counts|turn|deg|rad|mm
axis config set velocity-unit <unit>/s|rpm
axis config set acceleration-unit <unit>/s2|rpm/s
axis config set native-limits <minimum> <maximum>
axis config set soft-limits <minimum> <maximum>|none
axis origin <exact-native-integer>
prepare absolute <value> <unit> native|motor|load [round [quant-error [radian-error]]]
prepare relative <value> <unit> native|motor|load actual|commanded|queued [round [quant-error [radian-error]]]
prepare angle <value> turn|deg|rad motor|load positive|negative|shortest reject|positive|negative [round [quant-error [radian-error]]]
```

Round tokens are `exact`, `nearest`, `zero`, `floor`, `ceil`; omitted rounding/error is EXACT/0. Nonzero radians require a non-EXACT rounding policy and explicit positive radian error allowance. Strict numbers are ASCII signed integers, decimals with digits on both sides of the point, or integer/positive-denominator fractions. Exponents, hex, NaN, infinity, whitespace within a token, trailing junk and storage overflow reject. The public `parseExactNumber` is reused; no CLI conversion arithmetic exists. Line capacity is 128 bytes including terminator, token capacity 20 including correlation; overflow rejects the entire line. Output is bounded at 4608 bytes with existing backpressure disposition. Host `axis origin` takes an exact native integer and never changes drive counters. Actual `move relative|absolute|angle` and profile routes use the separate shared ESS executor and its admission prerequisites.

The core requires supplied scale/reference evidence. The standalone convenience
workflow uses the explicit ASSUMED 1000 command steps/turn convention and establishes
a RAM-only session zero at the first checked stationary preparation; it performs
no counter clear or NVS write. Idle sample expiry and motion-profile restoration
preserve this fixed offset while requiring fresh current-position evidence.
Release, external movement or coordinate interpretation changes invalidate it.
`relative-bases` is a host arithmetic policy, not ESS wire capability. Monitoring,
bus activity, faults and missing stationary evidence still reject host changes.
The standalone convention does not qualify physical scale, signed encoding or
homing/limit semantics. See [the current console policy](console.md).

Preview JSON keeps `bus_traffic:false`, `motion_command:false`, `wire_motion:"not_requested"`, independent host and binding generations, original request and validity/error fields. Exact requested values use a mixed integer/fraction; approximate requests use `requested_native:null` and a separate approximate number. Enum ordinals: frames native/motor/load 0/1/2; relative bases actual/commanded/queued 0/1/2; rounding EXACT/NEAREST/TOWARD_ZERO/FLOOR/CEIL 0..4; angle paths POSITIVE/NEGATIVE/SHORTEST 0/1/2 and half-turn ties REJECT/POSITIVE/NEGATIVE 0/1/2. The Python Console preserves correlation and checks evidence without reimplementing conversion. Framing failure never causes replay.
