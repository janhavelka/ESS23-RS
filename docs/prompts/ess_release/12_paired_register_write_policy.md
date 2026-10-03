# 12 — Resolve paired-register write support before expansion

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 05 coverage inventory and current checked codecs; earlier motion uses only already reviewed write windows.

## Read and reuse

Read original ESS protocol/appendix pages, the ledger/generator and `Codec.*`. The current FC10 policy admits exactly four windows: 0x0024/2, 0x0021/5, 0x001D/3 and 0x0031/6. FC06 rejects paired halves.

## Work

- Inventory every remaining writable pair needed by limits, home offsets and stored position records. Map source-supported write form, exact start/count, signedness, word order, range and partial-application semantics.
- Resolve configurable-word-order omissions such as the homing offset from authoritative evidence; do not assume all pairs share one order or alias conflicting addresses.
- Extend codec policy only for supported, reviewed transactions. Generate compact access policy from the same source ledger and retain descriptive-catalogue independence.
- If a documented writable field lacks an established admissible wire form, retain unavailable typed writes with the specific unresolved reason. Do not use split FC06 writes, arbitrary FC10 spans or an unchecked escape hatch to bypass the gap.
- Design any necessary firmware qualification as a narrow explicit experiment on a understood, stopped-state parameter with saved before-values and a recovery route. Do not sweep unknown addresses/counts or infer support from generic Modbus maxima.
- Implement real pair helpers only for resolved fields and expose their typed API/CLI paths; leave unresolved capabilities visible in the coverage inventory.

## Verify

Native tests cover each new permitted window and its neighboring forbidden spans, odd halves, capacity/CRC/echo failures, reserved gaps, both supported word orders and unchanged outputs. Perform only justified bounded bench write/readback/restore cases; if vendor or fixture evidence is missing, record that proof gap rather than marking the family complete.

## Subagents and handoff

Assign a PDF/access-policy reviewer and an independent wire-fixture reviewer. Audit generated artifacts and absence of access bypasses. Deliver a precise per-pair disposition for 13–18; audit, update downstream prompts if policy changed, and commit/sync.
