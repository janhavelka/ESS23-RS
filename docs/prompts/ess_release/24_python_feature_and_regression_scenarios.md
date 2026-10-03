# 24 — Consolidate automated feature and regression testing

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: actual API/CLI behavior and correlation from earlier steps. Reuse the Python tests added alongside each feature; do not postpone basic feature testing until this prompt.

## Read and reuse

Inspect `scripts/bench_probe.py`, `test/bench_probe_test.py`, the current console inventory and native fake serial scenarios. Preserve one port owner, bounded input and exclusive evidence files.

## Implement

- Consolidate finite scenarios for probes, observations, configuration, position/angle, velocity, stop, homing prerequisites, native operations and discovery. Share connection, framing/correlation, evidence and settings snapshot helpers.
- Keep the default quick regression read-only. Motion/configuration/persistence scenarios require explicit scenario selection and bounded settings; do not make stress mode execute every native operation.
- Validate actual workload evidence, result identity, raw/parser outcomes, operation acknowledgement/completion and health freshness. Fail when a requested workload never ran.
- Capture before/after firmware/settings, failures, service gaps, latency, CPU, memory and stack. Store bounded raw/structured evidence incrementally and summarize without keeping unbounded history.
- On failure stop issuing new work, collect cached diagnostics only when framing is intact, and apply the scenario's explicit stop/cleanup rules. Never replay uncertain movement or hide failed cleanup.
- Handle disconnect/restart, partial serial lines, asynchronous terminal records and stale IDs without accepting an unrelated reply. Keep host recovery explicit.

## Verify

Use fake serial tests for all parser/campaign failure branches and interrupted motion cleanup, including stopped/unknown outcomes. Run the short COM13 read-only regression and a small subset of already qualified finite feature scenarios. Do not use all-feature sweeps as the first hardware test or claim physical measurements from console-only evidence.

## Subagents and handoff

Assign a serial/correlation reviewer and an experiment/evidence reviewer. Audit duplicated runners and misleading PASS summaries; retain failed attempts. Deliver repeatable named scenarios, exact inputs and cleanup behavior for IDF parity and final qualification, then complete the common workflow.
