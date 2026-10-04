# 13 — Typed driver settings and software limits

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 05–07 configuration/reference ownership, 08 stopped-state actions and 12's reviewed pair-write policy.

## Read and reuse

Read [the prompt 12 per-pair disposition](../../ess_pair_writes.md): both soft-limit
pairs still lack admissible write windows. Read access and single-word settings
do not authorize split FC06 or new FC10 spans.

Review ESS direction/subdivision/word-order, over-limit, soft-limit and fixed-length interruption semantics in the original pages and ledger. Reuse typed descriptor validation and operation contexts; communication, save and restore belong to later steps.

## Implement

- Expose read/write operations for direction, command subdivision, device word order, soft-limit enable, positive/negative limit pairs, over-limit stop behavior and related interruption choices.
- Validate candidate dependencies and state before writes: ordered limits, reference requirements, legal masks/enums and resolved native ranges. Do not let a parameter group partially validate after its first write.
- Distinguish requested, acknowledged, read-back and active settings. A failed multi-write update can leave a partially changed device; retain exact progress and invalidate affected assumptions instead of promising rollback.
- Invalidate/reconcile host scale, polarity, origins, pair decoders and prepared operations after relevant changes. Historical raw results retain their old context.
- Provide typed profile CLI commands with the same preconditions as direct API. Host axis configuration remains distinct from drive configuration.
- Unresolved paired fields from 12 remain guarded, with reads and explicit coverage gaps where available.

## Verify

Test invalid combinations/no TX, edge ranges, changed word-order decoding, stale prepared requests, partial updates, lost replies, reference invalidation and readback disagreement. On the bench use bounded stopped-state settings changes with recorded original values; verify restoration. Test software-limit behavior with small movement only after the limit/reference semantics are established; unsupported fixture cases stay open.

## Subagents and handoff

Assign a configuration-effects reviewer and a limits/reference test reviewer. Audit one validation path for common/native/CLI callers and clear partial-update outcomes. Deliver settings-effect rules to homing and later native families, then report and sync.
