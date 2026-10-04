# Exact host target preparation

[Axis.h](../include/MotorControlRS/Axis.h) adds pure preparation, host configuration and supplied reference evidence. It performs no I/O, allocation, clock access, motion or device settings change. An arithmetic preview is not an implemented or qualified motor command. The ESS command mapping, signed encoding, ramp semantics and completion remain later work.

`AxisConfig` contains the existing `UnitConfig`, exact target/binding and independent host generation, inclusive native range, optional native soft limits, caller-declared relative basis policy and separate command/encoder-zero relations. Unknown gear, lead, encoder or origins are legal until used. Scales retain their source; operator configuration stays `ASSUMED`. `AxisReference` supplies correlated generation, explicit idle/stationary and optional established native-coordinate evidence with observation time, current time and maximum age. The library checks those supplied facts; the application qualifies their source and observes the drive.

`PositionRequest` retains an exact `Rational` (signed 64-bit numerator, positive unsigned 64-bit denominator), spatial unit, frame, absolute/relative choice, explicit relative basis, generation and rounding/error policies. `preparePosition` publishes a complete `PreparedTarget` only on success. All rejected preparations/configuration/origin/parser calls preserve their output or current configuration. `Status::detail` is an `AxisError`; details `100 + UnitError` preserve disjoint shared unit failures. No error requires inspecting a message string.

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

Native absolute targets bypass host origin. Other absolute targets use a known host origin or the separate encoder-zero relation. Relative displacement never acquires an origin offset. ACTUAL, COMMANDED and QUEUED are distinct; unsupported policy bits fail explicitly. A supplied established reference can report an endpoint/displacement. Endpoint soft limits require fresh matching reference/basis; an unavailable limit check fails. Starting reference, requested endpoint and effective endpoint are checked against applicable limits; there is no clamping or automatic inward-recovery exception.

## Exactness, quantization and errors

Integer/rational conversions reuse the same private factor selection as `Units.cpp`. Bounded factor cancellation precedes multiplication; origin/endpoint addition and sign reversal are checked. Reduced products must fit unsigned 64-bit storage and signed integral portions must fit signed 64-bit storage. Some mathematically finite ratios exceed this fixed representation and return overflow rather than approximate. Native integers beyond binary64's exact integer range remain exact.

EXACT is the default and rejects fractional native results. NEAREST uses ties-to-even; TOWARD_ZERO, FLOOR and CEIL are explicit alternatives. Absolute rounding applies to the final native target after origin addition; relative rounding applies to displacement. Both requested and effective values are checked, so rounding cannot bypass a limit. Error allowances are finite, nonnegative native increments. Exact fractional errors compare directly with binary64 allowances using a bounded integer expansion; the console's rational policy allowance is converted conservatively toward zero, never widened by parsing.

`PreparedTarget.effectiveNative` is the authoritative absolute target or relative displacement. `requestedNative` is an exact mixed quantity: endpoint when known, otherwise displacement. Endpoint/displacement validity is separate; `zeroDisplacement` is meaningful only with `displacementKnown`. An absolute preview without current coordinate evidence does not claim an actual displacement. The result retains the original request and target/configuration generation. The caller retains the corresponding configuration/provenance rather than relabelling the result after replacement. Display rounding error is a signed binary64 value; the retained exact requested mixed quantity and effective integer preserve its exact mathematical relation.

Radians use explicit approximation and quantization allowances and retain their original rational input or supplied floating value. Pi is approximated by the shared documented tau constant; conservative binary64 arithmetic/error bounds exclude uncertainty in operator scales. NaN, infinity, underflow, excessive precision and ambiguous rounding/limit intervals reject. Approximate endpoint/origin arithmetic must fit the bounded binary64 precision contract; excessive or boundary-ambiguous cases reject. Approximate results expose `approximateRequestedNative` plus error bound and `exactArithmetic=false`; no truncated value is published as an exact requested fraction. Exact turns/degrees remain available.

Position, velocity and acceleration preferences remain independent `UnitSettings` fields. Existing `convertVelocity` and `convertAcceleration` support steps/s2, deg/s2, rad/s2 and rpm/s. No native drive ramp-time encoding follows from those conversions.

## Host changes and console

`configureAxis` validates a complete candidate against current binding/generation and fresh idle/stationary evidence, replaces it atomically and advances generation. Interpretation changes invalidate command/encoder origins and dependent soft limits. Origins cannot be smuggled through configuration replacement; `setAxisOrigin` additionally requires established native-coordinate evidence, advances generation and invalidates encoder relations/soft limits. A changed device interpretation or host recovery invalidates application host generation while retaining original raw observations. Generation exhaustion rejects reuse rather than wrapping. Application invalidation at exhaustion latches generation zero for the remaining context lifetime; a diagnostic configuration query can report zero, but preparation and changes reject it.

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
```

Round tokens are `exact`, `nearest`, `zero`, `floor`, `ceil`; omitted rounding/error is EXACT/0. Nonzero radians require a non-EXACT rounding policy and explicit positive radian error allowance. Strict numbers are ASCII signed integers, decimals with digits on both sides of the point, or integer/positive-denominator fractions. Exponents, hex, NaN, infinity, whitespace within a token, trailing junk and storage overflow reject. The public `parseExactNumber` is reused; no CLI conversion arithmetic exists. Line capacity is 128 bytes including terminator, token capacity 10 including correlation; overflow rejects the entire line. Output stays 4096 bytes with existing backpressure disposition.

The standalone starts with unknown scales/origins and no relative-basis policy. `relative-bases` is an operator-declared host arithmetic policy, not ESS wire capability or readiness. Fresh checked non-running/alarm-clear motion flags allow idle host configuration; cached model identity cannot provide that witness. Monitoring, bus activity, faults or stale motion evidence reject changes. The current ESS algorithm/source/sign uncertainty never becomes `nativeKnown`, even at raw zero: origin and reference-dependent soft-limit changes remain unavailable through this bench application. Host assumptions do not resolve that firmware evidence.

JSON keeps `bus_traffic:false`, `motion_command:false`, `wire_motion:"unimplemented"`, independent host and binding generations, original request and validity/error fields. Exact requested values use a mixed integer/fraction; approximate requests use `requested_native:null` and a separate approximate number. Enum ordinals: frames native/motor/load 0/1/2; relative bases actual/commanded/queued 0/1/2; rounding EXACT/NEAREST/TOWARD_ZERO/FLOOR/CEIL 0..4. The Python Console accepts bounded `host_args` for these two commands, preserves correlation and checks evidence without reimplementing conversion. Framing failure never causes replay.
