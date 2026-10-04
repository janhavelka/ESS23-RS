# 08 — Bounded actions, enable/release and priority stop

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 02–03 owner priority/cancellation, 05 identity/configuration and 06 state observations. Physical actions also require the relevant transport disposition from 04 and exact documented drive prerequisites.

The authorized functional subset may run unattended without an analyzer or
human shaft observation. Keep FC06 echo execution uncertainty separate from
subsequent checked drive-state observations. Electrical and independently
measured stopping evidence remain unqualified.

## Read and reuse

Read axis operation/stop contracts, ESS auxiliary and normal/emergency-stop pages, current codecs and caller-owned typed-read context. Extend that real operation mechanism rather than adding a universal event framework.

## Implement

- Implement explicit native/common enable, release, alarm clear and documented stop preparations with typed effects. Do not silently substitute release, zero speed or emergency stop for another requested stop policy.
- Advance finite caller-owned sequences from supplied time/events. Yield bounded transaction work and release the bus between waits. Reject stale, duplicate or wrong-operation events.
- Add one application-owned same-axis operation reservation shared by common, native and CLI routes. Hold it across staging, triggering and observation; compatible non-consuming reads and unrelated targets may use the bus. Declare conflict disposition after failure/uncertain staging. Do not replace this with a whole-bus lock or put an application scheduler in the core.
- Retain admission, transmitted/acknowledged/rejected/unknown execution and completion evidence separately. A write echo is not proof that windings changed or movement stopped.
- Validate stop behavior and reserve urgent/result capacity before superseding unsent continuations. Rejected/unsupported/full stop admission leaves the current operation and queue intact. Once accepted, preserve the interrupted outcome and route stop through reserved priority after settling the in-flight transaction.
- Stop has its own prerequisites: a usable stop for a known target must not wait for move readiness, idle state, host origin, fresh position or an alarm-free axis unless its particular requested semantics needs that information. Do not insert an unnecessary refresh before priority stop; actual transport recovery constraints still apply.
- Support serial-only use with known unwired or documented disabled inputs.
  Gate only on external prerequisites of the requested action. An absent home
  switch must not reject a serial stop; an unwired but assigned asserted limit
  is not equivalent to disabled input. Needed I/O changes use prompt 15's typed
  operations and never happen implicitly as part of enable or stop.
- Define supported deceleration and host/device queued-command disposition explicitly. Unsupported ESS policy must fail before parameter writes. Local cancel remains local.
- Add the corresponding CLI commands through the public preparations. Ensure console and result pressure cannot erase either the interrupted operation or the stop result.

## Verify

Native tests cover FC06 echo vs acknowledgement, timeout after partial/full TX, invalid responses, delayed/stale events, stop in every phase, result capacity, explicit recovery and no replay. On hardware, begin with reviewed stopped-state stop and deliberate enable/release checks where prerequisites are established; retain before/after flags. Do not start a move in this prompt or claim stopped-state tests prove dynamic stop latency.

Test invalid event envelopes leave context unchanged, while a correlated timeout/bad reply advances a failed or uncertain operation without publishing invalid payload. Test stop with stale feedback, unknown prior execution, alarms and missing unrelated origin/scales; test rejected policy/full urgent capacity preserves active work. Public events remain core types mapped from application transport evidence.

## Subagents and handoff

Assign a sequencing/uncertainty reviewer and an independent stop/scheduling reviewer. Audit physical-state claims and operation lifetime. Deliver the actual action/event/result contract and usable stop path to 09; complete the common audit and sync steps.
