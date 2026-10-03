# 08 — Bounded actions, enable/release and priority stop

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 02–03 owner priority/cancellation, 05 identity/configuration and 06 state observations. Physical actions also require the relevant transport disposition from 04 and exact documented drive prerequisites.

## Read and reuse

Read axis operation/stop contracts, ESS auxiliary and normal/emergency-stop pages, current codecs and caller-owned typed-read context. Extend that real operation mechanism rather than adding a universal event framework.

## Implement

- Implement explicit native/common enable, release, alarm clear and documented stop preparations with typed effects. Do not silently substitute release, zero speed or emergency stop for another requested stop policy.
- Advance finite caller-owned sequences from supplied time/events. Yield bounded transaction work and release the bus between waits. Reject stale, duplicate or wrong-operation events.
- Retain admission, transmitted/acknowledged/rejected/unknown execution and completion evidence separately. A write echo is not proof that windings changed or movement stopped.
- Interrupt unsent continuations when a stop is requested, preserve the interrupted operation's outcome, and route stop through reserved owner priority after settling the in-flight transaction.
- Define supported deceleration and host/device queued-command disposition explicitly. Unsupported ESS policy must fail before parameter writes. Local cancel remains local.
- Add the corresponding CLI commands through the public preparations. Ensure console and result pressure cannot erase either the interrupted operation or the stop result.

## Verify

Native tests cover FC06 echo vs acknowledgement, timeout after partial/full TX, invalid responses, delayed/stale events, stop in every phase, result capacity, explicit recovery and no replay. On hardware, begin with reviewed stopped-state stop and deliberate enable/release checks where prerequisites are established; retain before/after flags. Do not start a move in this prompt or claim stopped-state tests prove dynamic stop latency.

## Subagents and handoff

Assign a sequencing/uncertainty reviewer and an independent stop/scheduling reviewer. Audit physical-state claims and operation lifetime. Deliver the actual action/event/result contract and usable stop path to 09; complete the common audit and sync steps.
