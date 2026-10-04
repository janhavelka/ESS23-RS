# 21 — Save, restore and persistence evidence

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: configuration snapshots/effects, stopped-state evidence, 20's communication recovery route and known limits of the exact drive firmware.

## Read and reuse

Read the original ESS save-all/factory-restore pages, access notation and timing audit. Reuse typed actions, owner exclusivity, retained results and configuration readback; never overload console reset/recover.

Prompt20's [handoff](../../reports/ess_release_20_2026-10-04.md) and
[communication API](../../ess_communication.md) provide `CommunicationContext`,
explicit candidate confirmation and the application's token-based commissioning
lease. Reuse that ownership while saving a pending custom address; do not route
save around it or release it merely because the host UART was restored. Physical
activation remains NOT RUN until a motor restart/route-back fixture exists.

## Implement

- Add explicit typed save and factory-restore preparations with documented stopped-state prerequisites and scope. Keep volatile application, acknowledged command and verified persistence distinct.
- Preserve before-values and configuration/identity evidence needed to recommission the actual drive. Restoration can change communications and invalidate all host scale/reference assumptions.
- Represent RW/RW-S uncertainty per field. Do not invent a save-complete flag or infer durability merely from a fixed sleep where the manual gives no guarantee.
- Bound invocation/write counts. No persistence operation belongs in startup, probe, diagnostics, generic recovery or repeated stress traffic.
- Add explicit profile CLI operations and post-action state/configuration verification. A lost acknowledgement remains uncertain; no automatic save/restore replay.
- Make the runtime's ending state clear when a restart or manual intervention is required.
- Revisit prompt 20's communication activation cases that require save, using this explicit operation and the documented restart procedure. Keep unavailable physical activation evidence open.

## Verify

Native tests cover moving/unknown-state rejection, lost replies, partial knowledge, restart-required status, changed communication, stale prepared motion and explicit host recovery. Perform limited physical persistence checks only with a known configuration backup and an available restart/recommissioning procedure. Factory restore is a deliberate controlled case; it is not needed merely to demonstrate command encoding. Record unavailable restart/restore proof honestly, and never cycle nonvolatile writes in a soak.

## Subagents and handoff

Assign a persistence-semantics reviewer and a configuration/recovery reviewer. Audit side effects, hidden retry paths and claims of durability. Deliver exact tested behaviors plus remaining field-level persistence gaps; complete common review, evidence and commit/sync.
