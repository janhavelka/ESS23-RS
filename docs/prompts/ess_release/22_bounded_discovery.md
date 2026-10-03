# 22 — Bounded discovery and minimal probe capabilities

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 05 typed identity, owner scheduling, 19 host tuple restoration and actual non-changing read-effect review. No additional manufacturer implementation is authorized.

## Read and reuse

Read the discovery contract in full, existing minimal model codec, profile capabilities and current CLI/harness. Reuse the smallest ESS query and typed identity refinement; keep register truth in its ledger.

## Implement

- Complete real `getDiscoveryCapabilities`/`prepareProbe` behavior and local profile/manufacturer inventory using existing operations. Unknown model remains a responder with explicit confidence.
- Add an application-owned cooperative scan with finite address candidates, supported host tuples, per-query/overall deadlines, request budget and result capacity. Default to selected ESS/current tuple.
- Preserve partial results and exact observed target/tuple/evidence. Checked exception, malformed frame, mismatch, timeout and identity ambiguity are different results.
- Yield to urgent stop and required service work. Reject scans that conflict with active motion or required serial configuration. Cancel unsent work and settle in-flight transport without erasing findings.
- Restore the original host tuple/selection before ordinary work resumes; a failed restoration leaves an explicit interlock. No automatic axis rebinding, scale inference, address reassignment or broadcast write.
- Add `profile list`, `probe`/`ping`, `read identity` and bounded `discover` parity. Manufacturer filtering expands only reviewed implemented profiles, not universal or mixed-protocol guesses.

## Verify

Native cases include budget/result exhaustion, duplicate/ambiguous responders, late/wrong replies, cancellation in each phase, priority, host restore failures and no consuming reads/writes. A timeout/recovery interlock cannot be skipped to continue scanning; stop with partial results unless a reviewed explicit read-only recovery policy establishes safe continuation. On COM13 scan a small explicit ESS address range and supported tuple set, restore known host settings, then probe normally. Do not claim one valid response excludes an address collision.

## Subagents and handoff

Assign a query-effects/identity reviewer and a scheduling/restoration reviewer. Audit all emitted frames and negative preconditions. Deliver scan limits, identity confidence and physical evidence to the final API/CLI audit; complete common report and sync.
