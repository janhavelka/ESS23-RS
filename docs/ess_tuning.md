# ESS filters, tracking and tuning settings

These are native drive settings. They do not implement a common torque/current
motion mode or establish physical filter delays, gain transfer functions,
arrival tolerances or collision sensitivity.

The original function-manual physical pages 73, 78 and 79 and the
[register ledger](reference/ess_rs_registers.json) own source facts. The
[operation inventory](reference/ess_rs_operations.json) accounts separately for
reads, writes, CLI access, native tests and physical evidence.

## Reviewed fields and unresolved units

All twenty accessible fields are unsigned single words with the documented
`RW/S` access marker. That marker does not establish activation or persistence
timing. No pair write, gain sweep or settings initialization is implied.

| Group | Ledger records | Native bounds and interpretation |
| --- | --- | --- |
| Filters/tracking | `INPUT_FILTER` | 0–65535; terminal-filter coefficient, no time formula |
| Filters/tracking | `PULSE_LOW_PASS_FILTER` | 0–1024; smaller coefficients strengthen filtering, no time formula |
| Filters/tracking | `POSITION_ERROR_ALARM_THRESHOLD` | 1–65535; position-deviation code, count basis unspecified |
| Filters/tracking | `POSITION_ARRIVAL_WINDOW` | 1–256; positioning-error code, count basis unspecified |
| Filters/tracking | `ARRIVAL_TIME` | 0–200; native arrival-time code, unit and timing rule unspecified |
| Filters/tracking | `PULSE_MEAN_FILTER` | 0–512; larger coefficients strengthen filtering, no time formula |
| Current loop | `CURRENT_LOOP_KP_MULTIPLIER`, `CURRENT_LOOP_KP`, `CURRENT_LOOP_KI`, `CURRENT_LOOP_KC` | Each 0–65535; native gain codes, no physical transfer formula |
| LA | `LA_SPEED_KP1`, `LA_SPEED_KV1`, `LA_SPEED_NODE1`, `LA_SPEED_KP2`, `LA_SPEED_KV2`, `LA_SPEED_NODE2`, `LA_SPEED_FEEDFORWARD_KVF`, `LA_POSITION_KI` | Each 0–65535; native codes, including nodes with unspecified units |
| Later collision fields | `COLLISION_THRESHOLD_0122` | 200–4000; position-error code, count basis unspecified |
| Later collision fields | `COLLISION_CURRENT_0123` | 20–100%; ratio of collision-return current to normal-motion current, not a commanded-current mode |

The earlier `COLLISION_THRESHOLD_003B` and `COLLISION_CURRENT_003C` rows have no
access marker. Their stated ranges are 50–4000 and 20–100 respectively; the
threshold lower bound differs from the later row. Their relationship and model
applicability are unresolved. They remain separate ledger records. The codec
refuses reads and writes to those words; neither a read window nor a setter
bypasses that policy. The undocumented gap between LA and the later collision
fields is also excluded from requests.

Native arrival-time codes have no established millisecond conversion. Position-deviation and arrival
codes are not automatically encoder counts or command subdivisions. LA speed
nodes are not assigned RPM from their names. The source provides no required
ordering between the two nodes, gain stability envelope or coefficient-to-time
conversion. Qualification must supply any such additional assumptions rather
than deriving them from defaults.

## Ownership, effects and qualification

[Tuning.h](../include/MotorControlRS/profiles/ess_rs/Tuning.h) exports
`TuningParameter`, `TuningParameterInfo`, `TuningObservation`,
`prepareTuningValue`, `prepareTuningRead`, `prepareTuningSettings` and `getTuning`.
These reuse `DriverRequest`, `DriverPrerequisites`, `DriverContext`, `nextDriver`
and `advanceDriver`; there is one existing settings sequencer and application
bus owner.

The four `DriverGroup` values are `FILTERS` (six fields), `CURRENT_LOOP` (four),
`LA` (eight) and `COLLISION` (two). Their selected-field masks are local to each
group, starting at bit zero; they are not the older global `DriverField` mask.
`prepareTuningValue(request, parameter, nativeValue)` chooses the group for an
empty request, validates the reviewed native range, and rejects mixed groups.
Its errors leave the request unchanged. `tuningParameter(group, slot)` accepts
only a valid zero-based field slot; `laStageParameter(stage, field)` accepts
stage 1 or 2 and `LaStageField::KP`, `KV` or `NODE`. Invalid selectors return
`TuningParameter::NONE`. The helper selects the actual six stage fields, with
feedforward and position Ki remaining separate.

`tuningParameterInfo` reports register/range/access metadata. The two earlier
collision entries remain visible with `accessReviewed=false`; metadata does not
authorize I/O. `prepareTuningValue` rejects them as unsupported before traffic.

Read windows are four plus two words for filters, four for the current loop,
four plus four for LA, and two for the later collision fields. Thus every
response fits the existing fifteen-byte retained response storage, and no
window crosses an undocumented hole. `getTuning` publishes only a complete
successful read and leaves output unchanged on failure. It retains native raw
words, known-field bits, target/configuration context and one or two provenance
blocks. Multiple reads are not an atomic motor snapshot.

A full LA update has eight writes and eight readbacks, within the existing
eighteen-transaction settings storage. Each context retains its copied group
and selected values; later cache reconciliation does not rewrite that context.

The application owns transport, time, exact-model identification and any tuning
experiment. The reusable profile prepares bounded work and checks supplied
events; it performs no I/O. Candidates, prerequisite evidence and retained raw
frames are copied into caller-owned contexts. No caller stack buffer is retained.

A grouped candidate validates all selected ranges, target/configuration context,
fresh stopped-state evidence, model identity and exact independently qualified
effects before its first write. Unsupported or unqualified candidates yield no
traffic. Settings are not silently modified at startup, on a probe, during
ordinary move preparation or by cached status queries.

The candidate's native words are copied in `DriverRequest::tuningValues`.
`DriverPrerequisites::previous` must be a complete checked observation of that
same group, target and configuration generation. Existing `controlIdentity`,
`exactModelQualified`, `modelSourceId`, `qualifiedModelCode` and
`qualifiedFirmwareCode` carry independent exact-model evidence. Selected tuning
effects use `tuningEffectsQualifiedFields`, the exact copied `qualifiedTuning`
candidate and fresh `tuningEarliestUs`/`tuningLatestUs` bounds. Stationary,
identity, every previous-settings window and effects bounds cap each write's
deadline without renewal. `driverWriteDeadline` supplies this same budget to
application diagnostics. Native
reads require no speculative physical gain formula; qualification flags default
to unavailable for writes.

Selected settings use one checked FC06 exchange followed by a separate checked
FC03 readback. A later failure or local cancellation can leave a partial update:
the result retains each requested, acknowledged and observed stored value,
accepted-TX effects and uncertainty. There is no rollback or automatic replay.
Only qualified closure within the immutable write budget can establish an
acknowledgement or checked device rejection; on-time evidence may be delivered
later without changing its timestamp.

An explicitly qualified setting-only echo/readback policy may accept an on-time
checked but source-unconfirmed FC06-shaped frame solely to perform a separate
confirmed readback. The write's acknowledgement remains false and its execution
remains unknown. Matching readback settles the stored word, not active behavior,
persistence, torque, stability or physical timing. Failed or mismatching readback
retains uncertainty and prevents later writes.

Changing completion/error thresholds invalidates completion and readiness
assumptions. Filters, control-loop and collision changes invalidate dependent
operation qualifications and settings/state freshness. Historical snapshots and
results retain their original target, configuration generation and raw evidence;
they are not rewritten into a new interpretation. A later read detecting an
external setting change uses the same invalidation boundary.

The application retains one bounded baseline for each of the four actual tuning
groups. Reading another group cannot erase the comparison needed to detect a
later arrival/deviation change. A partial or failed refresh retains the previous
raw observation. If a checked completed window already proves a setting changed,
the application invalidates its baseline and dependent readiness/reference
assumptions even when a later window fails. An unchanged checked prefix preserves
the prior validity; neither case rewrites historical results.

External terminals remain optional. A tuning change with external effects needs
evidence about its affected inputs; unrelated serial control does not acquire a
mandatory switch/load requirement. Declared unconnected wiring, current logical
levels and drive function assignments remain separate facts. No filter update
silently disables or reassigns an input function.

## Console and Python routes

The existing console and Python transport call the same public preparations:

```text
profile ess_rs tuning filters|current-loop|la|collision read [address]
profile ess_rs tuning GROUP set FIELD INTEGER [FIELD INTEGER ...] [address]
```

| Group | Fields |
| --- | --- |
| `filters` | `input-filter`, `pulse-low-pass`, `deviation-threshold`, `arrival-window`, `arrival-time`, `pulse-mean` |
| `current-loop` | `multiplier`, `kp`, `ki`, `kc` |
| `la` | `kp1`, `kv1`, `node1`, `kp2`, `kv2`, `node2`, `kvf`, `position-ki` |
| `collision` | `threshold`, `current` |

Values use strict native integer parsing. Duplicate fields, mixed groups,
unsupported parameters and out-of-range values fail before admission. There is
no bare-address tuning setter. The existing 128-byte input and 20-token console
bounds still apply; the direct API can prepare a complete eight-field candidate.
The transport keeps command correlation distinct from retained operation IDs,
supports interleaved accepted/terminal records and never splits or retries an
oversized or failed write automatically.

The native Console/Python parity check exercises 66 actual terminal records,
every one of the twenty named fields and a complete eight-field LA update with
sixteen transaction steps. Its largest terminal is 2,840 ASCII bytes within
the existing 8,192-byte output bound. The fixture wrapper is test evidence,
not an additional console wire record.

## Available physical qualification

The [implementation-image COM13 evidence](reports/ess_release_18_2026-10-04.json)
records checked reads of all twenty fields before and after a finite
`INPUT_FILTER` stored-code experiment. Native groups returned:

| Group | Raw native words |
| --- | --- |
| Filters | `2, 5, 4000, 5, 10, 512` |
| Current loop | `6553, 1024, 102, 409` |
| LA | `2560, 256, 20, 2560, 128, 100, 10, 2` |
| Later collision | `0, 0` |

The later collision values consistently remain outside the documented ranges;
their `known_fields` mask is zero. Checked transport and preserved raw values
qualify the read path, not these fields' physical meaning on firmware `0x0029`.
They were not changed, aliased to earlier rows or converted to documented
defaults.

The standalone application permits only the explicit input-filter candidate
`2` or `3`, from a previously checked `2`/`3` baseline, with fresh stopped state,
exact bench identity, known unconnected inputs, passive assignments and no
asserted inputs. The finite `2 → 3 → 2` experiment passes stored readback and
restoration. Both FC06-shaped observations remain source-unconfirmed:
acknowledgement is false and execution unknown. Separate confirmed FC03
readbacks establish the stored codes. No physical filter delay, active behavior
or persistent-save qualification follows.

Register readback is the baseline. An experiment must name a small candidate,
why it is appropriate for the actual stopped fixture, its finite duration and
explicit restoration/readback plan. Broad gain sweeps or deliberate instability
are outside this feature. Current-loop/LA gain, collision and arrival/deviation
physical effects remain unqualified unless separately measured on a suitable
fixture. The free-shaft bench supplies no collision mechanism, switches or loads.

Integrated verification passes all 45 native CTest cases, the installed
consumer, all four example builds, generator checks and the 21 inventory tests.
The source PDF and generated descriptors remain unchanged.

FieldCore's current request/work/result and RS485 owner were inspected read-only.
Serialized bounded work is compatible vocabulary. Its eight-byte transactions,
borrowed RX observation, measurement payload and immediate active cancellation
are not replacements for this profile's retained checked frame evidence, honest
partial settings progress and physical TX settlement. No FieldCore, UART, MCU,
console or scheduler type enters the installed profile API.

The [fresh 17/18 audit](reports/ess_release_17_18_audit_2026-10-04.md) repeats
all twenty native reads and input-filter stored restoration on the final image.
Sixty-six actual core/formatter fixtures exercise Python classification, including
previous-settings expiry, delayed echoes and forged codec/transport evidence.
