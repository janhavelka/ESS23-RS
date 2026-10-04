# Short unattended functional bench

The user's 2026-10-04 direction permits short unattended tests on the secured,
uncoupled ESS23-RS20. A person at the bench and a logic analyzer are not admission
requirements for these tests. Drive reports establish functional observations;
they do not establish independent shaft displacement or electrical timing.
The several-hour soak is explicitly omitted.

Build/upload the explicit `bench_s3_functional` environment. Ordinary builds keep
their qualification gates. This image does nothing to the bus at startup and
admits only enable, release, explicit stops and the narrow finite position
experiment. It does not enable continuous velocity, homing, position clear,
persistence or communication changes. Existing inputs, algorithm and ramp words
are preserved. Read the current bench report before reusing any command.

`motion-bench read` saves the original six words at `0x0020..0x0025` and refreshes
the current configuration/endpoint binding. `motion-bench inspect` shows retained
evidence without bus traffic. `motion-bench restore` explicitly restores the five
saved writable profile words with the reviewed FC10 window and verifies all six
words by FC03. It never triggers motion or automatically retries a write.
The saved original survives subsequent reads but not a host restart: restore
and archive it before changing firmware.

The app requires fresh stopped/alarm-free reports, zero reported speed for the
return, known word order, subdivision1000, disabled software limits, inactive
assigned inputs and the inspected start/ramp values. Individual positive native
moves are at most250 increments, speed at most60 in the documented native speed
field, with a three-second operation bound. Physical angle, encoder scaling and
ramp-time calibration remain unresolved for the observed algorithm value3.

The only unreferenced absolute experiment is the literal native target zero.
`ESS_RS::MovePrerequisites::nativeZeroEnvelopeVerified` explicitly admits it
without a supplied reference, host origins or soft limits. The app requires a
fresh positive raw position no greater than250 and zero reported speed. The
result keeps `reference.native_known=false` and `displacement_known=false`;
it does not turn raw feedback into calibrated command coordinates. All other
absolute/angle preparations retain their established-reference requirement.

`ActionOptions::allowUnconfirmedWriteObservation` defaults false. The functional
image explicitly opts in for these selected operations. A fully transmitted,
timely, checked FC06-shaped frame may be followed by checked read observations,
while its execution remains UNKNOWN and `responseConfirmed` remains false.
The option never accepts bad CRC, short transmission, ambiguous timing or an
invalid event envelope. FC10 staging still requires confirmed response evidence.

Prepare each phase with `python scripts/bench_functional_motion.py --phase PHASE
--out build/bench/NEW_PREFIX --plan-only`, then run it without `--plan-only`.
Supported phases are inspect, actions, forward, return, stop-normal, stop-direct,
restore and status. Execute phases individually and inspect their evidence before
the next physical action. The tool reserves new JSON/JSONL files before opening
the port, retains every read and write outcome, and never replays a motion write.
After an accepted failed move it may send one preplanned direct stop if no stop
was already attempted and console framing is usable.

ARRIVED/RUNNING and raw speed are separate reports. Completion of the position
sequence does not promise immediate zero speed or exact feedback-target equality.
The host observes up to ten fresh state reads, 50ms apart, to establish reported
zero speed; it preserves intermediate nonzero reports. It fails if that bound is
exhausted. Dynamic-stop tests require a new RUNNING report before sending stop,
retain the interrupted result and separately check stopped/zero-speed reports.
No host-measured interval is advertised as electrical or physical stop latency.
