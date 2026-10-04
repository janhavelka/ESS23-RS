# ESS algorithm, encoder, current and lock settings

These are drive configuration operations, independently of commanded velocity,
position, current or torque. ESS has no reviewed serial commanded-current or
torque-control mode. Reading or changing a current setting does not implement
either motion capability.

The original ESS function-manual physical pages 77-78 and the
[register ledger](reference/ess_rs_registers.json) own the documented values.
The [coverage inventory](reference/ess_rs_operations.json) links the eight
register records to their independent read/write and hardware dispositions.
Generated enums and descriptors remain unchanged by this feature.

| Ledger record | Native setting and documented bounds | Remaining interpretation |
| --- | --- | --- |
| `CONTROL_ALGORITHM` | `ControlAlgorithm::OPEN_LOOP` (1), `ALGORITHM_1` (2) | Bench firmware returns 3, retained unknown; no invented third algorithm |
| `ENCODER_RESOLUTION` | Four times encoder lines, raw table range 0-65535 | Zero is retained on read but rejected as a configured mathematical scale; counts do not identify an encoder part or prove accuracy |
| `MAXIMUM_CURRENT` | Maximum effective current, mA, 0-5600 | Conflicting defaults 5600/2200 have no reviewed model mapping |
| `CLOSED_LOOP_CURRENT_PERCENT` | Native percent, 0-150 | Prose names a register outside the ESS map; denominator is unresolved without independent qualification |
| `BASE_CURRENT_PERCENT` | Native percent, 0-75 | Same denominator uncertainty; no invented base-versus-maximum ordering rule |
| `OPEN_LOOP_CURRENT_PERCENT` | Native percent, 0-100 | Same denominator uncertainty |
| `LOCK_CURRENT_PERCENT` | Native percent, 0-100 | Same denominator uncertainty; no invented lock-versus-open-loop ordering rule |
| `LOCK_TIME` | Delay after motion stops, ms, 0-20000 | Stored readback does not prove the active lock transition or torque effect |

The user-confirmed ESS23-RS20 and raw model/version association are application
evidence, not a universal wire model-code mapping. The model's peak-current dial
specification does not establish the safe ceiling of the maximum-effective-
current register. No other motor's current rating, undocumented peak-to-effective
conversion or conflicting default is substituted. Percent values are native
register units; the erroneous foreign-register reference is never exposed as an
ESS alias or silently reinterpreted as the maximum-current register.

## Public operation and lifetime

[ControlSettings.h](../include/MotorControlRS/profiles/ess_rs/ControlSettings.h)
exports `prepareControlRead`, `prepareControlSettings`, `getControl` and
`ControlObservation` for typed preparation and complete observation publication.
It reuses `DriverRequest`, `DriverPrerequisites`, `DriverContext`, `nextDriver`
and `advanceDriver`, with `DriverGroup::CONTROL_SETTINGS`; there is no second
sequence, application queue, transport owner or clock. Contexts and buffers are
caller-owned and fixed-size. Preparation copies the candidate and qualifications;
retained evidence copies frame bytes rather than borrowing a caller buffer.

Two four-word reads cover the eight documented fields without exceeding the
existing fifteen-byte retained response storage. Each selected write uses one
checked FC06 exchange and a separate checked FC03 readback. Eight selected fields
fit sixteen transactions within the existing eighteen-step settings capacity.
No consuming status field, save, restore or implicit enable/release is included.

Select `DriverField::CONTROL_ALGORITHM`, `CONFIGURED_ENCODER`,
`MAX_EFFECTIVE_CURRENT`, `CLOSED_MAX_CURRENT`, `CLOSED_BASE_CURRENT`,
`OPEN_MAX_CURRENT`, `LOCK_CURRENT` or `LOCK_DELAY` explicitly. Request members are
`controlAlgorithm`, `encoderResolution`, `maximumEffectiveCurrentMa`,
`closedMaximumPercent`, `closedBasePercent`, `openMaximumPercent`, `lockPercent`
and `lockDelayMs`. `ControlObservation` preserves all eight raw words, known-field
bits and two provenance blocks, with `algorithmKnown` and `encoderScaleUsable`
reported separately. `percentBaseKnown` and `activeSettingsKnown` remain false;
the read cannot resolve the manual's denominator conflict or activation timing.

The complete candidate must validate before yielding its first write. A current
candidate needs the exact independently identified model, raw identity/version,
target and configuration context, a model-specific effective-current ceiling
and qualified field semantics. Percentage validation uses the merged candidate
and retained settings only when its denominator is independently established;
integer arithmetic checks the resulting bound without overflow. Unknown active
mode, unresolved effective-current limits or unresolved denominator block the
affected write. They do not block raw reads or an independently qualified
lock-delay update. A stationary qualification is fresh, exact-target evidence,
not an inference from a successful identity read.

The current bound is checked at every intermediate tuple in the fixed field
write order before yielding any traffic. A safe final tuple cannot conceal an
unsafe intermediate mode/current combination, and no automatic write reordering
or rollback is promised.
An explicit closed-loop transition requires a nonzero encoder setting when the
algorithm word is written. A later selected encoder update cannot repair an
invalid intermediate transition; establish the usable setting first through a
separate explicit operation.

`DriverPrerequisites::controlIdentity` retains the checked identity context.
The application supplies `exactModelQualified`, its own nonzero `modelSourceId`,
`qualifiedModelCode` and `qualifiedFirmwareCode`, plus exact candidate effects
in `controlEffectsQualifiedFields`/`qualifiedControl` and qualified observation
bounds in `controlEarliestUs`/`controlLatestUs`. Current-dependent candidates also
require `nativeCurrentLimitQualified`, `maximumEffectiveLimitMa` and
`currentPercentBaseQualified`. These default to unavailable, not bench values.
The identity is checked against its raw frame, target and generation before use.

Candidate dependencies use documented meanings and independently supplied
qualifications. Numeric ordering such as base percent <= closed-loop percent or
lock percent <= open-loop percent is not asserted by the reviewed source and is
not invented. Changing an algorithm or feedback scale also requires qualification
of its consequences for the selected motor; merely knowing an enum is not enough.

Requested, acknowledged, observed stored and active values remain separate.
Cancellation is local; it does not restore settings or request a motor stop.
Partial application retains the selected-field progress, raw frames and possible
effects. Missing or malformed acknowledgements/readbacks remain uncertain. There
is no automatic replay, rollback, persistence claim or promotion of a checked
echo into physical completion.

## Effects and application policy

Accepted writes invalidate relevant configuration and state freshness, readiness,
prepared operations and coordinate confidence. Algorithm changes affect feedback
meaning and control mode. Encoder changes additionally invalidate the configured
encoder scale and origin relation. Current and lock changes affect torque-producing
state assumptions. Historical raw observations/results keep their original
target, configuration generation and evidence; they are never rewritten to match
a new setting. Later reads detecting an external change use the same invalidation
path. A fresh observation alone does not retroactively qualify an earlier write.

The standalone application owns exact-model evidence and the bench-specific
policy. Its ordinary read route preserves raw algorithm 3. Current and encoder
writes remain guarded until their independent prerequisites are available. Its
bounded stopped-state lock-delay policy may use an unconfirmed but checked,
on-time FC06-shaped frame followed by confirmed FC03 readback. Matching readback
settles only the observed stored word: acknowledgement remains false, the original
write execution remains unknown and activation/persistence remain unproved.
Failure does not trigger a second write.

The native profile route uses the same public candidate and sequence:

```text
profile ess_rs control read [address]
profile ess_rs control set FIELD INTEGER [FIELD INTEGER ...] [address]
```

Fields are `algorithm`, `encoder-resolution`, `maximum-effective-current`,
`closed-maximum-current`, `closed-base-current`, `open-maximum-current`,
`lock-current` and `lock-delay`. They use strict integer parsing with native
counts, mA, percent or ms. Unselected values do not change, and no startup/probe/read or
ordinary motion preparation writes these settings. The Python console preserves
command/operation correlation, checked raw evidence and explicit retention and
release. Its finite setting campaign never retries an uncertain write.
The existing console limit is 128 input bytes and 20 tokens; long field names
can limit a single CLI candidate before the public API's eight-field capacity.
Split explicit commands are separate non-atomic updates, each with its own fresh
baseline, qualifications and retained result. The API still supports all eight
fields as one validated candidate within fixed storage.
Every field is individually reachable through the CLI. Python rejects an
oversized candidate before writing a console line; it never splits or retries
one automatically. Full sixteen-step formatter fixtures test retained output
capacity, independently of console input admission.
Algorithm also accepts the named enum values `open-loop` and `algorithm-1`;
these select documented codes 1 and 2 through the same typed preparation.

## Qualification

Native source, range, model, stopped-state, stale-context, partial-application and
uncertainty tests pass through the real core and application SDK paths. The
twenty-one actual core/formatter fixtures include full sixteen-step output and
cancellation at every boundary; the largest current fixture is 2980/8192 bytes.
These are software evidence, independently of physical behavior.

The [current COM13 evidence](reports/ess_release_17_2026-10-04.json) reads all
eight fields as `[3,4000,5600,100,40,100,40,200]`. The stopped lock-delay update
200-to201 ms and explicit restoration to200 ms both have confirmed separate
readbacks. Every FC06 frame keeps acknowledgement false, response source
unconfirmed and execution unknown. Stored-value settlement passes; active lock
behavior, torque and persistence remain unqualified. In particular, the observed
effective-current code5600 does not resolve the model ceiling or justify a
peak-current conversion.

Algorithm, encoder and current candidates fail before traffic through nine
application gates. Their effects are not exercised merely to credit coverage;
physical mode switching, feedback changes, current increases, lock transition
timing and torque effects remain NOT RUN. The ending control baseline exactly
matches the original, with no motion or automatic replay. Detailed named-image
latency, errors, owner/capture gaps, resources and cleanup are in the report.

Configured encoder counts remain distinct from physical resolution and shaft
accuracy. Lock readback is not measurement of holding torque or transition timing.
No hardware setup assumption becomes a universal profile default. Larger retained
contexts/cache remain application storage in PSRAM on the reference ESP32-S3;
ISR/capture/driver state and stacks remain internal. Final ABI sizes and memory
watermarks belong to the named-image report.
