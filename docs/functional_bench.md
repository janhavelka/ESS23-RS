# Short unattended motion checks

Supported operations use the regular library API and ordinary firmware. There is
no separate motor execution path or analyzer admission flag. Runtime debug
mode groups raw/decoded observation around those same operations.
The user owns correct wiring/electrical installation; firmware owns commands,
sequencing, framing, software timing and retained outcomes. HIL diagnostics and
independent physical measurements are separate from ordinary operation.

Build/upload `bench_s3_load_timer` for the existing COM13 setup; the probe and
polling builds expose the same library and console operations. Startup sends no
motor commands. The short test procedure uses finite positioning, enable/release
and normal/direct stop; it does not start continuous velocity, home the motor,
change inputs, save settings or restart the drive. The several-hour soak remains
omitted by user request.

`motion-profile read` saves the six native words at `0x0020..0x0025` and refreshes
current endpoint/configuration binding. `motion-profile inspect` reads retained
state without bus traffic. `motion-profile restore` explicitly restores the five
saved writable words through the typed `ESS_RS::PositionProfile` builder and
verifies all six by readback. It never triggers motion or retries a write.
Original values survive subsequent reads, but not a host restart; restore and
archive them before changing firmware.

The regular API accepts native relative displacements without an unrelated host
origin. An ordinary unwrapped absolute target needs a current-position reference
only when conversion, path selection or applicable limits need it. Without that
reference its endpoint can be known while displacement remains unknown; raw
feedback is not silently promoted to calibrated command coordinates. The example
retains its small free-shaft limits; those are not library-wide motion limits.
Unknown algorithms, negative wire encoding and physical-unit assumptions remain
explicit metadata, rather than a reason to add a second execution path.

The board configuration declares combined DE/~RE with its receiver disabled
while transmitting. The runner rejects bytes before physical TX completion,
DE release and the configured response gap. Under this user-supplied wiring
contract, a checked FC06 response is an acknowledgement; it still is not proof
of completion. Applications without confirmed response provenance may supply
`responseConfirmed=false`: actions and finite positioning retain UNKNOWN
execution while following a fully transmitted, valid, timely FC06 frame with
read-only observations. Short TX, invalid frames and timing ambiguity still fail.

Prepare a phase with `python scripts/bench_motion.py --phase PHASE --debug decoded
--out build/bench/NEW_PREFIX --plan-only`, then run without `--plan-only`.
Phases are inspect, actions, forward, absolute, return, stop-normal, stop-direct, restore
and status. Inspect each outcome before the next physical action. The tool
reserves new evidence files, records all traffic and never replays a motion
write. After an accepted failed move it may send one prepared direct stop if
no stop was already attempted and the console remains usable.

ARRIVED/RUNNING and raw speed remain separate reports. The host observes at most
ten fresh state reads, 50ms apart, for zero reported speed and retains transient
values. Moving-stop tests require new RUNNING evidence before stop, retain the
interrupted result and separately verify stopped/zero-speed reports. Software
intervals are not independent physical stop-latency measurements.

[Passive traffic observation](traffic.md) works during these operations and
ordinary application traffic. Raw and decoded displays consume only diagnostic
copies; display pressure cannot consume protocol bytes or delay the bus owner.
The earlier [functional campaign](reports/functional_motion_2026-10-04.md) remains
historical evidence for the superseded image, not instructions to rebuild it.
