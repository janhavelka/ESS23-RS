# 16 — Stored position/speed records and external triggers

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 12 pair policy, 13 configuration effects and 15 optional I/O configuration. This block is bounded indexed record access, not a trajectory engine.

## Read and reuse

Read [the prompt 12 per-pair disposition](../../ess_pair_writes.md): no stored
position pair has a reviewed FC10 window. The six-word record includes a reserved
word; its layout supplies neither whole-record nor five-word write permission.

Review original ESS multisegment pages and indexed ledger records. Reuse index validation, pair codecs, typed parameters and configuration sequence handling.

## Implement

- Provide indexed reads/writes for all 16 position records: pulse target pair, speed, acceleration and deceleration; account for each reserved slot without exposing a write.
- Provide all 16 speed/acceleration/deceleration records and shared PT/PV starting-speed fields. Include relative/absolute external positioning choice and documented PV trigger level/edge policy.
- Use one small indexed helper per real record layout, not 16 copied functions or a generic runtime schema engine.
- Record storage capacity separately from what the selected model's input combinations can select. Four ESS inputs do not prove all 16 entries are physically addressable simultaneously.
- Expose typed profile CLI access and bounded multi-field updates with honest partial-application results. Unknown paired-write support from 12 remains a visible blocker.
- Keep record configuration available without an external trigger fixture.
  Reject/defer physical execution prerequisites when its required inputs are
  unwired, disabled or unknown; other serial motion remains independent.
- Execution is external-input-triggered. Do not add a fictional serial startSegment, scheduler-generated trigger or claim that PT timing prose automatically applies to PV.

## Verify

Native tests enumerate valid/invalid indexes, both record edges, pair order/access, reserved words, partial update, cancellation and no-TX validation failures. On hardware read representative boundary records and perform limited justified writes/readback/restore when supported. Triggered motion requires actual documented input wiring and the existing stop envelope; absent wiring leaves that part explicitly unqualified.

## Subagents and handoff

Assign an indexed-layout/access reviewer and an external-trigger coverage reviewer. Audit reuse across position/speed/shared records without erasing semantic differences. Deliver complete per-record coverage dispositions and tests; finish common documentation, independent review and sync.
