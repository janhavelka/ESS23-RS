# 17 — Algorithm, encoder, current and lock settings

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 05 exact-model/configuration provenance, 08 deliberate enable/release and 13 effects/invalidation rules.

## Read and reuse

Read the exact ESS algorithm, encoder, effective/max/base/open-loop/lock current and lock-time entries. Reuse generated enums and typed parameter infrastructure; do not edit generated types directly.

## Implement

- Expose documented algorithm/open-loop selection, configured encoder resolution, maximum effective current, closed-loop maximum/base, open-loop maximum and lock-current percentages plus lock delay.
- Validate exact-model limits, resolved units, legal ranges and stopped-state dependencies. Unspecified motor ratings or field semantics must not be replaced with another model's constants.
- Mark effects on control mode, feedback scale, readiness, torque-producing state and all prepared operations. Changing configured encoder resolution does not identify the encoder part or prove physical accuracy.
- Keep current configuration distinct from a common commanded-current or torque-control mode. Advertise those motion modes only with actual documented support.
- Add public typed reads/writes and matching native CLI routes. Use explicit native units where source semantics permit; retain unknowns otherwise.

## Verify

Native tests cover enum/range boundaries, wrong model, zero/invalid mathematical scales, cross-field limits, stale configuration and partial/uncertain updates. On the bench begin with readback. Write only justified reversible stopped-state values within the identified motor's limits and restore/read back originals; do not disable feedback or raise current merely to tick a coverage box. Record unrun physical effects.

## Subagents and handoff

Assign a model/current source reviewer and an invalidation/capability reviewer. Audit that hardware setup assumptions never become universal profile defaults. Deliver supported settings and qualification gaps to the coverage inventory; apply common review, report and commit/sync steps.
