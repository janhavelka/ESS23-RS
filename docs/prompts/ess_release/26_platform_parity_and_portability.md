# 26 — Qualify platform parity and document portability

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 25's actual IDF firmware and 24's reusable scenarios. Use only already qualified motor behaviors and actual available hardware fixtures.

## Read and reuse

Read platform build settings, board notes, current electrical/capture report and each platform adapter. Reuse shared command scenarios; inspect FieldCore memory/task conventions without modifying it.

## Work

- Execute equivalent read-only, state, bounded movement/stop and native-feature cases on Arduino E2/S3 and native IDF E2/S3 where prerequisites permit.
- Each image's physical write/motion cases require its applicable capture/timing evidence. Arduino qualification and a native-IDF read smoke alone do not qualify IDF timing.
- Compare command semantics, effective native values, result/uncertainty, timing policy, capture failures and cleanup. Explain legitimate adapter capability differences explicitly.
- Repeat loaded owner-sleep/console tests and short memory/stack checks per image. Test init/PSRAM failure, console backpressure and transport faults through real paths or labelled injection.
- Verify the core and platform-neutral application pieces on the selected ESP32-S2 build target if a maintained target is available. Board pins/transceiver adapters require separate board facts; do not compile the S3-only E2 adapter as if it supported S2.
- Record compile-only portability separately from actual hardware qualification. Do not invent another board wiring map or claim unavailable S2 tests passed.
- Fix shared defects in shared owners, adapter defects in adapters. Re-run the original discrepancy and both affected platform regressions.

## Verify and handoff

Produce a platform/capability/evidence table naming image hashes, SDK versions, actual tuples, memory, stack and unrun cases. Leave COM13 on a documented working image with injected load disabled and standstill confirmed. Failed cleanup remains a failure or unknown outcome under the shared contract, not a claimed stopped state. Missing physical fixtures remain explicit.

## Subagents

Assign a cross-platform semantic reviewer and an SDK/memory reviewer. Independently audit the final code and evidence for hidden divergent workflows. Deliver supported platform scope to package/release checks, update roadmap/backlog and finish common commit/sync.
