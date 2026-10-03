# 23 — Complete public API and CLI coverage

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 05–22 have recorded implementation and qualification dispositions. This step audits and closes integration gaps; it must not hide an unimplemented native family behind generic register access.

## Read and reuse

Read the full axis/profile/CLI/discovery contracts and operation coverage inventory. Trace every public operation to its implementation, console entry, help text and test. Reuse passive status/diagnostic conventions from the existing console.

## Work

- Reconcile all common modes and native families with one bounded command inventory reused for dispatch/help/effect descriptions where practical. Keep short consistent names and exact numeric parsing.
- Ensure every implemented typed device operation is callable directly and through the CLI with identical validation, units, prerequisites and effects.
- Complete implemented host selection/configuration, capabilities, cached status/health, explicit checks, diagnostics/trace, result retrieval, cancel, optional polling, reset and recover. Add flush only with its explicit idle-only host semantics.
- Preserve response correlation, bounded progress/terminal output, target/configuration generations, unknown bits and per-field freshness. Status/result inspection is non-consuming.
- Expose unwired/disabled input disposition and documented typed disable
  commands consistently in API/CLI. Operations needing an unavailable input
  fail before TX; serial operations with no such dependency remain usable.
- Keep unsupported, unresolved and unimplemented capabilities separate. ESS current settings are not torque/current motion support; external-trigger segments do not gain a serial start.
- Remove redundant dispatch/conversion/register paths. Do not introduce a second CLI or split implementations by framework.
- Update the complete coverage matrix. Substantial missing family behavior returns to its owning prompt as a named gap; do not mark this prompt or the release fully complete by narrowing the denominator silently.

## Verify

Test help/dispatch/API inventory parity, strict argument counts/ranges, large integers, unsupported requests producing no TX, common/native equivalence, cached commands producing no reads, generation changes, stop responsiveness under console load and retained uncertain results after reset/recover. Run a short hardware regression plus newly reachable features within their qualified envelope.

## Subagents and handoff

Assign an independent API/CLI inventory reviewer and a state/side-effect reviewer. Audit contradictions in docs against actual code. Deliver the final command/result contract for 24–26; complete common fixes, evidence and commit/sync.
