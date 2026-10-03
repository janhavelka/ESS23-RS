# 03 — Connect the bus owner to the E2 console

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: verified owner and scheduling contracts from 01–02.

## Read and reuse

Inspect `examples/probe_cli/main.cpp`, `ProbeConsole.*`, `E2Load.*`, `test/probe_app_test.cpp`, shared SDK fakes and `scripts/bench_probe.py`. Read the CLI and FieldCore diagnostics contracts; reuse the existing console instead of creating a second command engine.

## Implement

- Route every bus request through the new owner and its admitted validator; the shipped motor probes, including load-campaign probes, use the checked ESS parser. Remove the superseded direct runner admission path. Load configuration/query, cached status/health, statistics and host reset remain local controls under their documented admission rules.
- Service a bounded amount of console input while transactions are active. Keep output bounded/nonblocking with backpressure disposition, so a future stop command will not wait behind USB formatting or a full transaction.
- Expose implemented queue, retained-result, cancel and driver diagnostics using the CLI contract's vocabulary. Preserve cached status/health and host-only reset/recover semantics.
- Apply 02's recovery queue policy through the actual CLI path; clearing a runner fault cannot resume old queued work. Preserve distinct recovery and interrupted-request results under full result pressure.
- Separate observation time, application delivery and recovery-settlement time. The current `finishedUs` serves both cache age and recovery guard; refactor those roles so delayed servicing cannot make an old probe look newly observed. Use qualified timing bounds, not repeated status serialization, for freshness.
- Define command correlation separately from operation IDs and retain one terminal result per admitted request. State limits for outstanding commands and result retention.
- Update the Python transport for interleaved accepted/terminal records if needed, without weakening strict correlation or automatically retrying after framing failure.
- Keep one UART owner and one owner task; preserve 01's cooperative access contract and use small adapters for other producers. Do not call unsynchronized owner methods from competing tasks. Put larger queue/result/trace storage in PSRAM, required capture/driver state and stacks internal.

## Verify

Compile/test actual application paths with SDK fakes: command during active TX/RX, full queues, result pressure, console backpressure, cancel, parser rejection and recover during unsettled DE. On COM13 run short unloaded probes and bounded competing-task/console campaigns. Record latency, owner/capture gaps, RAM/PSRAM/stack and exact console lines accepted/dropped. Add a native responsiveness test; do not advertise physical stop yet.

Use delayed-owner timer capture to verify old observations keep their age, repeated cached queries do not rejuvenate them, and refactoring timestamps preserves the recovery guard.

## Subagents and handoff

Assign a console/correlation reviewer and an application/FieldCore-fit reviewer. Re-audit code and help/dispatch parity, fix root causes of hardware failures, and deliver the real owner integration and measured limits to 04 under the common workflow.
