# ESS paired-register write disposition

Prompt 12 reviews all **20 writable pairs** in the ESS ledger: the active
position target, homing offset, two software limits and sixteen stored position
targets. The twenty-first pair, current position `0x000A/2`, is read-only and
has no direct write form. This review establishes no new admissible write
window or physical qualification. Source facts remain in
[the register ledger](reference/ess_rs_registers.json); operational availability
remains in [the operation inventory](reference/ess_rs_operations.json).

All page references below are physical pages of the preserved
[function manual](vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf),
visually checked for this review. DM-PR pages 80 onward supply no ESS aliases.

## Reviewed transactions

| Exact FC10 start/count | Contents | Evidence |
| --- | --- | --- |
| `0x0024/2` | Active position target pair | p8 |
| `0x0021/5` | Position acceleration, deceleration, speed and target pair | p16 |
| `0x001D/3` | Jog/speed parameters; no pair | p18 |
| `0x0031/6` | Homing method, two speeds, ramp and offset pair | p20 |

These are exact reviewed transactions, not a device-wide maximum or permission
for their subsets. In particular `0x0035/2`, `0x0037/2`, `0x0039/2`, a combined
limit span and stored-record writes remain unreviewed. FC06 rejects either
half of every pair as library policy against unqualified split updates; this
is not a claim that firmware necessarily rejects FC06. Generic Modbus limits
cannot establish ESS acceptance. The malformed p16 request has an extra
payload byte despite its five-word count; use the independently checked frame
described in [the implementation reference](reference/01_implementation_reference.md),
not the printed bytes. The conflicting diagram/prose CRCs on p8 likewise do
not change the reviewed start/count.

## Per-pair disposition

| Field | Pair addresses | Source range and signed meaning | Word order | Admissible write / remaining proof |
| --- | --- | --- | --- | --- |
| Active position target | `0x0024–0x0025` | p16 gives positive/negative direction; p70 prints malformed `-0xFFFFFFF` to `0xFFFFFFFF`. Negative wire encoding and applicable device bounds remain unresolved. | Configurable by `0x0019`, p30 | `0x0024/2` or containing `0x0021/5`; existing typed move path uses the latter with qualified prerequisites. |
| Homing offset | `0x0035–0x0036` | p73: `-0xFFFFFFF` to `0xFFFFFFF`; negative encoding and native scale unresolved. | High/low labels on p19/p73; configurable behavior unresolved | Only containing `0x0031/6` is reviewed. Standalone offset write and a typed homing setter remain unavailable. |
| Positive software limit | `0x0037–0x0038` | p73: `-0xFFFFFFF` to `0xFFFFFFF`; negative encoding and native scale unresolved. | Configurable, p30 | No established admissible write window. Applies only after homing, p29/p73. |
| Negative software limit | `0x0039–0x003A` | Same p73 range and uncertainties | Configurable, p30 | No established admissible write window. Applies only after homing, p29/p73. |

All sixteen stored target pairs below share the p22 signed-direction semantics,
p75 range `-0xFFFFFFF` to `0xFFFFFFF`, unresolved negative wire encoding and
command scale, and p30 configurable word order. **None has an established
admissible FC10 window.** A two-word pair, five-word parameter subset or
six-word record cannot be inferred from its readable/writable catalogue entry.

| Segment | Target pair | Reserved record word | Appendix evidence |
| --- | --- | --- | --- |
| 1 | `0x0060–0x0061` | `0x0065` | p75 |
| 2 | `0x0066–0x0067` | `0x006B` | p75 |
| 3 | `0x006C–0x006D` | `0x0071` | p75 |
| 4 | `0x0072–0x0073` | `0x0077` | p75 |
| 5 | `0x0078–0x0079` | `0x007D` | p75 |
| 6 | `0x007E–0x007F` | `0x0083` | p75 |
| 7 | `0x0084–0x0085` | `0x0089` | p75 |
| 8 | `0x008A–0x008B` | `0x008F` | p75 |
| 9 | `0x0090–0x0091` | `0x0095` | p76 |
| 10 | `0x0096–0x0097` | `0x009B` | p76 |
| 11 | `0x009C–0x009D` | `0x00A1` | p76 |
| 12 | `0x00A2–0x00A3` | `0x00A7` | p76 |
| 13 | `0x00A8–0x00A9` | `0x00AD` | p76 |
| 14 | `0x00AE–0x00AF` | `0x00B3` | p76 |
| 15 | `0x00B4–0x00B5` | `0x00B9` | p76 |
| 16 | `0x00BA–0x00BB` | `0x00BF` | p76 |

Stored-position execution is external-input-triggered (p20–23); no serial
segment trigger is supplied by these addresses. Reserved words remain
inaccessible even though the source prints RW/S. RW and RW/S notation do not
establish automatic persistence or authorize a save operation.

The homing omission remains substantive: p29 calls the following table a list
of all high/low parameters, but p30 includes feedback, active target, both
limits and stored targets while omitting `0x0035/0x0036`. The p20 homing example
writes two zero offset words, which cannot distinguish either order. Generic
word-order prose and default high/low labels cannot resolve this omission.
An authoritative clarification or a separately designed interpretation
experiment is still needed; even successful raw readback alone cannot prove
which word the homing engine interprets as high.

Keep conflicting addresses separate: p73's collision fields `0x003B/0x003C`
have unspecified access, while p79's `0x0122/0x0123` have RW/S and a different
threshold minimum (50 versus 200). These are single-word descriptors, not
extra pairs or established aliases. The soft-limit-enable prose conflict
(`0x0019` on p28 versus `0x0018` in p29/p70 tables) does not authorize treating
the word-order register as a limit switch.

No reviewed FC10 transaction has documented atomic application, rollback or
partial-failure semantics. An exception, timeout, cancellation or bad echo
after accepted TX therefore cannot prove unchanged parameters. Preserve
before-values and uncertainty; never automatically replay an uncertain write.
Acknowledgement proves neither motion completion nor persistence.

## API and downstream handoff

Reuse [Position.h](../include/MotorControlRS/profiles/ess_rs/Position.h):
`prepareMoveRelative`, `prepareMoveAbsolute`, `prepareMoveAngle`, `nextMove`
and `advanceMove` already encode the target with `encodeInt32` and validate
the `0x0021/5` transaction before publishing work. The mathematical signed32
subset is not a source range resolution; negative values require the explicit
qualified two's-complement prerequisite. The existing `move relative`,
`move absolute`, `move angle` and `profile ess_rs move-*` console routes call
these public operations. Keep their readiness, axis reservation, uncertainty
and physical-write gates; do not add duplicate pair writers or unavailable
setter stubs. Exact CLI grammar is in [the positioning guide](ess_position.md).

Prompts 13–18 must retain the above field-specific unavailable reasons.
Homing's containing window does not resolve its offset interpretation; software
limits and stored targets need wire-form evidence before typed setters.
Single-word parameters can be considered independently by their owning prompt,
without implying that a complete pair-bearing record can be written.

The current FieldCore `Rs485OwnerTransaction.h` still has eight TX bytes;
`Rs485Task.cpp` retains request-prefix echo handling. Its bounded request/work/
result ownership is a useful reference, but its current capacity cannot carry
even the 13-byte `0x0024/2` request. This repository keeps its own checked codec,
explicit response evidence and larger bounded requests. No FieldCore edit or
motor-compatibility claim accompanies this review.

## Narrow future firmware experiment

Physical paired writes are **NOT RUN**: independent timing/echo qualification
and the necessary stopped-state/input prerequisites are absent. A future
experiment may examine only the already reviewed `0x0024/2` parameter on the
identified firmware, with exclusive bus ownership, qualified TX/RX/DE timing
and response provenance, fresh stopped/alarm-free observations, understood
active inputs and independently prevented external triggers. No assignment or
polarity is silently rewritten to obtain those conditions.

Read and retain both original target words and active `0x0019` order. Choose
one understood positive asymmetric value within established bounds, for example
`0x00010002` only if that value is qualified for the actual configuration.
Transmit once through the checked `0x0024/2` codec, retain the acknowledgement
and raw traffic, then read back exactly two words. Issue no motion trigger,
save, homing command or word-order change. While the same stopped/input
conditions hold, explicitly restore the saved raw words with that same window
and verify them by readback. Retain timing, errors, before/after words and
ending stopped state. If execution becomes uncertain, stop the sequence and
reconcile state through bounded reads before deciding on any separate restore;
do not automatically replay the experiment or restore attempt.

This requires an established communication recovery route and a means to keep
external triggering prevented until restoration is confirmed. A failed restore
leaves an unresolved configuration change, not successful cleanup. The result
would qualify only that firmware/window/value case: no negative encoding,
atomicity, home-offset interpretation or other window is proved. No sweep of
unknown addresses/counts is justified.
