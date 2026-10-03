# 14 — Homing methods and reference establishment

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 08 stop, 09 motion evidence, 12 pair policy and 13 configuration/limits. Physical homing requires each selected method's actual sensors/index/mechanics.

## Read and reuse

Read the original ESS method diagrams, search/return speed, offset, limit and collision-homing sections. Reuse operation sequencing, state reads, native ramps and host/device coordinate separation.

## Implement

- Build an explicit per-method support/prerequisite table from documented ESS methods. Distinguish configured direction, search/return rates, acceleration/deceleration, auxiliary flags, offset and reference/completion evidence.
- Prepare and execute supported methods with bounded caller-owned state. No method is available simply because its numeric enum exists.
- Use already verified sensor/limit input configuration. Methods needing new input assignments remain pending prompt 15; do not duplicate its I/O setters here. Record the dependent admission and fixture checks for 15 to revisit.
- Reuse reviewed pair writes for offsets; unresolved fields fail before staging. Retain collision-parameter conflicts rather than aliasing addresses.
- Establish origin/reference only on sufficiently fresh correlated completion evidence. Old homed flags, a start echo or interrupted search cannot establish a new reference.
- Preserve stop preemption, timeout/unknown execution, failed switch transitions and explicit external-input requirements. Device-position clear and host origin are separate existing actions.
- Add method-specific CLI help/capabilities and finite Python scenarios for only the available fixture methods.

## Verify

Native tests cover all documented method descriptors, absent inputs/no TX, stale flags, search/return transitions, partial configuration, missing edges, offset boundaries, alarm/stop and lost acknowledgements. On the free-shaft bench run only methods whose prerequisites are present; do not invent switches or attempt every method mechanically. Record zero reference and return behavior with independent observations where possible.

## Subagents and handoff

Assign a diagram/prerequisite reviewer and a state/reference reviewer. Audit unsupported versus unimplemented method reasons, remove duplicated sequence code and document actual qualification. Complete the common audit and commit/sync workflow.
