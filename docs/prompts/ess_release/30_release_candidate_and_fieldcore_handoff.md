# 30 — Release candidate and FieldCore integration handoff

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 27's clean package verification, 28's corrected code/coverage and 29's exact qualification disposition. All remaining gaps must be visible.

Consume [29's measured qualification matrix and open blockers](../../reports/ess_release_29_2026-10-05.md).
Its finite Arduino/native-IDF results do not close deferred multi-hour endurance,
missing physical fault fixtures, electrical/shaft measurements or the nine named
native-family gaps. Preserve the original failed Arduino aggregate separately
from its diagnosed prerequisite correction; no broad release PASS follows from
the1,760 checked frames or green software verifier.

## Read and reuse

Read the complete roadmap and operation/platform coverage, current package metadata, reports and actual FieldCore integration counterparts. Source paths/line numbers in earlier reports are inspection aids, not guarantees that FieldCore stayed unchanged.

## Deliver

- Prepare a reproducible release candidate with consistent version, public API docs, getting-started examples, CLI inventory, troubleshooting, supported models/firmware/platforms and known operating limits.
- Reconcile every release gate with evidence. A partial candidate must say which applicable native operations or physical cases are missing; it cannot claim complete ESS coverage or industrial certification.
- Verify clean library consumers and shipped source/package artifacts. Record hashes, commands, exact candidate commit and available test/CI results. Preserve vendor licensing/provenance and follow the repository rename guide for canonical metadata and the active Git remote.
- Write a concise FieldCore handoff mapping final motor API/sequence requests, IDs/results, cancellation/stop, freshness/health, memory and timing to its current module/owner/backend boundaries.
- Name the necessary FieldCore changes: frame capacity, FC06 echo policy, exception framing, physical timing evidence, typed motion outcomes, scheduling/stop and no uncertain replay. Preserve its other sensor/framing modes in the proposed regression plan.
- Map onto its existing owner, preserving VibWire ASCII/suffix framing alongside RTU rather than replacing the owner with `RtuRunner`. Specify regressions for mixed device/tuple traffic, per-request validator/context, same-axis operation exclusion, retained recovery outcomes and stale queued work after recovery.
- Distinguish normal per-transaction UART baud selection from logical motor reconfiguration and reference invalidation. Record C++11 core and current FieldCore consumer-build compatibility without aliasing firmware-owned types into the library.
- Target a future FieldCore motor device through its RS485 task. No such device implementation is assumed to exist. Select its board and application wiring during later integration; the current standalone bench is not its product specification. Exclude unrelated buses and product composition from the handoff.
- Leave the standalone repository independently usable. Do not edit FieldCore or create a second bus owner there.

## Final review and disposition

Run the actual release verifier and final hardware smoke on any changed runtime image. Check coverage claims against evidence, ensure a known final bench state and confirm no queued/autonomous motion. Update the roadmap to completed, qualified or still open per row.

This prompt prepares the candidate and handoff; publishing a tag/GitHub release is a separate explicitly dispatched action after the candidate is reviewable. Do not move tags or imply release publication merely because commits were pushed.

## Subagents and handoff

Assign an independent package/release-claims reviewer and a FieldCore source-compatibility reviewer. Fix contradictions and feasible gaps, record unresolved blockers without changing scope to hide them, then commit/push and verify synchronization. Deliver candidate status, artifact/evidence links and the separately scoped integration/publication work.
