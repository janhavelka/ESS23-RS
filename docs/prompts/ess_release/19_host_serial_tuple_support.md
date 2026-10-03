# 19 — Host serial settings and adapter capability limits

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: the selected capture mechanism and timing rules from 04; the current adapter initially fixes 115200 8N1. This step changes host settings only, not device settings.

## Read and reuse

Read discovery/CLI contracts, board settings, the pinned SDK UART APIs and current `E2Uart` framing arithmetic. Inspect FieldCore's actual baud/format validation read-only; do not copy its limited whitelist as a universal motor contract.

## Implement

- Define adapter-reported supported baud/format tuples based on actual hardware/SDK capability and reviewed ESS tuples. Reject unsupported values before changing the UART.
- Allow changes only after queued/active work and DE are settled. Reserve exclusive bus configuration ownership; prevent other producers from dispatching against an intermediate tuple.
- Recompute actual character duration, RTU thresholds, capture interval bounds and budgets for each tuple. The 304-us first-reply exception is specific to the recorded bench tuple and is not a universal override.
- Retain original/requested/active host tuple, configuration generation and failure state. If change or restoration fails, block new work until explicit repair; do not silently claim the old configuration survived.
- Reuse one adapter; no second UART engine. Add `host` and tuple capability diagnostics through application APIs, without exposing platform types in the core.
- Invalidate selected-device confidence appropriately while retaining historical results under their original tuple.

## Verify

Native tests cover invalid tuple/no hardware mutation, active-operation refusal, changed character lengths, failed setup/restoration, queued producer exclusion and stale observations. On COM13 use deliberate host-only mismatch/nonresponse/restore checks at reviewed tuples; keep the device unchanged and restore the known working tuple. Record unsupported/unqualified combinations separately.

## Subagents and handoff

Assign an SDK/framing reviewer and a configuration-ownership reviewer. Audit recovery and timestamp continuity. Deliver real host tuple support and restoration APIs for 20/22; complete the common verification and sync workflow.
