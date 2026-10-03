# 20 — Explicit drive communication commissioning

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 05 device configuration reads, 12/13 checked writes/effects and 19 host tuple support/restoration.

## Read and reuse

Review original ESS address/DIP/baud/format, activation and persistence rules. Reuse owner exclusivity, typed parameters and exact target generations. The user-reported defaults are not activation evidence.

## Implement

- Add explicit typed communication setting operations, retaining before/requested/read-back/observed-active endpoints and tuples.
- Distinguish a stored setting from the currently responding interface. Baud/format restart requirements and uncertain address acknowledgement/activation rules stay visible.
- Sequence only documented changes. Do not guess that a reply will use the old/new address, auto-scan arbitrary protocols, silently save, or power-cycle a board through an unrelated mechanism.
- If activation requires save, implement and inspect the pending change here; complete that activation case with prompt 21's explicit save operation. Record the revisit instead of pulling a hidden persistence action into this block.
- On lost acknowledgement or failed host switch, retain the candidate contexts and uncertain outcome. No write replay. Use explicit bounded documented read-only confirmation or operator-directed recovery.
- Keep normal bus producers excluded while commissioning owns tuple changes. Restore ordinary service only under a known settled host/target context.
- Add profile communication CLI commands and a finite host procedure that exposes effects and prerequisites before execution.

## Verify

Native cases cover delayed/lost acknowledgement, old/new response contexts, rejected settings, failed host reconfiguration, stale queued work and restart-required states. Hardware changes require a recorded route back to communication and actual available restart/fixture control. Without it, implement/test the protocol paths and mark those physical cases NOT RUN; do not invent remote power control. Retain/restore configuration and verify an ordinary probe afterward.

## Subagents and handoff

Assign an activation/persistence source reviewer and a failure/recovery reviewer. Audit no hidden writes/retries and exact endpoint ownership. Deliver commissioning/restoration evidence for persistence and discovery; finish common re-audit, report and commit/sync steps.
