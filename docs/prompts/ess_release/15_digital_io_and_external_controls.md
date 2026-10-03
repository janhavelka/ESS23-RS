# 15 — Optional digital I/O and external-control configuration

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: typed configuration/effect tracking from 13 and state observations
from 06. Physical changes require known wiring/configuration only for the affected
terminals and operations; no external switch or output load is mandatory for
serial-only motor control.

## Read and reuse

Read the original ESS-specific X0–X3 and Y0/Y1 function/polarity tables and
[the profile contract](../../profile_contract.md). ESS function-manual physical
PDF pages 27–28 and 73–74 define function 0, named `UNDEFINED` in the generated
enums, as a terminal with no function. Recheck original pages before coding.
Reuse indexed descriptors, configuration sequencing and cached I/O observations.

## Implement

- Expose typed input/output function, polarity and custom-output reads/writes
  for documented terminals and values. Keep terminal counts profile-specific.
- Provide explicit disabling by assigning the documented no-function value:
  ESS `InputFunction::UNDEFINED` / `OutputFunction::UNDEFINED` (0). Reuse the
  same checked function setter from direct API and CLI (`none` selects this
  documented value); no second write path or
  invented universal disable register. Other profiles report unsupported when
  their documented command set cannot disable a function.
- Distinguish declared wiring (connected, unconnected, unknown), observed input
  level and drive function assignment. Unconnected does not mean disabled;
  polarity inversion does not disable a function. Function 0 does not document
  an electrical output level or guarantee that an external load is de-energized.
- Permit serial-only control without external home/limit/trigger wiring where
  the selected mode supports it. Reject only operations needing an unavailable
  or disabled input, such as a particular homing method or external segment
  trigger. Retain unknown serial-vs-external enable/release precedence explicitly.
- Cover origin, limits, enable/release, stop, position/speed/JOG, home, trigger
  and segment-selection choices from the ledger. Reserved/unexplained values
  remain unavailable. Never silently disable or reassign controls at startup,
  on a probe, or while preparing an ordinary move.
- Declare that reassignment or polarity changes can reinterpret an asserted
  input. Validate actual external-control prerequisites for the changed setting;
  disabling an existing limit/stop function is an explicit configuration action.
- Preserve writable masks and read-only actual input/output state. No generic
  MCU GPIO command enters the motor library. Preserve partial updates and
  uncertainty, including required reference/configuration invalidation.
- Revisit 08–09's serial-only prerequisites and 14's input-dependent homing
  methods. Reuse these setters; rerun admission tests and physical checks where
  available. Do not report a method ready while its prerequisites remain unmet;
  keep its documented/implemented capability visible.

## Verify

Native tests cover input and output disable encoding, readback, index bounds,
all resolved choices, reserved bits, no implicit writes, configured-but-unwired
inputs, unknown wiring, polarity changes and failures mid-update. Verify that
serial-only motion is not rejected solely because optional I/O is unconnected,
while methods needing disabled/unavailable inputs fail before TX. Use only
established operation prerequisites; do not assume external input override.

Bench tests record before/after assignments and use explicit disable/readback/
restore where supported and appropriate for known wiring. Run a short existing
serial-only regression. Missing external switches/loads leave their physical
function tests NOT RUN; they do not block serial-only or register-path evidence.

## Subagents and handoff

Assign a terminal/mask source reviewer and an external-effect reviewer. Audit
all API/CLI routes for prerequisite bypass, mandatory optional I/O and invented
terminal capability. Deliver typed I/O operations, disable evidence and the
external-trigger contract to 16; follow common audit, report and commit/sync.
