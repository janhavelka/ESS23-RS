# ESS bounded actions and priority stop

Prompt 08 implements the installed `ActionOperation.h` and
`profiles/ess_rs/Actions.h` contract. Applications own `ActionContext`, the
clock, transport, scheduling, reservation and retained results. Core functions
perform no I/O, allocation, retries or clock reads. Include the ESS header to
use the common `MotorControlRS::prepareEnable`, `prepareRelease`,
`prepareClearAlarm` and `prepareStop` overloads. They call the same implementation
as the native ESS preparations, including `prepareNormalStop` and
`prepareEmergencyStop`.

| Action | Prepared FC06 | Reported completion after acknowledgement |
| --- | --- | --- |
| Enable | `0x002D = 0x0012` | RELEASED bit clear |
| Release | `0x002D = 0x0011` | RELEASED bit set |
| Clear resettable alarm | `0x002D = 0x0021` | Alarm word zero and ALARM bit clear |
| Normal stop | `0x0027 = 0x0100` | RUNNING bit clear |
| Direct stop (native emergency command) | `0x0027 = 0x0200` | RUNNING bit clear |

These are status reports, not independent measurements of winding current,
shaft standstill or stopping latency. Already-matching flags do not establish
that the action caused a transition. Alarm clear does not reset faults for which
the hardware manual requires power cycling. Enable does not promise to override
an assigned external release input. The implementation changes no input function,
polarity, ramp, position or persistent setting.

The original function manual physical pages 25–26 and 70–71 establish these
commands. The stop page has inconsistent copied table descriptions; its prose,
wire examples, graph and register appendix agree on the two stop values.
Normal stop uses deceleration established before motion; direct stop does not
use that deceleration. `StopPolicy` requires an explicit behavior.
Custom deceleration and device queue PRESERVE/DISCARD reject before yielding
traffic. `DeviceQueue::UNSPECIFIED` explicitly requests no device queue guarantee.
Host continuation cancellation is a separate application effect. A serial direct
stop is not a qualified independent emergency-stop mechanism.

## Preparation and event lifetime

`prepareAction` validates target/address/generation, nonzero operation ID,
absolute deadline and bounded observation options without changing output on
rejection. `nextAction` yields the same step token until the caller consumes an
event: one eight-byte FC06 write, then WAIT or an eight-byte FC03 `0x0006/2`
read. A read returns nine bytes. The default observation policy is 10 ms between
reads, at most 20 polls; configurable limits are positive interval and 1–64 polls.
These are scheduling choices, not vendor latency guarantees. DONE yields no work.

`ActionEvent` contains the existing public `ReadEvent` transport envelope plus
physical `txComplete` and `responseConfirmed` evidence. The latter means the
application has excluded local echo and foreign/late-response ambiguity. An
identical FC06 frame alone is insufficient. The core validates source/address,
function, length, CRC and echoed register/value using the existing codec.

Wrong operation/step/target/generation, duplicate terminal events, backward time
and malformed envelopes reject without changing context. A correlated transport
failure, malformed frame, exception, cancellation or deadline instead consumes
the event into a retained failure. Payload decoding only updates observations
after all frame/source/timing checks succeed. Delayed delivery may use an on-time
qualified closure; no new transaction is yielded beyond the absolute deadline.

Admission belongs to the application. `writeEvidence.txAccepted` and `txComplete`
retain transmission independently of `execution` (NOT_TRANSMITTED,
ACKNOWLEDGED, REJECTED or UNKNOWN). An acknowledged command still has
NOT_OBSERVED completion until a later checked status matches the requested
effect. Partial/full TX without a confirmed reply remains UNKNOWN. Later read
failure preserves write acknowledgement and the last valid raw observation.
Contexts copy bounded write, last-observation and failure evidence; borrowed
event bytes are not retained. No timeout, cancellation, recovery or poll replays
the write.

## Application reservation and stop admission

The standalone `App` shares one action reservation across common, native and
console routes. Active records retain it through the write, observation waits
and reads. A wait holds no bus transaction. Compatible non-consuming reads and
other addresses can use the same existing `BusOwner`.

A physical-address uncertainty bit survives result release, reset and host
recovery. An uncertain command or acknowledged command without observed effect
blocks another ordinary action on that address. Explicit stop can reconcile the
conflict after a checked stopped-state report, without changing the historical
interrupted outcome. Releasing a failed stop's result frees the dedicated stop
record so a later explicit stop can run; it does not clear the uncertainty bit.

The first actual or possible action TX invalidates earlier cached state with an
explicit `invalidated_us` watermark while preserving raw observations. Release
also invalidates host origin/reference generation. Rejected admission and
unsent cancellation leave those assumptions intact. Fresh explicit reads can
restore current state; action result observations keep their own provenance.

Stop policy and all capacity checks precede supersession. One dedicated frontend
record, one console correlation and the owner's urgent transaction/result quota
are reserved for stop. Rejected/unsupported/full admission leaves active work
and its queue intact. Accepted stop cancels only unsent same-address action work
through `cancelUnsent`; an in-flight transaction settles and its evidence remains
retained. Its unsent continuation is then cancelled. Stop uses priority admission
without a readiness refresh. Transport faults still require explicit recovery.
`interrupted_by_stop` remains explicit even if an already in-flight final
observation establishes completion before settlement; actual completion and the
accepted interruption are both retained.

Stop does not depend on host origin, scales, fresh position, idle state,
alarm-free state or a home switch. Unconnected, disabled and unknown inputs remain
different observations; no input changes occur implicitly. Dependent input
configuration belongs to prompt 15.

## Console and bench restriction

| Common console command | ESS-native spelling |
| --- | --- |
| `enable [address]` | `profile ess_rs enable [address]` |
| `motor-release [address]` | `profile ess_rs release [address]` |
| `alarm-clear [address]` | `profile ess_rs clear-alarm [address]` |
| `stop normal [address]` | `profile ess_rs normal-stop [address]` |
| `stop direct [address]` | `profile ess_rs emergency-stop [address]` |

`release operation_id` remains explicit host result release. `cancel` remains
local cancellation, `recover` host transport recovery and `reset` local counters.
Admission replies and terminal `action` JSON lines have independent command and
operation IDs. `result` inspects retained evidence without consuming it. The
console retains a stop admission reply under output pressure as compact fields,
then emits it after the currently blocked line and before terminal delivery.
Ordinary output/correlation pressure cannot consume its reserved slot or erase
either action outcome. Additional stops reject or are counted as dropped before
effects if their reserved output slot is still occupied.

The Python harness offers explicit one-attempt action commands and strict
terminal validation. It does not add actions to read/load campaigns or retry
writes. `caps` distinguishes implemented write preparations from
`actions_qualified` on the actual application.

The bench application currently sets `actionTimingQualified=false`: independent
TX/RX/DE timing and FC06 echo-source qualification are missing. Physical action
admission returns `timing_unqualified` before TX. Native fake tests can supply
qualified evidence; they do not lift this hardware gate. Stopped-state actions
and all dynamic stop/motion claims remain NOT RUN. Prompt 09 can reuse the real
software path, but live moves require these gates plus its own setup/units and
independent stop prerequisites.
