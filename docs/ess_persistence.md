# ESS save and factory restore

`MotorControlRS::ESS_RS` exposes `prepareSave`, `prepareFactoryRestore`,
`nextPersistence`, `advancePersistence` and `verifyPersistence` in
[`Persistence.h`](../include/MotorControlRS/profiles/ess_rs/Persistence.h).
These functions use caller-owned bounded state and supplied time/events. They
perform no I/O, allocation, delay, recovery, restart or retry. The application
executes the yielded request through its existing owner and checked ESS parser.

The original function manual, physical PDF page26 (printed24), documents
`AuxiliaryCommand::SAVE_PARAMETERS` and `RESTORE_FACTORY` at
`Registers::AUXILIARY_COMMAND`. Both affect all parameters and are ignored
unless the motor is stopped. It specifies no completion flag, worst-case save
duration, safe power-removal delay or durability guarantee. An acknowledgement
therefore means command acknowledgement, never verified persistence.

## Preparation and retained evidence

`PersistencePrerequisites` retains the exact before identity, standard
configuration, motion-state block, responding host tuple and configuration
generation. Raw frames, CRC, expected windows, target/generation and observation
intervals are rechecked. Evidence must be fresh, alarm-free and stopped, with
explicit qualification of stationary state and the selected operation's effects.
The immutable write deadline is capped by the oldest prerequisite's expiry.
Wrong target, stale/malformed evidence or missing qualifications yield no write
and leave output unchanged. Unknown model/firmware and setting codes remain raw.

The standard configuration contains fifteen words; it is a **partial backup**.
`PersistenceContext::configurationSnapshotComplete` remains false. Factory
restore additionally requires an externally retained complete configuration
backup with a source token and a qualified recommissioning route. The standalone
bench also requires a backup/restart/route-back procedure before invoking save.
The current fixture has no motor restart control; neither physical invocation
is enabled by a console flag.

One operation token admits one FC06 write. Repeated `nextPersistence` inspection
returns the same work; the cooperative application prevents duplicate admission.
`advancePersistence` retains accepted TX count, checked raw response, closure
bounds, source qualification, outcome and possible execution. Lost, malformed,
late or source-unconfirmed acknowledgements remain uncertain. Explicit host
recovery and subsequent readback never change that historical write outcome.

Each retained field records its ledger-derived `RW` or `RW/S` notation,
before/readback values and evidence of survival after restart. No access notation
alone proves durability. `verifyPersistence` accepts at most two explicit valid
verification submissions. Live identity/configuration/stopped-state reads
establish live readback only. For SAVE, matching fields after an independently
established actual motor restart can become `VERIFIED_FIELDS`. This proves field
survival, not that SAVE caused it. Unread/changed fields and all-parameter
durability remain unverified. MCU reset, host tuple restoration and elapsed time
cannot provide motor-restart evidence. Factory defaults are never guessed.

## Application and console ownership

```text
profile ess_rs persistence inspect
profile ess_rs persistence plan save|factory-restore
profile ess_rs persistence snapshot
profile ess_rs persistence begin save|factory-restore
profile ess_rs persistence verify
profile ess_rs persistence host before
profile ess_rs persistence finish
```

Inspect/plan are passive. Snapshot acquires the existing exclusive commissioning
lease and executes identity, five configuration windows and three state windows:
nine FC03 requests within one three-second deadline. Every capture uses the same
token; ordinary/urgent producers, monitoring and ordinary host changes are
excluded. The original host tuple is retained even if capture fails. Explicit
`host before` can repair failed UART setup without abandoning the lease.

There are two nonvolatile invocation attempts per firmware boot, one retained
persistence result, and at most two verification captures per invocation,
including failed attempts. Each capture is bounded to nine reads. The next
explicit begin may replace a finished retained result; inspect never consumes
it. Command IDs correlate console replies separately from operation IDs.
Diagnostics report invocation/verification counts, snapshot failure, lease role,
before/after raw frames and timestamps. A fresh snapshot remains visible beside
the historical operation. No persistence action runs in startup, probing,
statistics reset, transport recovery, polling or stress traffic.

Possible TX invalidates configuration, units, polarity, origins, readiness,
pair-decoding and prepared motion assumptions, including a confirmed new address
that still belongs to the original physical axis. Historical raw results survive.
Standalone SAVE finish requires current post-action readback and a settled
original responding tuple. Changed communication requires explicit
recommissioning; factory restore retains manual-intervention status and its
lease. Repeated cached inspection cannot establish verification or freshness.

For a pending custom address, first explicitly confirm a responding candidate
in the existing [`CommunicationContext`](ess_communication.md), then snapshot
and save through the **same commissioning token**. Persistence finish retains
the parent lease. Communication finish requires its own new candidate
confirmation after save or recovery; restoring the UART alone cannot release
it. Custom address activation requires the documented saved setting and DIP
address switches OFF. Baud/format require motor power-on again; their save
requirement remains unresolved. No motor restart or route is manufactured here.

The Python `persistence` command validates strict correlated records.
`persistence-check save|factory-restore` defaults to planning; explicit
`--execute`, `--verify`, `--finish` request bounded steps. Any failed/unknown
step leaves retained evidence and ownership for explicit inspection/recovery;
there is no automatic replay or cleanup persistence write.

Current software and hardware dispositions are in the
[prompt21 handoff](reports/ess_release_21_2026-10-04.md). Physical save, restart
survival and factory restoration remain NOT RUN without the restart/backup/
recommissioning procedure. Register readback does not close those proof gaps.
