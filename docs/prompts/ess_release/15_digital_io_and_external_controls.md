# 15 — Digital I/O and external-control configuration

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: typed configuration/effect tracking from 13 and state observations from 06; physical writes require known external-input states.

## Read and reuse

Read the ESS-specific X0–X3 and Y0/Y1 tables, polarity masks and function choices; do not borrow extra terminals from another family. Reuse indexed descriptors, configuration sequencing and cached I/O observations.

## Implement

- Expose typed input/output function, polarity and custom-output reads/writes for documented terminals and values.
- Cover origin, limits, enable/release, stop, position/speed/JOG, home, trigger and segment-selection choices from the ledger. Reserved/unexplained values remain unavailable.
- Declare that reassignment or polarity change can reinterpret an already asserted input and cause an action. Validate the actual external-control prerequisites before modifying those settings.
- Preserve model-specific terminal count, writable masks and read-only actual input/output state. Do not claim output state readback proves an external load changed.
- Route all operations through typed public helpers and the existing profile CLI inventory. No generic pin/GPIO command enters the motor library.
- Retain partial configuration and uncertainty, including required reference/configuration invalidation.
- Revisit prompt 14's homing methods that need these input assignments. Reuse these setters and rerun dependent method-admission tests and physical fixture checks where available; do not advertise methods while their prerequisites remain unmet.

## Verify

Native cases include terminal/index limits, every resolved choice, reserved bits, incompatible combinations, asserted-input assumptions, failures mid-update and passive cache reads. Bench-test only known wiring; retain before/after/readback and restore explicitly. Unconnected switches or output loads mean physical function qualification is NOT RUN, not a software test pass.

## Subagents and handoff

Assign a terminal/mask source reviewer and an external-effect reviewer. Audit all routes for prerequisite bypass and invented terminal capability. Deliver typed I/O configuration and the external-trigger contract to 16; follow the common report, re-audit and commit/sync requirements.
