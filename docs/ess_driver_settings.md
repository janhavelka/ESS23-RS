# ESS driver settings

The installed [DriverSettings.h](../include/MotorControlRS/profiles/ess_rs/DriverSettings.h)
implements typed, bounded settings reads and stopped-state updates. It performs
no I/O, allocation, retry, save, restore or automatic rollback. Applications
execute `PreparedDriver` through their bus owner and feed checked transport
evidence through the existing public `ActionEvent`.

`prepareDriverRead(context, target, operationId, configurationGeneration, now,
deadline)`, `nextDriver`, `advanceDriver` and `getDriver` read four reviewed
windows: direction/subdivision, over-limit/soft-enable/word-order, both limit
pairs, and external position/trigger modes. The largest reply is 13 bytes.
`getDriver` publishes only a complete successful read; failed refreshes leave
the previous observation unchanged. Separate reads are not an atomic snapshot.
Raw unknown codes remain observable. Limit pairs assemble unsigned bits only
when the observed word order is legal; signed encoding and native units remain
unresolved. Historical raw words are never decoded using a newer order.

`prepareDriverSettings` validates the whole `DriverRequest` and copied
`DriverPrerequisites` before yielding any work. Seven single-word fields are
implemented:

| Field | Legal candidate | Important meaning |
| --- | --- | --- |
| Direction | NORMAL/REVERSED | No automatic host-polarity inversion |
| Subdivision | 400 through 51200 inclusive | Command increments per revolution remain a separate qualified scale |
| Word order | HIGH_WORD_FIRST/LOW_WORD_FIRST | Changes paired words, not Modbus byte order |
| Soft-limit enable | LIMITS_OFF/AFTER_HOMING | Enabling needs qualified existing limits and device homing |
| Over-limit stop | FREE_PARKING/EMERGENCY_STOP | Value zero's exact torque/deceleration behavior remains unresolved |
| Interruption | LEVEL/RISING_EDGE | External PV trigger policy; not serial positioning interrupt bit 3 |
| Position mode | RELATIVE/ABSOLUTE | External positioning policy; not serial command bit 2 |

Positive/negative limit candidates are explicit typed fields, but preparations
containing either reject with `PAIR_WRITE_UNSUPPORTED` before *all* traffic.
Their reads are implemented. [Prompt 12's pair policy](ess_pair_writes.md) admits
neither separate FC06 halves nor a new FC10 limit window. No settings request
implicitly reassigns an input, changes communications or saves the drive.

Updates require a fresh, checked previous `DriverObservation`, exact target,
transport and configuration generations, qualified stationary evidence and an
explicit input policy. Released state is allowed for ordinary settings when
the caller can establish stationarity; a released axis cannot supply the
homed-reference prerequisite for enabling limits. Unknown alarms/running bits
reject. Geometry changes under enabled or unknown limits require an explicit
OFF candidate, which is written and read back first. Geometry changes and
enabling limits together reject until the changed interpretation is reconciled.

Enabling requires the fresh device homing-complete flag, a qualified reference
from the same configuration generation, ordered native limits in the qualified
subset, and their exact corresponding raw pairs. `qualifiedPositiveWords` and
`qualifiedNegativeWords` assert a caller-proved mapping to the supplied signed
bounds. The library compares those words with the checked previous observation;
a cast, guessed signed representation or host origin does not establish that
mapping. The current standalone has no qualification route for this assertion.

Each selected field yields one FC06 and one exact FC03 readback, at most fourteen
transactions. The context retains prior/requested values, checked ACK, readback,
execution disposition, possible-change effects, raw evidence and uncertainty.
No later failure erases completed progress. An ACK without matching readback,
lost reply or partial update leaves explicit uncertainty; no rollback is promised.
Checked device exceptions remain rejections. Active and saved settings remain
unknown because these pages do not specify activation or persistence semantics.

One immutable absolute deadline covers the entire operation. Each new write is
also capped by the original stationary evidence's validity interval. Readback
keeps the operation deadline. Qualified final closure before its budget may be
delivered later; an intermediate completion delivered after expiry cannot start
another transaction. Observation age uses earliest step eligibility and closure
bounds, independently of application delivery.

Contexts, prerequisites and observations are caller-owned bounded values.
Preparation copies input expectations; borrowed event frames last only for the
call and are copied into retained evidence. Treat context fields as read-only
between API calls. Update preparation rejects its own embedded request or
prerequisite as input, avoiding a large stack copy while preserving output on
rejection. `nextDriver` is non-consuming; applications prevent duplicate
admission and hold the same-axis reservation through write/readback gaps.
Cancellation is local; an accepted physical frame settles before its result is
classified. Recovery cancels continuations and never resumes queued updates.

The standalone updates only its configured axis; other addresses remain readable.
At accepted write TX it invalidates dependent origins, limits, prepared operations,
state interpretation, clear/motion qualifications and configuration cache.
Direction/subdivision also clear the command scale; possible direction changes
mark host polarity unknown. Engineering previews/moves/velocity then require an
explicit stationary host polarity declaration, while native intent keeps its
own prerequisites. External trigger/mode changes invalidate input qualification.
Full driver/config reads detecting external changes apply the same effects.
The newly reconciled driver cache has the current host configuration generation;
retained operation contexts and raw evidence keep their original generation.
Old state/config continuations cannot publish under a changed interpretation.

The existing console exposes:

```text
profile ess_rs driver read [address]
profile ess_rs driver set FIELD INTEGER [FIELD INTEGER ...] [address]
```

Fields are `direction`, `subdivision`, `word-order`, `soft-limit`, `over-limit`,
`interruption`, `position-mode`, `positive-limit`, `negative-limit`. Enum fields
use their documented zero/one codes. Values use strict integer parsing; duplicate
fields and incomplete pairs reject. The existing 128-byte line/20-token bound
limits CLI group size; the direct API accepts all seven single-word fields.
Host `axis` configuration remains distinct. Operations use ordinary retained
capacity (eight frontend records), existing command correlation, explicit result
inspection/release, cancellation and the reserved stop/recovery paths. Compact
terminal evidence fits the unchanged 4608-byte output bound.

`python scripts/bench_probe.py --port COM13 --log <new.jsonl> driver-read` performs
one checked read, immutable result inspection and explicit release. It does not
retry or write settings. The Python `Console` can submit explicitly selected
typed groups through the same correlated protocol.

See the [prompt 13 report](reports/ess_release_13_2026-10-04.md) for tests, actual
platform sizes and current read-only evidence. Physical settings changes and
restoration are NOT RUN pending electrical/FC06 response-source and input
qualification. Software-limit movement also needs resolved pair encoding,
homing/reference semantics and a suitable fixture; raw zero limits are no proof.
