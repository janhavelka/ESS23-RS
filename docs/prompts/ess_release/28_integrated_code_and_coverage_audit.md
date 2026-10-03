# 28 — Integrated architecture, code and coverage audit

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 01–27 have recorded dispositions. This is a corrective audit of actual code and scenarios, not a documentation-only approval pass.

## Read and reuse

Read all current contracts, coverage rows and predecessor requirement tables. Trace core API to profile operations, sequences, owner admission/result, platform I/O, CLI and Python. Recheck the shared ownership contract and record platform/profile boundary violations.

## Audit and fix

- Check every roadmap/native-family obligation, enum/bit/indexed record and read/write/action path against implemented API, CLI, tests and physical evidence. Keep unresolved applicable operations in the denominator.
- Recheck ownership, request/result lifetimes, full-capacity urgent admission, deadline settlement, stop/cancel differences, generation invalidation and retained uncertain writes.
- Recheck exact units/origins/limits, actual-vs-commanded feedback, consuming reads, command echo vs acknowledgement and new-event completion.
- Find duplicated validation/dispatch/sequencing, stale fallback paths, unused abstractions, hidden framework dependencies and unbounded work. Refactor the smallest cohesive owner; do not create wrappers that merely rename existing complexity.
- Audit platform and manufacturer boundaries: no board pins, unrelated bus or
  product types in core/owner APIs; ESS details stay in the ESS profile. Confirm
  serial-only operation with unwired/disabled optional I/O, with exact device
  capability and per-operation prerequisites rather than a blanket I/O gate.
- Review memory placement, public documentation, package exports and error vocabulary. Preserve bounded request/result ownership without sensor-only assumptions, hidden retry policy or duplicate queue layers.
- Fix feasible scoped defects, add regressions that reproduce them and rerun affected native/firmware/hardware scenarios. A missing whole feature stays a named owning-prompt gap rather than a fictional complete audit.

## Verification and handoff

Use prompt 27's actual verifier, targeted direct-API/CLI parity cases and a short COM13 regression; run focused physical checks for runtime fixes. Record defects, causal evidence, removed redundancy, tests and remaining release blockers in the requirement table. No unresolved software defect becomes closed simply because unrelated tests pass.

## Subagents

Assign an independent end-to-end ownership/uncertainty reviewer and a coverage/simplicity reviewer. Reviewers must not be the sole authors of audited code. Re-review fixes, update contracts/prompts that changed, then commit/sync the corrected candidate for 29.
