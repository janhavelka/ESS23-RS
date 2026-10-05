# Move by an amount, or move to a target

The existing ESP32 example exposes ordinary C++ functions in
[`ProbeApp.h`](../examples/probe_cli/ProbeApp.h). They submit work to the same
owner used by the console. `serviceApplication()` sends the parameters, waits
for their checked acknowledgement, sends start, and polls motion status until
completion or a bounded failure. Do not add another UART reader or polling task.

```cpp
using namespace MotorControlRS;
using namespace MotorControlRSExample;

uint32_t operation = 0;

// Choose ONE when your button, network command or application requests a move:
Status accepted = moveBy(100, PositionUnit::STEPS, operation); // +100 command increments
// accepted = moveBy(36, PositionUnit::DEGREES, operation);    // +36 motor degrees
// accepted = moveBy(Rational(1, 10), PositionUnit::TURNS, operation);
// accepted = moveBy(Rational(1, 5), PositionUnit::MILLIMETRES, operation);
// accepted = moveTo(100, PositionUnit::STEPS, operation);     // absolute native target 100
// accepted = moveTo(36, PositionUnit::DEGREES, operation);    // requires host origin
```

`moveBy` means displacement from the current position. `moveTo` means an
absolute target; it does not normalize angles to one revolution. Steps mean
command increments, not full motor steps. Angular units use the motor shaft;
millimetres use configured load travel. Full steps and identified encoder counts
are also available when their scales are supplied. A missing scale/origin is an
error, never an assumed zero or guessed conversion.

The optional fourth argument is native motor RPM (default 60). Ramps come from
the example's checked motion-profile snapshot. Every move stages the selected
ramps, speed and converted target together. This is the ordinary observed move
path; the lower-level `ESS_RS::PositionCommand` remains available separately.

## Configure once, request moves, keep servicing

Supply host scales when starting the existing Arduino example. These are
explicit example assumptions; they do not change the motor's settings or prove
that a shaft moved the calculated angle:

```cpp
ApplicationOptions options;
options.positionUnits.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::ASSUMED);
options.positionUnits.fullStepsPerMotorTurn = UnitScale(200, 1, ScaleSource::ASSUMED);
// Only configure these for an actual machine with this gearing and lead:
// options.positionUnits.motorTurnsPerLoadTurn = UnitScale(1, 1, ScaleSource::ASSUMED);
// options.positionUnits.millimetresPerLoadTurn = UnitScale(2, 1, ScaleSource::ASSUMED);
beginApplication({Board::kRs485TxPin, Board::kRs485RxPin, Board::kRs485DeRePin,
                  Board::kRs485DeReActiveHigh},
                 Board::kRs485ReceiverDisabledDuringTransmit, options);
```

The existing example must first have checked configuration/state and a saved
motion profile. Its setup procedure remains `read config`, `read state`,
`motion-profile read`; these are non-changing reads. For a manual console session,
`axis config set command 1000` supplies the same host scale (then reread the
motion profile to bind the new host configuration). No timed refresh is needed
by default. A changed setting, recovery or other known invalidation still needs
reconciliation. This change does not add hidden enable, calibration or I/O writes.

The application loop can inspect progress without parsing console output:

```cpp
MoveProgress lastMove; // Keep the result your application needs.

void loop() {
    serviceApplication(); // Also performs the actual RTU status polls.
    if (!operation) return;

    MoveProgress progress;
    if (!moveProgress(operation, progress)) return;
    // progress.observationKnown/rawMotion/rawAlarm are the latest drive report.
    // progress.runningObserved means activity has been seen, not "running now".
    if (progress.pending) return; // Other application work can continue each turn.

    lastMove = progress;
    if (progress.completion == ActionCompletion::OBSERVED) {
        // This move reported new RUNNING followed by ARRIVED and not RUNNING.
    } else {
        // Report failure/uncertainty to the application; do not replay the move.
    }
    if (releaseMove(operation)) operation = 0; // Free result storage, not a stop.
}
```

Check the returned `Status` before treating a move as admitted. Rejection leaves
the output ID unchanged. The example retains completed programmatic results
until `releaseMove`; console output pressure cannot consume them. Invoke these
functions from the same task as `serviceApplication`, not directly from an ISR
or another task. Motion is never started automatically at boot by this example.

## Exact numbers, rounding and other frames

`Rational(1, 10)` means exactly 0.1. Integer values convert implicitly. The simple
functions require an exact representable target. Use `submitMove` for explicit
motor/load frames, rounding, radians or wrapped-angle policy, reusing the core
`PositionRequest` rather than adding another conversion function:

```cpp
AxisConfig coordinates;
positionConfiguration(coordinates);
PositionRequest target;
target.configurationGeneration = coordinates.generation;
target.value = Rational(6283185, 10000000); // approximately 0.6283185 radians
target.unit = PositionUnit::RADIANS;
target.frame = CoordinateFrame::MOTOR;
target.approximate = true;
target.rounding = Rounding::NEAREST;
target.maximumQuantizationError = 0.5; // command increments
target.maximumApproximationError = 0.01;
Status accepted = submitMove(target, 60, operation);
```

The standalone bench still limits displacement to 250 native increments and
speed to 60 RPM; it does not establish linear travel on a free shaft. Absolute
bench targets and starting feedback must lie in the existing 0..250 window.
Engineering absolute targets need the relevant origin; a raw position register
does not automatically establish it. Unsupported sign/basis/mode requests fail
before writes. The reusable core is not restricted to the example's bench range.
An unseen very short movement remains completion-unobserved rather than being
declared successful from an old arrival flag. Polling belongs to the upper
firmware and does not independently measure shaft motion.
