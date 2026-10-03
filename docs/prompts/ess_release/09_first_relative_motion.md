# 09 — First finite relative move and dynamic stop

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 07 target checks, 08 implemented stop/action path, 05–06 configuration and state. Live movement requires reviewed transport evidence from 04, resolved units/sign/ramp prerequisites and a recorded small free-shaft test envelope.

## Read and reuse

Review original ESS position/start pages and appendix, `01_implementation_reference.md`, `09_timing_and_gap_audit.md`, existing FC10 policies, units and operation contexts. Use reviewed position windows; do not broaden arbitrary paired writes in this step.

## Implement

- Add a native finite relative-position operation and its common preparation. Validate the entire parameter set, command flags, native target, speed/ramp policy and readiness before any write.
- Stage parameters, then start only after prerequisites and preceding writes succeed. A partially applied setup or lost trigger acknowledgement produces a retained uncertain outcome, never an automatic replay.
- Reuse 08's same-axis reservation across staging and trigger; no competing native/common/configuration write may replace the staged parameters. A stop may supersede the sequence through its admitted priority path, while eligible unrelated bus work can continue.
- Reuse the bounded operation mechanism and priority stop. Select an explicit verified device-configured ramp or resolved conversion; do not label an unknown millisecond field as physical acceleration.
- Define completion from new operation-relevant observations. An old arrival flag, target equality or trigger echo alone must not falsely complete a fresh move.
- Add direct API and CLI paths for a small relative move with explicit units, effective target and limits. Extend the finite Python bench scenario, including cleanup evidence.

## Verify

Native fault injection covers each sequence boundary, stale completion, rejected intermediate write, wrong echo, delayed response, cancel and stop during setup/acceleration/movement. On COM13 use small bounded positive/negative moves only where encoding is resolved; compare requested/native/readback values and independently observed shaft movement. Measure dynamic stop and final state. Do not blindly repeat a move after uncertain execution or substitute fake physical observations.

Inject a second producer's target/speed/configuration write between staging acknowledgement and trigger; verify explicit conflict rejection/defer and that the original prepared values alone can be triggered.

## Subagents and handoff

Assign a manual/command reviewer and a sequence/completion reviewer. Diagnose every hardware discrepancy to its root cause before changing defaults. Deliver the qualified subset, unresolved sign/ramp/model facts and physical evidence to 10–11; audit and commit/sync under the shared contract.
