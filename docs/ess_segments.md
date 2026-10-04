# ESS stored position and speed records

Stored records configure external-input-controlled PT and PV modes. They do
not create a serial trajectory or a serial segment-start command. Record
configuration is independent of having an external trigger fixture; execution
requires its own documented wiring, input assignment and stop prerequisites.

## Source and layout

The original [function manual](vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf)
physical PDF pages 22–24 and 75–77 were visually reviewed against the
[register ledger](reference/ess_rs_registers.json). The ledger owns addresses,
counts, widths, ranges, issues and reserved slots. The linked
[operation inventory](reference/ess_rs_operations.json) tracks read and write
obligations separately for every expanded record.

| Record family | Stored capacity | Meaning and access disposition |
| --- | --- | --- |
| `POSITION_SEGMENT_01` through `POSITION_SEGMENT_16` | 16 | Two pulse words, positioning speed and native acceleration/deceleration time words; the sixth word of each layout is reserved and inaccessible |
| `SPEED_SEGMENT_01` through `SPEED_SEGMENT_16` | 16 | Speed and native acceleration/deceleration time words |
| `SEGMENT_START_SPEED_01_VALUE` through `SEGMENT_START_SPEED_16_VALUE` | 16 | Starting-speed word shared by PT and PV for the corresponding index |
| `EXTERNAL_POSITION_MODE` | One shared setting | Relative or absolute positioning for external positioning/PT; does not change serial motion flags |
| `PV_TRIGGER_MODE` | One shared setting | Level-valid or rising-edge-valid PV trigger; does not select serial position interruption |

Indexes identify the manual's records 1–16. Neither index zero nor index 17
is a record. A position read includes the five documented words and stops
before the reserved sixth word. A speed read includes three words; a shared
starting-speed read includes one word. No grouped write may span a reserved
word, and no missing address is invented.

The position target preserves both raw words and an unsigned assembled value
only when the retained word order is established. Signed meaning and physical
command scale remain separate qualifications. Every stored target pair has
an unavailable typed-write disposition: [prompt 12's policy](ess_pair_writes.md)
establishes no admissible FC10 window for those pairs, a five-word subset or a
whole record. Two FC06 writes are not a substitute. A candidate combining an
unsupported pair with otherwise legal scalar changes fails before any write.

PT positioning speed is documented as 0–3000 r/min and native ramp words as
0–2000 ms. The PV page describes signed speed, while its appendix range is
printed ambiguously as `-3000 -3000`. Shared starting speed is documented as
-180–180 rpm. Negative wire encoding is not established by these prose
ranges. Retain raw negative/unknown observations; do not infer two's complement
or use a contradictory default as an acceleration conversion. Native time
parameters remain distinct from physical acceleration.

## Configuration and uncertainty

The installed [Segments.h](../include/MotorControlRS/profiles/ess_rs/Segments.h)
exports `SegmentKind`, `SegmentObservation`, `prepareSegmentRead`,
`prepareSegmentSettings` and `getSegment`. The wrappers reuse the existing
caller-owned `DriverContext`, `DriverRequest`, `DriverPrerequisites`,
`PreparedDriver`, `nextDriver` and `advanceDriver`; they introduce no second
sequence, bus queue or I/O owner. `DriverGroup::POSITION_SEGMENT`,
`SPEED_SEGMENT` and `SEGMENT_START_SPEED` select the real layouts. Select
`SEGMENT_SPEED`, `SEGMENT_ACCELERATION`, `SEGMENT_DECELERATION` or
`SEGMENT_START_SPEED` explicitly; `SEGMENT_PULSE_TARGET` selects the visible
unsupported pair-write case. Each admitted context copies its request,
qualifications and prior observation. Callers supply events/time and keep
the context unchanged between API calls; no retained caller frame pointer,
allocation or transport call enters the core.

A complete position or speed update has at most three selected scalar fields,
six write/readback transactions and a 15-byte maximum response; a shared
starting-speed update has one field and two transactions. The existing
eighteen-step settings storage also serves the wider I/O group. The standalone
keeps one replaceable indexed-record baseline; updates require that exact
kind/index/target/generation. Admitted operations retain their own immutable
copy and evidence, so selecting a different cached record does not rewrite
unread historical results. The existing bounded console input, command and
retained-result capacities apply; no per-record queue or persistent library
cache is created.

`getSegment` publishes only a complete successful read, with output unchanged
on error. It preserves raw scalar words, both pulse words and provenance.
The optional word-order qualification must come from the exact current
target/generation; the resulting `pulseBits` remains unsigned raw bits,
not an established signed native displacement or physical distance.

Preparation validates the whole selected candidate, index, native ranges,
target/configuration generation, previous observations and stopped-state
external effects before yielding work. Reads require no trigger fixture.
Safe stopped configuration does not require optional home/limit switches or
output loads. It does require that external triggering cannot reinterpret a
record update as a live action; unwired, disabled and unknown inputs are
different states, and inputs are never reassigned implicitly.

An update consists of checked single-word writes followed by separate checked
readbacks. There is no rollback, automatic retry, save or invented atomic
application guarantee. Requested, acknowledged, read-back and active values
remain distinct. A partial update or lost reply retains exact progress and
possible effects. Accepted writes invalidate dependent prepared knowledge;
historical results retain the original target and generation. Local
cancellation settles transport and never sends a motor stop or restores a
previous setting.

The standalone routes use the same public preparation and sequence:

```text
profile ess_rs segment position INDEX read [address]
profile ess_rs segment speed INDEX read [address]
profile ess_rs segment start INDEX read [address]
profile ess_rs segment position|speed INDEX set speed VALUE acceleration VALUE deceleration VALUE [address]
profile ess_rs segment start INDEX set value VALUE [address]
profile ess_rs driver read [address]
profile ess_rs driver set position-mode 0|1 interruption 0|1 [address]
```

Only explicitly named fields change. `position-mode` and `interruption` are
the existing typed external PT/PV control settings, not serial start flags.
Strict integer parsing, index/range checks and duplicate-field rejection
match direct API validation. An unsupported `target` candidate cannot leave
earlier scalar fields applied. No device setting changes on a read, probe,
ordinary move preparation or startup.

The setting-only echo/readback policy, where explicitly qualified by the
application for the exact safe candidate, preserves an unconfirmed FC06
frame as `UNKNOWN` execution with acknowledgement false. Confirmed matching
FC03 readback establishes the observed stored value; it does not retroactively
confirm the FC06 source, active engine interpretation, persistence or motion.
Missing, malformed, mismatching or unconfirmed readback remains uncertain and
prevents further writes. The policy never permits replay after a timeout.

## External selection and execution

The manual has 16 stored records, but the ESS appendix exposes only four
inputs X0–X3. One terminal must serve the PT or PV trigger, leaving at most
three independent selector inputs. Physical PDF page 23 explicitly states a
maximum of eight externally selected segments for four-input drives; page 24
repeats that limit in the PV section. Sixteen stored records do not establish
that all sixteen can be selected simultaneously on this model.

PIN0–PIN3 encode the manual's binary selection. A fixture must establish which
selector functions are assigned, connected and controlled, how unassigned
selector bits behave, and which records are actually reachable. An absent
input is not a verified constant zero; duplicate terminal/function ownership
does not create another independent selector.

PT requires selector inputs stable for at least 5 ms before and after its
trigger. This requirement appears in the PT notice on physical PDF page 23;
the PV section supplies no independently documented equivalent timing rule.
PV uses its own selected level/rising-edge policy. The hardware manual's
minimum input pulse duration also remains an external fixture obligation,
not a scheduler-generated waveform.

Physical execution requires the correct known trigger assignment, connected
trigger/selector wiring, current logical input observations, verified
polarity and edge/level behavior, selected record parameters and an available
qualified stop envelope. Disabled, unwired, unknown or stale required inputs
block that execution without blocking storage reads or independently eligible
serial motion. No library or console command generates an external trigger.

The current free-shaft bench has power and RS485 only. External-triggered
motion is therefore NOT RUN. Register read/write/readback evidence does not
qualify trigger transitions, selectability, ramps or shaft movement.

The current [bench evidence](reports/ess_release_16_2026-10-04.json) reads
position, speed and shared starting-speed records 1 and 16, preserving raw
words and timing. The [first campaign](reports/ess_release_16_attempt1_2026-10-04.json)
changes all three scalar fields in PT record 1 and PV record 16, verifies
matching stored readbacks, explicitly restores the original values and
verifies restoration. All other records retain native coverage; these narrow
physical cases do not qualify every index, value, activation or persistence.

Shared starting-speed record 1 has a retained hardware discrepancy. A single
requested change from 0 to 1 produced a checked FC06-shaped frame with
unconfirmed source, followed by confirmed readback 0. The operation correctly
returned `READBACK_MISMATCH`, retaining execution `UNKNOWN` and uncertainty.
Delayed non-changing reads minutes later still returned 0, excluding observed
readback lag as the explanation over that interval. No write was replayed and
no speculative restore was sent. Independent response-source/device-acceptance
evidence or exact firmware semantics is still required to identify the cause.
The read path remains usable; the failed write is not hardware-qualified.

On the tested ESP32-S3 ABI, `DriverContext` occupies 3096 bytes,
`DriverRequest` 88, `DriverPrerequisites` 848 and `SegmentObservation` 152.
The application retains larger settings contexts/cache and console storage
in PSRAM; capture/driver state and stacks remain internal. The report records
actual memory watermarks and service/capture gaps for the named image.

FieldCore's actual RS485 task was reinspected read-only. Its bounded
request/work/result workflow remains a useful integration reference; its
request-identical prefix stripping, measurement payload and immediate active
cancellation cannot stand in for retained checked FC06 evidence, stored
readback settlement or physical TX settlement. The core imports no FieldCore,
UART, MCU, console or scheduler types.
