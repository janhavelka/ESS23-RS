# 29 — Fault, load and endurance qualification

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 28's audited candidate, 24's finite scenarios and known model/firmware/settings. Physical operations still require their specific fixture and timing evidence.

## Read and reuse

Read the qualification matrix, capture/CPU policy, supported platform scope and all current hardware gaps. Use existing runners and diagnostics; add a narrow missing probe only when it distinguishes a real failure cause.

## Execute

- Define finite durations/counts and measurable pass/fail thresholds before each run: no lost terminal results or replay, bounded service/stop latency, unchanged memory trend, explicit failures and known final state.
- Run extended read-only load/owner-sleep/console campaigns on supported images, then bounded feature sequences within the qualified motion/configuration envelope. Persistence/factory restore never enters a repetitive wear loop.
- Exercise lost/delayed acknowledgements, partial setup, wrong/stale frames, queue/result pressure, interrupted motion, local cancellation and pending stop. Label wire/software injection separately from physically removed connections.
- Run real disconnect/reconnect, power interruption or external I/O cases only with the actual fixture/control available. Confirm no motion replay on startup/recovery and reconcile reference/configuration afterward.
- Independently observe dynamic stop/position where required. A telemetry flag alone does not prove physical movement or stopping.
- For each failure rerun the scenario with adequate diagnostics, establish the culprit, apply the simplest fix/refactor and repeat the failing and affected regression cases. Preserve failed logs and exact image changes.

## Acceptance and handoff

Record exact image/platform/model/settings, scenarios, counts/durations, latency distributions, service gaps, CPU, RAM/PSRAM/stack and cleanup. Missing external timing, fixture or firmware behavior remains a release qualification gap. Long passing probes do not qualify unrelated homing/tuning/persistence. If code changes, rerun the relevant 28 audit and verifier before promoting the candidate.

## Subagents

Assign an experiment/failure-causality reviewer and an evidence/coverage reviewer; the lead alone owns hardware. Produce the final qualification matrix and open-blocker list for 30, update roadmap/backlog and commit/sync.
