# 11 — Velocity operation and verified ramp semantics

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 06 feedback, 07 units, 08 stop and 09 finite-motion evidence. Missing speed/ramp semantics block only the affected preparation or physical test.

## Read and reuse

Read original ESS speed/JOG/ramp/stop pages, the timing audit and axis velocity contract. Reuse the position sequence's event/result ownership and checked native parameter writers.

## Implement

- Resolve native signed-speed and ramp-time interpretation against the manual and exact firmware evidence. Keep native time parameters distinct from acceleration; a contradictory default is not a conversion formula.
- Add common/native velocity preparation and bounded start/observe/stop sequencing. Convert steps/s2, deg/s2, rad/s2 and rpm/s only where the drive's formula, starting speed, limits and quantization are established.
- Offer an explicit verified-device-configured ramp policy when appropriate; report unsupported conversion before writes instead of guessing. Treat zero target speed distinctly from a requested stop/release.
- Declare real service obligations and missed-service outcomes. Do not copy Leadshine's refresh requirement into ESS or advertise a communication-loss stop absent evidence.
- Separate serial signed-speed control from external JOG inputs. Unsupported jerk, blending, live target changes, torque/current modes fail clearly.
- Add matching CLI/API behavior and a finite Python velocity scenario whose normal/error path requests implemented stop and retains unknown stop outcome if communication fails.

## Verify

Native cases cover sign/range boundaries, acceleration conversions, unreachable ramps, latched-vs-live settings, start acknowledgement loss, stop during acceleration, stale feedback and delayed service. Physical tests use low speed, a finite host duration and an available stop path; record actual acceleration/stop behavior separately from echo timing. No endless velocity loop on host exit.

## Subagents and handoff

Assign a units/manual reviewer and a lifecycle/stop reviewer. Re-audit unsupported capability reporting and common/native reuse. Deliver verified ramp behavior and remaining uncertainty, then complete the common report and commit/sync workflow.
