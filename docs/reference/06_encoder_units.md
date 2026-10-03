# ESS encoder evidence and the first unit-conversion block

The library can use explicit, labelled bench assumptions now. It does not need
an encoder chip identification before converting user-defined steps, angles
and rates. This first implementation supplies pure conversion and exact native
integer range checks; it does not send commands, establish absolute position,
set motor parameters, or implement drive acceleration registers.

## Evidence checked in the original documents

The table below uses physical PDF pages, counted from one. The listed original
pages were rendered and visually checked on 2026-10-02; the searchable text was
used only to locate them. Original files and hashes remain in
[sources.json](sources.json).

| Quantity or finding | Evidence and interpretation |
| --- | --- |
| Encoder resolution | [Function manual](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf), p77, ESS register `0x0101`: resolution is four times the encoder value; the default is a 1,000-line encoder and register default is 4,000. Its table range is 0-65535. Zero is not a usable mathematical scale even though it appears in the device table. |
| Command subdivision | Function manual p69, `0x0011`: default 1,000, range 400-51200. The English table says "Segment settings". Interpreting this as 1,000 command increments per motor revolution is the initial bench assumption; its active setting has not been read. |
| Position observation | Function manual p69, `0x000A/0x000B`: open-loop position is the commanded position; closed-loop position is encoder feedback converted into subdivision units. These registers must not be labelled raw encoder counts. |
| Full motor step | [RS20 datasheet](../vendor/ESS23-RS20_Full_Datasheet.pdf) p1 and [RS10 datasheet](../vendor/ESS23-RS10_Full_Datasheet.pdf) p1, drawings A4573/A4572 dated 2025-05-26: step angle 1.80 degrees. Therefore 360/1.8 = 200 full motor steps per revolution. |
| Hardware identity | [Hardware manual](../vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf), p3, explicitly covers ESS23-RS10 and RS20. Its overview/electrical information on pp4-5 and the two model datasheets do not identify an encoder manufacturer, chip/assembly part number or encoder electrical interface. |
| Operating mode | Function manual p77, `0x0100`: default 2 is the listed closed-loop algorithm; value 1 is open loop. This is a documented default, not a readback of the bench motor. |
| Speed and ramp distinction | Function manual p70 lists speed in r/min and acceleration/deceleration times in ms, with conflicting default-column annotations. Pure steps/s², deg/s², rad/s² and rpm/s conversions do not establish a mapping to those drive ramp-time fields. |

No exact encoder part number, index-channel behavior, absolute/multiturn
capability, sensing technology or electrical interface is established by this
reference pack. The four-times-resolution statement establishes the documented
count multiplier; it is not a reason to guess an encoder IC or exposed A/B/Z
connector. The hardware manual's external X/Y terminals are programmable drive
I/O, not evidence of a raw encoder port.

The nominal increments implied by the working configuration are 1.8 degrees
per full motor step, 0.36 degrees per command increment and 0.09 degrees per
decoded encoder count. These describe scale/resolution, not measured shaft
accuracy. The 4,000-count encoder scale does not replace the 1,000-increment
command/position scale, and raw encoder feedback availability is a separate
profile question.

## Explicit free-shaft bench configuration

[Defaults.h](../../include/MotorControlRS/profiles/ess_rs/Defaults.h) supplies
`MotorControlRS::ESS_RS::makeBenchUnitConfig()`:

| Setting | Initial value | Retained source |
| --- | --- | --- |
| Command increments per motor turn | 1000/1 | `ASSUMED`: documented subdivision default interpreted as increments/revolution and assumed active |
| Motor turns per load turn | 1/1 | `ASSUMED`: direct free-shaft bench configuration |
| Full steps per motor turn | 200/1 | `DOCUMENTED`: derived from the RS10/RS20 step-angle specifications |
| Encoder decoded counts per motor turn | 4000/1 | `ASSUMED`: documented default assumed active |
| Encoder source | Local source ID 1, motor-turn basis | Application identifier for the selected motor encoder; not a device register value |
| Command and encoder polarity | +1 | Initial host direction convention; physical direction has not been checked |
| Millimetres per load turn | Unknown | No lead or linear mechanism is assumed |
| Preferred position / velocity / acceleration | steps / steps per second / steps per second squared | Independent caller-editable preferences |

The returned configuration is a value owned by the caller. Constructing,
editing or validating it does not write subdivision, direction, encoder
resolution or electronic gearing to a drive. Readback can replace assumptions
and their source labels later; changing a label alone does not prove that a
scale is correct. The existing [bench record](../hardware_bench.md) reports
the motor was left at defaults but is not configuration-readback evidence.

## Implemented API and dimensional rules

[Units.h](../../include/MotorControlRS/Units.h) exposes:

- `validateUnitConfig` for mathematical/configuration validation.
- `convertDisplacement` for signed spatial quantities with no origin or angle
  wrapping. Full turns remain full turns.
- `convertVelocity` and `convertAcceleration` for signed derivatives with
  explicit time denominators.
- `validateNativePosition` for exact signed 64-bit values and caller-supplied
  inclusive profile limits; `narrowNativePosition` for a checked signed 32-bit
  field. These functions never convert the native integer through `double`.

Supported spatial units are command steps, motor full steps, selected-source
encoder counts, load turns, degrees, radians and configured millimetres.
Positive rational scales retain numerator, denominator and provenance.
Motor/load gearing is motor turns per load turn. A single selected encoder
may use motor-turn, load-turn or millimetre basis, with its own polarity;
the appropriate gear/lead is applied only for that basis. Additional encoder
sources can be represented by separate caller-owned configurations.

Command steps follow `commandPolarity`; encoder counts follow the selected
encoder's polarity. Angles, travel and full-step displacements use positive
load-axis direction. Motor full-step conversion includes the gear ratio when
converting to load rotation. Encoder-count displacement conversion needs a
known scale and identified source. Absolute encoder/reference mapping is
outside this displacement-only implementation.

`VelocityUnit` has a spatial unit and one time denominator.
`AccelerationUnit` has a spatial unit and two independently specified time
denominators. For example:

| Requested quantity | Representation | Equivalent at the bench scale |
| --- | --- | --- |
| 360 degrees/s² | degrees, second, second | 1000 steps/s² |
| 2 pi radians/s² | radians, second, second | Approximately 1000 steps/s² |
| 60 rpm/s | turns, minute, second | 1000 steps/s² |
| 3600 turns/min² | turns, minute, minute | 1000 steps/s² |
| 1 step/ms² | steps, millisecond, millisecond | 1,000,000 steps/s² |

In particular, rpm/s divides the rotational numerator by 60, while turns/min²
divides by 3600. The helpers convert signed acceleration; a later motion
preparer separately checks the nonnegative ramp magnitude required by a drive.

Position, velocity and acceleration preferences are independently editable.
They are explicit inputs to calls, not a hidden mode or automatic drive setup:

```cpp
using namespace MotorControlRS;
UnitConfig config = ESS_RS::makeBenchUnitConfig();
config.settings.position = PositionUnit::DEGREES;
config.settings.velocity = VelocityUnit(PositionUnit::TURNS, TimeUnit::MINUTE);
config.settings.acceleration = AccelerationUnit(PositionUnit::RADIANS);

UnitConversion displacement;
Status result = convertDisplacement(90.0, config.settings.position,
                                    PositionUnit::STEPS, config, displacement);
// If result.isOk(), displacement.value is 250. This sends nothing.

UnitConversion acceleration;
result = convertAcceleration(6.283185307179586, config.settings.acceleration,
                             AccelerationUnit(PositionUnit::STEPS), config,
                             acceleration);
// If successful, approximately 1000 steps/s^2, with an arithmetic error bound.
```

## Numeric scope of this block

Engineering conversions accept `double` values and return `UnitConversion`
with the value and a conservative absolute arithmetic-error bound in the
destination unit. They preserve signed values and do not quantize a result
into a motion command. Input/output magnitudes are limited to 2^53, and the
error bound must fit the caller's `maxConversionError` (default 0.000001 in
the destination unit). The bound describes the calculation on the supplied
numbers; it does not cover inaccurate scale assumptions or a value already
rounded by the caller/parser.

The calculation uses bounded rational scale fields, a stated numerical pi
constant, and at most a fixed number of floating operations. It conservatively
uses binary64 error accounting even if the host's `long double` has greater
precision. Nonfinite values, overflow/precision excess and arithmetic
underflow are rejected. Exact same-unit passthrough performs no arithmetic and
reports zero arithmetic error. Missing scales reject only calculations that
depend on them; for example, degrees-to-turns needs no motor subdivision.
Motor steps-to-full-steps or motor-encoder conversion also needs no load
gearing. A linear encoder's counts-to-millimetres conversion needs its own
scale but no screw lead. Gear ratio and lead are required only when crossing
the corresponding coordinate bases.

Bounded spatial and time factors are combined before multiplying the input
value. This avoids an intermediate subnormal rounding error for very small
inputs when the final result is representable. Regression checks also use a
compiler mode where `long double` has binary64 precision.

Keep raw native signed 64-bit positions as integers. Do not pass them through
the engineering `double` API to narrow them; use `validateNativePosition` and
`narrowNativePosition`. Conversion of engineering quantities into rounded
integer motion targets, exact rational quantity input, origins, reference
frames, limits and wrapped-angle path selection remain later axis work. This
module does not claim to implement those planned axis operations.

Every fallible conversion/narrowing preserves its output on error. Validation
returns the shared `Status` with a structured `UnitError` in `detail`. All
messages are static; the module uses no heap, UART, platform framework, clock,
logging or persistent storage. Native tests exercise dimensions, gear/encoder
bases, signs, configuration failures, precision limits and exact integer
boundaries. Software tests are separate from unperformed motor qualification.

## Current configuration readback (2026-10-04)

[Prompt 05](../reports/ess_release_05_2026-10-04.md) read the configuration through the typed public API on COM13. The raw subdivision is 1000 and configured encoder resolution is 4000. These supersede the earlier statement that the active raw subdivision had not been read; its physical command-unit interpretation remains SCALE_UNRESOLVED. The defaults helper deliberately remains labelled ASSUMED. `getConfig` supplies only the nonzero configured encoder scale as READBACK metadata, with remaining unit fields unknown. No encoder IC/manufacturer, physical resolution, shaft accuracy or external encoder interface has been identified.

The observed algorithm code is 3, outside the reviewed documented 1/2 enum. It is retained raw with `algorithmKnown=false`; neither a guessed third algorithm nor motion readiness is inferred. Model 0x4EEA and firmware raw0x0029 remain unmapped. Missing label/firmware identification and input-level evidence remain explicit motion prerequisites. No motor setting changed.
