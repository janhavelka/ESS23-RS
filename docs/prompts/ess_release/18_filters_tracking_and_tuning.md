# 18 — Filters, tracking and tuning parameters

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: typed native parameter infrastructure, exact-model context and 13/17 configuration-effect rules.

## Read and reuse

Read original ESS filter, deviation/arrival, current-loop, LA and collision-return entries and their unresolved ledger annotations. Reuse bounded descriptors and indexed node helpers.

## Implement

- Cover I/O filtering, pulse low-pass/mean filters, position-deviation threshold, arrival/error window and arrival time.
- Cover current-loop Kp multiplier/Kp/Ki/Kc, LA speed Kp/Kv stages/nodes, feedforward Kvf and position Ki, plus documented collision-return threshold/current fields.
- Expose reviewed reads and typed writes with exact native ranges/effects. Unknown physical scaling must not produce invented engineering units.
- Preserve conflicting/unspecified access at 0x003B/0x003C and later collision locations. Do not alias them, read forbidden words or bypass codec policy to close coverage.
- Invalidate completion/readiness assumptions when arrival/deviation semantics change. Preserve settings snapshots and partial-write uncertainty.
- Use the same typed operations from CLI; organize related parameters for readability without an unchecked bare-address setter.

## Verify

Native tests cover every resolved descriptor, invalid values/masks, grouped candidates, boundary indexes, changed completion thresholds and failures during updates. Hardware readback is the baseline. Any tuning perturbation must have a specific finite experiment, bounded values and a restore plan; broad gain sweeps or deliberate instability are outside this step. Record values not safely testable on the current fixture as unqualified.

## Subagents and handoff

Assign a full-ledger coverage reviewer and a tuning-effects reviewer. Audit missing parameters, duplicated validation and claims of physical units. Deliver coverage dispositions rather than fabricated completeness, then finish the common audit/report/commit workflow.
