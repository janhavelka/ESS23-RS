# Drive profile and complete native API contract

Prompt18 implements [typed native tuning](ess_tuning.md): twenty reviewed
parameters reuse bounded settings preparation, sequencing and raw observation.
Ambiguous earlier collision access, unknown physical scaling and independent
effects qualification remain guarded before traffic.

Prompt17 adds [typed control configuration](ess_control_settings.md): two bounded reads and checked single-word updates with whole-candidate model/effects/current validation. These are settings, not commanded current or torque modes. Raw algorithm3 and conflicting percent/current semantics remain explicit.


Prompt16 implements [stored PT/PV records](ess_segments.md) through the existing bounded typed settings mechanism. All16 records are readable; reserved slots remain unavailable and pulse-pair writes unsupported. Storage, externally selectable combinations and physical qualification remain separate.


Prompt14 implements [ESS homing methods33/34/35](ess_homing.md), with [all35 source dispositions](ess_homing_methods.md), existing active auxiliary7 and qualified zero offset only. No I/O setter, collision alias or new paired span is introduced.

Prompts 05–06 implement bounded [typed identity/configuration/state reads](ess_reads.md), common/profile routes, passive per-block status/health and finite opt-in polling. The [linked inventory](reference/ess_rs_operations.json) separates read/write/action, native and hardware evidence. Model/firmware compatibility, physical feedback units, readiness and motion remain unqualified; these reads perform no writes.

This is the accepted profile-layer design for `MotorControlRS`. It specifies how
each selected drive exposes its complete documented functionality alongside
the [common axis API](axis_contract.md). The first implementation supplies the
[ESS register catalogue](reference/05_ess_register_catalog.md), generated C++
descriptors and native enums. Checked raw ESS codecs and a minimal probe are
also implemented, with limited read-only bench evidence. Typed identity/configuration/state reads, bounded actions and finite relative-position sequencing are implemented; physical action/motion and broader hardware qualification remain open. The installed [relative-position API](ess_position.md) shares common target preparation and the existing action evidence boundary.
ESS-RS is the first target; Leadshine iEM-RS
is a contrasting design case whose concrete model and firmware still require
selection.

The [architecture](architecture.md) defines ownership and the three layers.
The [CLI contract](cli_contract.md) provides console access to these same
library operations. Framework choice does not change the profile contract:
Arduino, ESP-IDF, native applications and a future FieldCore module use the
same types, validation and sequencing.

## Profile identity and scope

A profile means an explicit combination of manufacturer, product family,
model or enumerated model set, protocol variant, hardware revision where
relevant, and supported firmware revision or verified revision range.
Sharing a manufacturer, RS485 connector, Modbus function, or register address
does not establish compatibility. Profile versions also identify the library
contract revision and source documents on which their interpretation rests.

Each profile declares:

- Its exact identity, axes/addressing model, transport/framing requirements,
  request/response bounds, and required transaction features.
- Supported common modes and their restrictions, plus all family-native
  operations. Availability can depend on firmware, drive mode, configuration,
  physical inputs and established coordinate references.
- Document versions and hashes, physical PDF page references, applicable
  model sections, unresolved differences, and qualification evidence.
- Identification observations the drive can actually supply. An explicit
  caller-selected identity remains an assumption until verified; unavailable
  identity fields are not filled with a guessed model or firmware.
- Discovery and minimal non-changing probe support, or an explicit reason
  it is unavailable/unresolved. Use the [discovery contract](discovery_contract.md)
  for exact query evidence, side effects, response/timing bounds, candidate
  sets and manufacturer grouping; manufacturer identity is not inferred from
  a generic valid frame.

Selecting a profile performs no I/O. Identification is a separately prepared
read operation, where documented. An unknown model or firmware remains
unqualified even if familiar reads happen to succeed. Selection must not
probe compatibility by writing settings or issuing motion.

Full coverage means all documented functions applicable to the selected
profile, including specialized configuration, diagnostics and workflows.
It does not mean every product sold by that manufacturer. Common operations
provide shared semantics; typed family extensions preserve capabilities
that do not fit those semantics. A generic register reader/writer is useful
for diagnostics but cannot substitute for documented native API coverage.

## Coverage ledger

Extend the existing ESS register ledger with linked, reviewable operational
coverage as typed implementation proceeds. Keep register transcription in one
source and generate documentation from it where useful. The operational fields
below are requirements, not completed coverage. Expand every applicable vendor register,
object, command value, meaningful bitfield and indexed record, including
reserved entries needed to account for the source map. Link command prose,
appendix rows and diagrams that describe the same feature. Record differing
definitions as conflicts until resolved; do not quietly choose one.

| Ledger field | Required content |
| --- | --- |
| Identity and evidence | Profile/model/firmware applicability; vendor label; document revision/hash; physical page and section/table/diagram; vendor clarification or observed evidence where available |
| Wire representation | Address, index/subindex or opcode when established; register count; field width; signedness; byte/word order; masks; request grouping/count limits; frame and exception forms |
| Value meaning | Native unit, scaling, resolution, valid range, defaults and model-dependent values; enum alternatives; reserved values; common-unit conversion and quantization constraints |
| Access | Read-only, write-only, read/write, read-to-clear, write-trigger, or other documented behavior; separate read and write contracts; required device state and other prerequisites |
| Effects | Motion/enable/release, coordinate changes, input/output changes, alarms, destructive reads, pending-command effects, parameter dependencies and conflict classes |
| Persistence/application | Volatile or persistent behavior, save/reset/power-cycle requirements, application timing, flash-write effects where documented, readback requirements and cache invalidation |
| Transaction and sequence | Expected reply and echo; acknowledgement versus effect evidence; observation/completion requirements; deadline/refresh requirements; uncertainty and retry classification; partial-failure behavior |
| Public access | Typed native operation or parameter descriptor; common operation mapping when valid; CLI grammar; read/write/action distinctions; unsupported common mappings explained |
| Coverage status | Independent documentation, device-availability, implementation and qualification states, with reasons and evidence; no single ambiguous `supported` flag |
| Verification | Parser/builder and sequence test references, unit/range/word-order cases, negative cases, hardware record and exact tested identity/settings, outstanding limitations |

An indexed typed accessor can cover a repeated table; its bounds and all
documented instances still appear in the ledger. A combined status decoder
can cover multiple flags, but each flag retains its polarity, unknown-value
handling and meaning. A read/write parameter has two access obligations.
Reserved slots are accounted for without offering arbitrary writes to them.

Keep these distinctions visible in generated documentation and capability
queries:

| State dimension | Examples and consequences |
| --- | --- |
| Documentation | Resolved; ambiguous; conflicting; not documented. An unresolved semantic is not a guessed constant or unit. |
| Device availability | Documented for this identity; absent for this identity; conditional; external-input-only; reserved. Absence needs evidence. |
| Implementation | Planned/unimplemented; partial; implemented. A missing backend operation cannot be described as a device limitation. |
| Qualification | Not tested; native protocol/sequence tests passed; hardware checked for named model/firmware/configuration. One tested operation does not qualify the whole profile. |

Unknown and unimplemented operations remain in the coverage denominator.
They block a claim of complete coverage. A documented external-input-only
mode requires its available serial configuration and observation API, plus
the explicit external handoff contract; it does not require inventing a
serial trigger. A common operation that the selected drive cannot express
returns an unsupported result before yielding a transaction.

## Typed native surface

The ESS family namespace is `MotorControlRS::ESS_RS`. The table distinguishes
existing files from planned operations; do not create placeholder headers.

| Path / implementation state | Responsibility |
| --- | --- |
| `include/MotorControlRS/Profiles.h` (planned) | Profile identity, capability and coverage metadata shared by applications |
| `include/MotorControlRS/profiles/ess_rs/Codec.h` | Stateless bounded frame builders, validators, decoders and checked response parsers |
| `include/MotorControlRS/ReadOperation.h` | Portable read targets, application-supplied events, timing evidence and declared wiring |
| `include/MotorControlRS/profiles/ess_rs/Reads.h` | Bounded typed identity/configuration preparations, observations and read capabilities |
| `include/MotorControlRS/profiles/ess_rs/Registers.h` | Resolved register identities and typed descriptors, including access and widths |
| `include/MotorControlRS/profiles/ess_rs/Types.h` | Existing generated family enums; future handwritten observations and operation types belong in separately owned headers |
| `include/MotorControlRS/ActionOperation.h` | Explicit action/stop policies and caller-supplied transaction evidence |
| `include/MotorControlRS/profiles/ess_rs/Actions.h` | Bounded enable/release/alarm-clear/normal/direct-stop preparations and contexts |
| `src/profiles/ess_rs/` | Codec/catalogue implementation, typed reads in `Reads.cpp` and bounded actions in `Actions.cpp` |

Expose named read preparations, typed configuration preparations and explicit
actions. Native operations can use the `prepare...` vocabulary and the same
caller-owned `OperationContext`/`advanceOperation` contract as the common
layer. Final family function signatures and resolved types are established
against the completed ledger, rather than invented here.

Use distinct types for quantities with different meanings, bounded terminal
and segment indexes, and family-specific enums for native choices. A typed
parameter interface is acceptable when its descriptor fixes the value type,
access, units, validation and side effects. A string key or bare address plus
an unchecked integer is not a typed extension. A read-only field must not
acquire a setter through a generic parameter API; a write-only trigger must
not masquerade as a stored setting. Named action preparations expose motion,
enable/release, homing, save, restore and communication changes explicitly.

Both common and native operations call the same codec and preparation logic.
They do not call console parsers, UART functions, platform clocks, logging,
allocation or storage. Any family-native feature advertised as implemented
must be reachable through callable API and the full standalone bring-up CLI.
The CLI may group many typed parameters under one command family; it may not
implement extra register behavior that the public API lacks. See
[CLI access parity](cli_contract.md) for the console contract.

## ESS-RS inventory required for complete coverage

The inventory below fixes coverage categories, not resolved C++ constants.
It is grounded in the local original
[function manual](vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf),
particularly the **ESS-RS appendix on physical PDF pages 68-79**. All ESS page
numbers below are physical PDF pages, counted from one. The DM-PR appendix
on pages 80-90 is a different family and is excluded from this profile.
Consult the [implementation reference](reference/01_implementation_reference.md)
and [hardware manual](vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf)
for conflicts and applicability limits. This table defines required coverage.
The raw codecs and minimal model probe already implement part of it, with
limited read-only bench evidence; complete typed coverage remains pending.
Track implementation and hardware qualification separately for each operation.

| Native group | Required serial API coverage and distinctions | Evidence |
| --- | --- | --- |
| Identity and observation | Read drive identification/firmware, current address and raw switch status; alarm/motion flags, actual I/O, current speed and paired position. Preserve unknown bits/codes. Enabled/released polarity and feedback units need exact decoding. | 68-69 |
| Driver and communications | Read/write direction, subdivision, custom address, baud, serial format, over-limit stop behavior, soft-limit enable and 32-bit word order. Configuration changes expose their application/restart requirements. | 5-6, 69-70 |
| Position and velocity parameters | Read/write speed/jog speed, acceleration/deceleration, positioning start speed, positioning speed/ramp settings and paired target pulse count. Native fields remain distinct where units or ranges are unresolved. | 14-18, 70 |
| Motion commands | Write-only position, speed and homing starts; relative/absolute choice; ignore-versus-interrupt behavior; normal and emergency stop. Do not read a command as if it were configuration. | 13-14, 25, 70-71 |
| Auxiliary actions | Explicit release, enable, alarm clear, current-position clear, factory restore and save-all preparations. Position clear changes device coordinates; it is not a host-origin setter. Save/restore require stopped state and can be ignored otherwise. | 26, 71 |
| Homing and software limits | Read/write homing auxiliary options, method, search/return speed, acceleration/deceleration, offset pair, positive/negative limit pairs and collision-homing settings; expose method prerequisites and completion evidence. Account for the method diagrams. | 19-20, 28-29, 32-67, 72-73, 79 |
| Digital inputs and outputs | Read/write polarity, X0-X3 function assignments, Y0/Y1 assignments and custom output values. Include explicit no-function/disabled assignment and every documented choice: origin/limits, release, stops, position/speed/JOG inputs, homing, PT/PV triggers and segment-selection inputs. | 27-28, 73-74 |
| Multisegment positioning | Read/write I/O relative/absolute mode and each of the 16 position records: paired pulse target, speed, acceleration and deceleration. Account for each reserved slot without assigning invented behavior. Execution is external-input-only. | 13, 20-24, 75-76 |
| Multisegment speed | Read/write PV trigger level/edge selection and each of the 16 speed/acceleration/deceleration records; cover per-segment starting speeds shared by PT/PV. Execution is external-input-only. | 13, 20-24, 75-77 |
| Operating algorithm, encoder and current | Read/write operating algorithm/open-loop selection, encoder resolution, maximum effective current, closed-loop maximum/base, open-loop maximum and lock-current percentages, and lock time. Keep model-specific limits explicit. | 77-78 |
| Filtering, tracking and tuning | Read/write I/O filtering, pulse low-pass/mean filters, position-deviation alarm threshold, arrival/error window and arrival time, current-loop Kp multiplier/Kp/Ki/Kc, LA speed Kp/Kv stages and nodes, feedforward Kvf and position Ki; include additional collision-return threshold/current fields. | 78-79 |
| Protocol diagnostics | All documented exception forms and raw vendor codes; checked FC03/06/10 request/reply forms, read limits, write echoes and configured paired-word decoding. Keep device alarms separate from protocol outcomes. | 6-12, 29-30 |

The original page 13 explicitly restricts multisegment position/speed
execution to external input signals. The generic text on that page mentions
input registers through `0047h`; the ESS-specific tables on pages 73-74
document **X0-X3 and Y0/Y1**, with other polarity-mask bits reserved.
Serial configuration of these modes is in scope. A fictional serial
`startSegment` operation or extra terminals borrowed from another appendix
are not. Input reconfiguration can change the interpretation of an already
asserted signal, so configuration writes must declare that interaction.

Pages 17-18 describe serial signed-speed start separately from external
JOG+/JOG- input functions. Their shared parameter names do not establish a
serial momentary-jog watchdog. External trigger ownership and prerequisites
remain application responsibilities even when the profile configures them.

Preserve the unresolved items in the implementation reference. In addition,
the ledger must resolve or retain uncertainty for the two collision-return
parameter locations shown on pages 73 and 79 instead of assuming aliases;
the `RW/S` access notation and actual persistence/application rules; and
performance descriptions that refer to register identifiers outside the ESS
table. Conflicting defaults, model-dependent current values, malformed
signed ranges, request limits and unclear scaling must not become guessed
API constants. Native raw values may be observable with a documented wire
format while an engineering-unit conversion or write remains unavailable.

### Unused and disabled terminals

ESS function-manual pages 27-28 explicitly define function value `0` as
"the terminal has no function". The ESS appendix on pages 73-74 assigns it to
X0-X3 at `0x0041`-`0x0044` and Y0-Y1 at `0x004C`-`0x004D`. Existing
`InputFunction::UNDEFINED` and `OutputFunction::UNDEFINED` represent this known
no-function value, not an unknown decode. Typed I/O operations must expose it
as an explicit disabled/none choice for each terminal. These enum values and
raw codecs and the [typed I/O settings operations](ess_io.md) exist. Register-path
and external switch/load/electrical qualification remain distinct.

Keep no-function assignment separate from level inversion (`0x0040`/`0x004B`),
a custom output's inactive value (`0x004F`), and the application's report that
a cable is absent. Function `0` establishes no assigned function; the manual
does not establish its electrical output level or an immediate application
deadline. Do not infer either, automatically save the change, or claim that a
successful write echo verifies the physical state. Preserve partial/uncertain
configuration results and read back the assignment where supported.

Serial-only operation is a supported configuration goal. Require external
inputs only for the selected method/mode that uses them. Disabled origin or
limit functions cannot satisfy switch-based homing prerequisites; ESS external
segment execution still needs its documented trigger/selection signals. Do not
add a global requirement to wire these signals before ordinary serial motion.
Enabling the drive through `0x002D` does not prove precedence over a configured
release input: the reviewed ESS pages do not resolve that arbitration. A
profile for another manufacturer must supply its own disable values, activation
rules and input/serial precedence; ESS assignments are not generic RS485 rules.

## Two-profile design check

This matrix tests the abstraction against different documented behavior; it
does not declare Leadshine implemented. ESS has the typed subset and named
gaps in [the complete coverage inventory](reference/ess_rs_operations.json). ESS evidence is the local
function manual cited above. Leadshine evidence is its official
[iEM-RS manual](https://www.leadshine.com/upfiles/downloads/3b01ef83987a2abfffcb4d4301a9cf4b_1689587677198.pdf),
revision table V2.0, sections 4.3.5, 5.3.2-3, 5.4.1 and 5.5.3-4
(physical PDF pages 25, 35-37, 42-43). The
[feasibility review](reference/03_multi_vendor_feasibility.md) records the
broader candidate scope.

| Concern | ESS-RS design evidence | Leadshine iEM-RS design evidence | Required common/profile behavior |
| --- | --- | --- | --- |
| Start sequencing | Configure parameters, then write the motion command; segmented modes use external triggers (13-24). | Stage PR data then trigger; an immediate-trigger path also exists. | Model staging, trigger and any triggering write as distinct effects; never append start after a failed prerequisite. |
| Units | Position uses configured pulse/subdivision interpretation; speed is rpm; ramp fields describe milliseconds, with conflicting defaults (17, 69-70). | PR speed uses rpm; ramp fields use ms/1000 rpm. | Convert physical acceleration using profile-specific semantics, validated parameters and reported quantization; unresolved scaling blocks conversion. |
| Jog timing | Serial speed start has no periodic-refresh requirement stated on 17-18; external JOG inputs have separate direction semantics. | Continuous serial jog requires trigger intervals below 50 ms. | Declare refresh requirements and worst-case bus admission before start; a missed deadline is not evidence of standstill. |
| Completion | Decode motion/arrival/homing observations separately from command acknowledgement (68). | Trigger readback distinguishes running, command completion and positioning completion. | Correlate fresh evidence to the operation; preserve accepted, executing, complete and uncertain outcomes separately. |
| Stop | Normal stop uses preconfigured deceleration; emergency stop is described as direct stopping without deceleration (25). | Quick stop uses its configured deceleration time. | Request explicit stop behavior; do not translate similar labels into assumed identical behavior. |
| Observation effects | Read flags with their documented polarity; do not infer side effects absent evidence. | Save-result status returns to its initial value after reading. | Declare destructive reads and one observation owner so background polling cannot consume another operation's evidence. |
| Optional I/O | Function `0` disables assignment; input polarity is separate. Serial enable/release precedence over active inputs is unresolved (26-28, 73-74). | Input function, polarity and filtering share a parameter; software forced enable has documented priority over I/O enable (20, 23-24). | Keep wiring, assignment and observation separate. Use each profile's actual encoding, activation and enable rules; require external I/O only when the selected operation needs it. |

The Leadshine jog timing prose and its shorter table wording differ; preserve
that discrepancy in its future ledger and qualify cadence with the target
firmware. No claim about a safe communication-loss response follows from
the refresh rule. The common layer must be able to reject an otherwise
representable command when the caller cannot meet its timing requirements.

## Sequencing, conflicts and cached knowledge

Family sequences are optional reusable library logic with application-owned
state. A native preparer fills the same bounded `OperationContext` used by
common operations. `advanceOperation` consumes one supplied event and
yields a bounded transaction, a wait requirement, or a result. It never
transmits, waits, reads a clock or retries on its own. Direct codec consumers
may manage the equivalent workflow themselves and retain responsibility for
the same prerequisites and uncertainty handling.

Contexts own the values needed across calls. They retain no caller frame,
response, temporary configuration or stack pointers. Only immutable profile
tables with static lifetime may be referenced. Transaction events carry the
original target, profile/configuration generation and transmission evidence;
completion is not routed according to whichever profile the CLI currently
selects. An event tagged with an older context cannot validate a newer
configuration or operation. Software correlation does not add a transaction
identifier to a protocol that lacks one: the bus owner must enforce framing,
late-response handling and recovery before reusing a response shape. If wire
evidence cannot distinguish a delayed reply, retain that uncertainty rather
than claiming that the local operation ID proves response freshness.

Native operations declare conflicts and effects so they cannot bypass axis
busy rules. Parameter staging, pending motion, refresh sequences and reads
that consume status must have one coherent owner. Reject incompatible
operations before yielding writes. Explicit stop follows the priority-stop
contract in [the axis API](axis_contract.md): invalidate unsent work, settle
the in-flight transaction and schedule stop without creating simultaneous
bus transactions. Retain the interrupted operation's result separately.

Successful write acknowledgement proves the checked protocol exchange, not
that the requested physical effect or persistence completed. Sequences
publish verified stages, partial application and uncertain effects
separately. Do not blindly replay motion triggers, saves, read-to-clear
transactions or communication changes after a lost reply. Recovery belongs
to the caller, using a profile's explicit evidence and replay constraints.

| Change | Knowledge requiring review or invalidation |
| --- | --- |
| Subdivision, direction, gearing or encoder interpretation | Conversion dependencies, native/engineering position relation, targets, limits and any affected reference assumptions |
| Word order or paired-data layout | Dependent decoders and cached paired quantities until active format is established |
| Address, baud or serial format | Active endpoint/transport assumptions and readiness, distinguished from staged settings awaiting restart |
| Homing, device-position clear/set or reference configuration | Coordinate generation, target/completion comparisons and dependent software-limit validity |
| I/O, modes, limits, tuning or segment parameters | Configuration snapshots, active workflow prerequisites, external-trigger assumptions and affected capability availability |
| Save, restore, reset, reboot or uncertain partial write | Relevant persistence/active-value knowledge; potentially all configuration/reference knowledge for broad changes |

Applications own caches and their timestamps; profiles report dependencies
and invalidation effects. Keep historical raw observations with provenance,
but mark them stale or uncertain when their interpretation no longer holds.
Do not apply a cached value under a new configuration generation. Distinguish
requested, staged, observed-active and observed-persistent values; update
certainty only when the documented application evidence supports it.

FieldCore can own the same contexts and events inside a future motor module;
the reusable profile does not become a bus owner or a scheduler. Current
FieldCore transaction and device-module limitations remain as recorded in
the [ecosystem review](reference/02_ecosystem_review.md). No existing motor
integration or deadline guarantee is claimed by this contract.

## Acceptance for a complete profile

A future release may claim complete documented API coverage only when:

1. Every applicable source entry and command alternative is accounted for,
   with independently reviewed model/firmware scope and no unexplained map
   gaps. Reserved and truly unavailable entries have evidence, not omission.
2. Every documented serial read, write and action has a typed callable
   mapping and equivalent standalone CLI access. Repeated entries have
   checked indexes. External-only execution is explicit, with complete
   serial configuration/observation coverage where available.
3. Width, access, units, byte/word order, ranges, prerequisites, persistence,
   response checking, completion and uncertainty are resolved for offered
   operations. Remaining ambiguities or unimplemented applicable features
   are reported as partial coverage, never hidden behind generic raw access.
4. Native tests verify frames and malformed responses, exact output update
   rules, range/unit boundaries, paired words, command effects and sequences
   through failures, timeouts, stale events and conflicting operations.
   Tests cite independent evidence rather than reproduce unreviewed tables.
5. Hardware qualification is reported separately with exact identity,
   firmware, configuration, operations and results. Protocol-test coverage
   alone does not establish physical motion, persistence, timing or stopping
   behavior.
6. Discovery/probe/identity coverage is accounted for, including an explicit
   unsupported result where no suitable non-changing operation exists. An
   advertised fast probe has its own validation, side-effect and timing
   evidence; a full identity/status sweep is not hidden behind `probe`.

The catalogue and bounded raw ESS codec are implemented. They do not establish
complete typed native command coverage. Resolving remaining vendor ambiguities,
implementing typed operations and qualifying hardware are subsequent blocks.
The [serial comparison](reference/08_serial_protocol_review.md) supplies original
manuals and protocol contrasts for future families without claiming support.
