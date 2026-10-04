# Finite ESS serial velocity

Prompt11 implements signed serial velocity using the installed core in
`VelocityOperation.h` and `profiles/ess_rs/Velocity.h`. It reuses `Axis.cpp` exact
factor cancellation and quantization, checked ESS codecs and the existing
`ActionContext` stop. There is no clock, UART, allocation or retry in the core.

`prepareVelocityTarget(VelocityRequest, AxisConfig, PreparedVelocityTarget)`
prepares signed motor RPM with exact rational arithmetic and EXACT rounding by
default. Explicit nearest uses ties to even; zero, floor and ceil require an
error allowance. Radians require approximation and quantization allowances;
the full uncertainty interval must fit the range. Zero speed, including zero
after rounding, returns ZERO_SPEED and never substitutes for stop/release.
Native frame accepts native motor rpm; motor angles require no load gearing or
command subdivision; load rpm/angles require gearing, linear travel requires
lead, command increments require subdivision, identified encoder counts require
their source/basis/scale. Origins and position references are not rate scales.
Engineering command polarity applies; native RPM bypasses host polarity.

`ESS_RS::prepareVelocity` (also a common namespace wrapper) validates the entire
intent, target/configuration generation, caller-qualified signed RPM range,
negative encoding, native configured ramps, fresh enabled/stationary alarm-free
readiness, serial input effects, explicit stop policy and finite duration before
publishing a context. Configured ramp words are immutable snapshots. The public
quantity path checks signed16 range; ESS additionally checks requested and
rounded targets against the caller's qualified subset within -3000..3000.
Preparation outputs remain unchanged on error. Zero/custom acceleration ramp,
jerk, blending and live updates are not silently mapped. ACCELERATION policy
returns UNSUPPORTED: engineering `convertAcceleration` still supports steps/s2,
deg/s2, rad/s2 and rpm/s, but no qualified ESS formula translates these to time.
Torque/current and external-input JOG modes are unsupported console routes.

The caller owns `VelocityContext` for the whole operation and treats it as
read-only between calls. `nextVelocity` yields a bounded copied frame or wait;
`advanceVelocity` consumes borrowed event bytes only during that call and copies
bounded raw evidence. `serviceVelocity` advances supplied time only when no
transaction is admitted. Applications process captured completion first,
retain checked on-time closure even under delayed delivery, and settle physical
TX before changing phase/correlation. Wrong event bindings leave context intact.

The sequence writes the reviewed three-word velocity window, then START_SPEED
only after checked staging; bounded alarm/motion reads may observe fresh RUNNING.
The immutable `stopDueUs = admission + duration` includes setup. A wait releases
the bus while the same-axis write reservation remains. At the boundary, or an
error after possible start, the sequence yields the existing explicit stop using
urgent admission, then observes a new checked non-running report. RUNNING proves
neither target speed nor acceleration; a stopped report is not independent shaft
standstill. Normal success needs fresh activity and checked stop observation.

Stop eligibility serviced later than stopDueUs plus one supplied poll interval
records SERVICE_MISSED, even if a later checked stop succeeds. This is a host
service obligation, not an ESS refresh/watchdog or physical stop latency bound.
Urgent storage pressure, an in-flight transaction and recovery can delay or
prevent dispatch; the original overall deadline never renews. Stop expiry retains
`needsStop`, stop outcome and execution evidence separately. Faults cannot be
bypassed, automatically recovered or replayed to obtain success. A lost start
acknowledgement remains UNKNOWN even after a subsequent checked stop; primary
failure and cleanup failure remain separate. Local cancellation ends the local
sequence without sending stop, retaining needsStop/uncertainty. Explicit stop
preempts unsent continuations through the existing reserved stop path. Uncertain
staging reserves the physical address until reconciliation; result release and
host recovery do not clear that uncertainty.

The reference console uses one cooperative owner and caller-owned PSRAM records.
Eight ordinary operation records, one monitor and one explicit stop remain;
velocity does not add a task or a second UART. Terminal results are inspectable
until explicit release; backpressure retains complete results and correlation.

```
velocity 30 rpm native 200 configured normal
velocity 180 deg/s motor 200 configured direct
velocity 3 rad/s motor 200 configured normal round nearest 0.5 approx 0.000001
profile ess_rs velocity -30 rpm native 200 configured normal
```

Units: rpm, steps/s, fullsteps/s, counts/s, turns/s, deg/s, rad/s, mm/s.
Frames: native/motor/load. Optional `round exact|nearest|zero|floor|ceil ERROR`
and `approx ERROR` use strict exact-number parsing; optional final address is
1..247. This reference limits requested/effective speed to +/-60 native RPM and
duration to1..1000ms, with an additional immutable2s stop budget, 20ms observation
interval and64 polls. This is a proposed software envelope, not physical
qualification. Production qualification flags remain false and have no CLI
bypass. These commands reject before TX on the current bench.

The Python `velocity` scenario validates strict command/operation correlation,
finite duration and raw checked evidence. It shares the finite move scenario's
bounded cleanup: normal/error exit requests explicit stop for an admitted
operation. Ctrl+C during an idle wait preserves known framing, requests one
explicit stop and retains the original terminal even when it arrives during
cleanup. An interrupted serial read/write or framing/communication failure
retains UNKNOWN cleanup and sends no further commands. A rejected
unadmitted operation generates no cleanup write. No hidden retry or recovery is
used, including after host interrupt or framing loss. Software cannot stop an
autonomous motor after host/link loss; such a physical test needs an independent
bench stop mechanism.

Original function PDF pages17/18 document signed RPM, the three native words and
serial start; pages18/19 distinguish external JOG functions8/9/10. Page25 describes
normal stop using deceleration set before motion and direct stop without that
ramp. Page70 contradicts examples/defaults: speed120 labelled5rpm, ramp50
labelled100ms. Therefore no default ratio becomes a conversion formula. Exact
firmware0029 with model4EEA remains unqualified for negative wire encoding, RPM
factor, ramp factor/initial speed, physical acceleration and stopping. CURRENT_SPEED
raw readback is not decoded as actual RPM. No ESS communication-loss stop is
established. The user-confirmed ESS23-RS20/nominal1000PPR identity is recorded;
it does not resolve these firmware semantics.

See [implementation/evidence](reports/ess_release_11_2026-10-04.md) and the
[fresh independent audit](reports/ess_release_11_audit_2026-10-04.md). The native
console emits ten actual public-operation outcomes for a registered Python
parity check; synthetic terminal records do not substitute for that path.
