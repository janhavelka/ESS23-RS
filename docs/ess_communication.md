# ESS communication commissioning

Prompt 20 adds explicit communication preparations in
[`Communication.h`](../include/MotorControlRS/profiles/ess_rs/Communication.h).
They share the checked FC06/FC03 codecs, `ActionEvent`, `ReadTarget` and
`ActiveSerialTuple`. Transport and tuple selection remain application-owned.

## Source rules

The original function manual physical pages 6 and 69 and hardware manual
physical page 11 establish these settings. Page 26 describes the separate
stopped-state save-all command; prompt 21 owns its implementation.

| Field | Register and source values | Pending effect / qualification |
| --- | --- | --- |
| Custom address | `0x0013`, source 0–255; this API accepts usable unicast 1–247 | Address switches SW2–SW5 must be OFF. Hardware instructions require setting and saving. Activation instant and acknowledgement address are unspecified. |
| Baud | `0x0014`: 0=115200, 1=38400, 2=19200, 3=9600 | Power-on again is explicitly required. Whether saving is additionally necessary remains unresolved. |
| Format | `0x0015`: 0=8N1, 1=8N2, 2=8E1, 3=8O1 | Power-on again is explicitly required. Whether saving is additionally necessary remains unresolved. |

All address switches ON mean factory restore, not address 15. Raw DIP/default
readback does not establish the exact physical switch/model applicability.
The application must supply qualified address-switch evidence for an address
write. Optional unwired external inputs do not block these operations by
themselves. No input assignment, persistence command or restart is implicit.

## Core sequence and evidence

`prepareCommunication` takes one typed `CommunicationRequest`, exact target,
operation ID, before-configuration provenance, observed host tuple, age budget
and candidate-specific stationary/effects/route-back qualifications. It rejects
bad values, stale/mismatched provenance, an already-pending baud/format baseline
or missing prerequisites before yielding
traffic. `nextCommunication` yields one bounded write. Applications must admit
each token only once; repeated preparation queries are not retry authorization.

`advanceCommunication` validates event correlation/envelopes before mutation,
then retains transmission, source confirmation, raw reply and failure evidence.
The write never schedules a follow-up. An address echo at the original endpoint
does not resolve acknowledgement/activation rules; a new-address reply is not
silently accepted as an old-address response. Missing/lost replies retain both
candidate contexts and unknown execution. No write is replayed.

After the write settles, `prepareCommunicationConfirmation` explicitly selects
either the exact before candidate or requested candidate for one FC03 read of
the changed register. The application first establishes the selected host tuple
and settles recovery. There are at most two confirmation attempts in a context;
the caller explicitly arms each, with its own bounded deadline. Both evidence
slots and the original write evidence remain available after failure. Exhaustion
leaves the uncertain session for operator-directed recovery; it never scans or
restarts a write sequence automatically.

The context separates previous/requested/readback register words, original and
candidate endpoints/tuples, and the interface observed responding to a checked
confirmation. A matching stored word can exist while the old tuple responds.
A mismatch can still establish which candidate responded, while preserving the
mismatch outcome. Neither observation proves identity, persistence or physical
activation. Save/restart requirements and `activationUnknown` remain explicit.

## Application ownership and restoration

The standalone application retains one commissioning context in its existing
PSRAM allocation. `BusOwner::beginCommissioning` acquires a generation-token
lease only when settled, excludes normal/urgent producers, invalidates old
producer sequences and preserves retained results. Only `admitCommissioning`
with that token can admit session work. There is no second bus queue or UART.
Active profile continuations and monitoring also prevent entry.

Host configuration and explicit recovery keep this lease. Ordinary `host`
changes, axis changes and normal producers cannot bypass it. Session host
commands reuse prompt 19's adapter/timing/reconfiguration implementation.
Failed configuration keeps the host blocked and both candidates inspectable;
successful UART repair alone does not establish a responding target. Recovery
does not clear write evidence or replay it.
Selecting an already-active, settled host tuple preserves its current candidate
confirmation. An actual host change, failed selection or recovery invalidates
finish eligibility and requires a new explicit confirmation.

After possible transmission, old transport-confidence caches are invalidated.
Session finish requires a settled known host and a checked candidate response
under the current serial generation. An attempt that sent no write may instead
finish on its unchanged original tuple. A selected responding endpoint starts
a new binding generation and invalidates dependent axis assumptions. Historical
session evidence remains inspectable after finish until an explicit new begin.
Console pressure cannot consume this context or release the lease.

## Console and finite host procedure

Execution commands use the public preparation through the same application
callback. Plan reports source rules and fixture availability; it does not
certify execution readiness. Begin checks the current exact prerequisites:

```text
profile ess_rs communication inspect
profile ess_rs communication plan baud 38400
profile ess_rs communication begin baud 38400
profile ess_rs communication host before
profile ess_rs communication confirm before
profile ess_rs communication finish
```

`address` accepts an integer; `baud` accepts a documented nominal rate; `format`
accepts 8N1/8N2/8E1/8O1. Plan/begin optionally take the currently bound address.
`host` and `confirm` each require `before` or `requested`; they never choose or
scan a candidate automatically. Inspect returns retained state and the current
host snapshot. A successful command admission with `pending:true` is not a
completed write/read. Inspect until the finite attempt settles before another
step. Use ordinary explicit `recover` only for a transport recovery requirement;
it cannot release commissioning ownership.

The Python `communication` mode sends one explicit command. The finite
`communication-check` procedure prints/retains a plan first, executes only when explicitly
selected, bounds its inspection polls, and never automatically saves, restarts,
replays writes or guesses recovery. Candidate confirmation and finish are
explicit options. A failed/lost command stops the procedure; any acquired lease and
retained session evidence remain for inspection.

The shipped bench application has no qualified restart/route-back fixture and
therefore rejects begin before TX. Native tests inject simulated qualification
into the actual application; those flags are not available as console overrides.
Physical address/baud/format change, restart, readback and restoration are
**NOT RUN**. This limitation is independent of the confirmed power/RS485-only,
free-shaft setup. Prompt 21 must revisit custom-address activation using its
explicit save operation and an actual available restart/recommissioning route.
Prompt 22 receives no permission to write during discovery.
