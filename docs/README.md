# MotorControl-RS documentation

[Pulse-rate boundary and endurance evidence](reports/2026-10-07_rate_boundary.md)
records the measured speed/subdivision/ramp matrix, reproducible reset-like
failures below and above 200 kHz, and the tested practical envelope.

[High-subdivision motion research](reference/12_ess_rs_motion_web_review.md)
compares the exact ESS-RS manuals, current vendor FAQs and community reports
against firmware `0x0029` bench evidence, including the unresolved 200 kHz entry.

[Move by / move to in C++](move_example.md) shows the existing ESP32 application's
unit-aware functions, background polling and retained progress/results.

The [partial0.6.0 candidate](release_candidate.md),
[getting started/troubleshooting](getting_started.md) and
[FieldCore handoff](fieldcore_handoff.md) describe the independently usable
package and separately scoped integration work. [30's candidate record](reports/ess_release_30_2026-10-05.md)
pins its exact source, shipped core ZIP, verification evidence and CI.

The [integrated audit](reports/ess_release_28_2026-10-05.md) records current
code and coverage corrections. The [qualification matrix](reports/ess_release_29_2026-10-05.md)
separates available Arduino/native-IDF S3 functional evidence from nine named
native-family gaps, deferred endurance, missing physical fault fixtures and
unmeasured electrical/shaft behavior. These are partial-candidate evidence,
not complete ESS support or release publication.

The [human-readable console guide](console.md) explains grouped help, operation
results and diagnostics. Ordinary commands are readable; `@ID` commands retain
the existing JSONL automation protocol.

[Repeatable verification and core packaging](verification.md) provides one
quick/full entry point shared by hosted CI, strict C++11/C++17 consumers and
offline reference/document checks. Hardware qualification is separate.

[Native ESP-IDF standalone build and recovery](esp_idf_probe.md) reuse the same
owner, commands and operations as Arduino. [Prompt25 verification](reports/ess_release_25_2026-10-05.md)
records clean firmware/core builds, startup/backpressure failure tests and
read-only COM13 evidence. [Prompt26 platform qualification](reports/ess_release_26_2026-10-05.md)
adds matched capture/load, finite movement/stop and reversible settings evidence;
S2 is compile-only, and electrical/independent shaft measurements remain open.

Prompt24 delivers [repeatable finite Python scenarios](bench_scenarios.md),
bounded incremental evidence and same-session cleanup. [Verification](reports/ess_release_24_2026-10-05.md)
separates read-only/finite functional PASS from remaining fixtures and native gaps.

Prompt23 delivers the [actual API/CLI coverage handoff](ess_api_cli_coverage.md) and [verified integration](reports/ess_release_23_2026-10-05.md). Typed commands, local target selection and retained results share one owner. Named native-family gaps remain explicit; this is not a full-release completion claim.

Prompt22 adds [bounded ESS discovery](ess_discovery.md), minimal public probes and retained
scan evidence with explicit recovery/restoration. [Verification](reports/ess_release_22_2026-10-05.md) records
COM13 address/tuple scans, budget limits and unchanged motor settings/state.

Prompt21 adds [typed save/factory restore](ess_persistence.md), exclusive
commissioning ownership, before-values and separate ACK/live-readback/restart
evidence. [Verification](reports/ess_release_21_2026-10-04.md) records software
and read-only COM13 checks; physical persistence remains NOT RUN.

[Debugging normal operations](traffic.md) groups passive traffic, checked
translation, software timing and retained outcomes around the production path.
Use `debug off|raw|decoded`; the former console `sniff` spelling is replaced.

The [ordinary motion procedure](functional_bench.md) uses regular APIs and
firmware. [Passive raw/decoded sniffing](traffic.md) copies traffic without
consuming protocol bytes. Earlier [recorded results](reports/functional_motion_2026-10-04.md)
include drive-reported forward/return motion, moving stops and exact restoration;
independent shaft/electrical measurements and the omitted soak remain separate.

Prompt20 adds [explicit communication commissioning](ess_communication.md):
typed settings, exclusive owner admission, retained candidates and bounded
read-only confirmation. [Native/build/read-only verification](reports/ess_release_20_2026-10-04.md)
passes; physical activation/restoration is NOT RUN pending a motor restart and
qualified route back. Prompt21 owns explicit save-dependent activation.

Prompt19 delivers [host tuple support](host_serial.md): one adapter, exclusive
configuration ownership, retained historical context and explicit restoration.
All sixteen SDK setups and two mismatch/restore COM13 scenarios pass without
device changes. The [fresh audit](reports/ess_release_19_audit_2026-10-04.md) fixes
retained settings reconciliation, timer error settlement and Python diagnostics;
malformed mismatch traffic remains unresolved. The [implementation handoff](reports/ess_release_19_2026-10-04.md) keeps
alternate-tuple communication and electrical qualification separate.

The [fresh prompts 17/18 audit](reports/ess_release_17_18_audit_2026-10-04.md) fixes
settings freshness, copied provenance, partial-refresh invalidation and strict
console evidence validation. All 45 native suites, installed consumption and
four firmware builds pass. The final COM13 image passes 104 frames and restores
input filter `2?3?2` and lock delay `200?201?200`; physical effects remain unqualified.

Prompt18 adds [typed tuning settings](ess_tuning.md) and shared API/CLI/Python
routes. All twenty native reads and input-filter stored restoration pass on
COM13; gain, arrival and collision effects remain unqualified in
[the handoff](reports/ess_release_18_2026-10-04.md).

Prompt17 implements [control settings](ess_control_settings.md) through the existing bounded sequence. Native/API/CLI/Python checks and all-field COM13 reads pass; delay200-to201-to200 stored restoration passes. Mode/encoder/current effects remain unqualified in [the report](reports/ess_release_17_2026-10-04.md).


Prompt16 implements [indexed stored records](ess_segments.md), retaining pair-write and signed-encoding blockers. Boundary reads and PT/PV scalar restoration pass; shared starting-speed readback mismatch remains explicitly unresolved in [the report](reports/ess_release_16_2026-10-04.md). External triggering has no wired fixture.


Prompt15 implements [typed optional I/O](ess_io.md) through the existing settings sequencer. Explicit disabling/readback and exact restoration have separate register-path evidence; external switch/load functionality and active electrical state remain unqualified.

Prompt14 implements [bounded homing](ess_homing.md) and the [complete method/prerequisite table](ess_homing_methods.md). Methods33/34/35 have software paths; external-switch trajectories, collision conflicts, nonzero offsets and physical reference/return qualification remain open. See the [handoff](reports/ess_release_14_2026-10-04.md).

## Architecture baseline

Prompt13 implements [typed driver settings](ess_driver_settings.md), shared
generation/effect invalidation, strict console/Python evidence and raw limit
reads. [Native/build and read-only bench evidence](reports/ess_release_13_2026-10-04.md)
pass; paired setters and physical settings/soft-limit qualification remain open.
The [fresh audit](reports/ess_release_13_audit_2026-10-04.md) corrects response
confirmation, shared axis-cache ordering and input qualification invalidation.


Prompt 12 records the [complete paired-write disposition](ess_pair_writes.md).
The four FC10 windows now share the generated register-ledger policy; no new
window or physical pair-write qualification is inferred. See the
[implementation evidence](reports/ess_release_12_2026-10-04.md) and
[fresh audit](reports/ess_release_12_audit_2026-10-04.md).

Release prompts 01–04 deliver the application owner, responsive console and
[capture review](reports/ess_release_04_2026-10-04.md): native/build checks and
7/37-byte read-only bench evidence pass. Capture costs about 20–21% of one
core; independent electrical timing remains NOT RUN.

Prompt 05 adds installed typed identity/configuration reads, matching console
routes and a ledger-linked operation inventory, with native/package and read-only
COM13 evidence. See the [read API](ess_reads.md) and
[fresh audit](reports/ess_release_05_audit_2026-10-04.md).

Prompt 06 adds typed state blocks, per-block conservative ages, separate health
and finite polling. The [fresh audit](reports/ess_release_06_audit_2026-10-04.md)
checks current interpretation confidence and strict host validation.

Prompt 07 implements [exact host coordinate preparation](axis_preparation.md),
configuration/reference validation and pure console previews.

Prompt 08 adds [bounded actions and priority stop](ess_actions.md), application
axis reservations, explicit uncertainty and console routes. Later
[finite functional tests](reports/ess_release_29_2026-10-05.md) exercise the
ordinary API on the secured free shaft; independent TX/RX/DE and echo-source
measurements remain unperformed and do not replace checked transport evidence.

Prompt09 adds [finite relative positioning](ess_position.md): exact common/native
preparation, checked staging/trigger, new activity/completion evidence and priority
stop interruption. Later bounded positive moves and both moving stops have
drive-reported evidence on both S3 frameworks; calibrated position, independent
shaft behavior and other motion subsets remain unqualified.

Prompt10 adds shared absolute/wrapped-angle preparation and execution, zero-only
device clear, host-origin/reference invalidation and all spatial-unit CLI routes.
The [handoff](reports/ess_release_10_2026-10-04.md) records native/package/build and
read-only COM13 evidence separately from unperformed physical comparisons.
The [fresh audit](reports/ess_release_10_audit_2026-10-04.md) corrects reference
retention and external-motion invalidation and repeats required verification.

Prompt11 adds [finite serial velocity](ess_velocity.md), shared exact rate conversion,
configured native ramp snapshots and existing priority stop. The [handoff](reports/ess_release_11_2026-10-04.md)
records native/package/build and read-only bench/gate evidence; physical velocity,
acceleration and stop remain unqualified.
The [fresh prompt 11 audit](reports/ess_release_11_audit_2026-10-04.md) verifies
exact conversions independently and fixes Python interruption/evidence handling;
actual C++ operation terminals now participate in a registered parity check.

Implemented blocks include exact target preparation, typed reads and settings,
pure unit conversion, the ESS register catalogue, checked codecs, bounded
discovery and motion sequences. The standalone owner and console share those
APIs on Arduino and native ESP-IDF. Native tests and selected S3 bench campaigns
cover their actual paths. Remaining native operations and physical cases stay
in the [coverage inventory](ess_api_cli_coverage.md) and qualification matrix;
S2 core/portable consumption is compile-only. See the [root README](../README.md)
for current code and builds.

- [Standalone RTU runner](runner.md): current callback/timing contract, state
  machine, fake tests, diagnostics, memory sizes and explicit recovery.
- [Bounded RTU bus owner](bus_owner.md): fair scheduling, reserved urgent/results
  storage, cancellation, recovery, parser settlement and absolute closure deadlines.
- [ESP32-S3 probe guide](esp32_probe.md): adapter, read-only console, PSRAM and Python tools.
- [0.5.1 implementation audit](reports/2026-10-03_audit.md): defects, fixes,
  regression tests and remaining qualification gaps.
- [Initial bench report](reports/2026-10-03_e2_probe.md): measured probes, timing
  exception, raw model, firmware backup and remaining qualification work.
- [Current architecture report](architecture_report.md): source-based review
  of what exists, file and dependency map, transaction flow, standalone and
  FieldCore ownership, debugging and remaining integration work.
- [Architecture and ownership](architecture.md): intended core layout,
  function names, buffer/status contracts, motor workflows and implementation
  stages, with current implementation distinguished from planned behavior.
- [Axis API and units](axis_contract.md): shared motion vocabulary, steps,
  angles, linear travel, checked conversion and caller-owned operation state.
- [Profile and command coverage](profile_contract.md): full documented native
  command access per model/protocol/firmware, evidence and coverage requirements,
  and the contrasting Leadshine design review.
- [Standalone CLI contract](cli_contract.md): shared ecosystem vocabulary,
  host/device distinctions, diagnostics, health and explicit motor operations.
- [Discovery and minimal probes](discovery_contract.md): per-profile and
  manufacturer discovery, bounded scans and fast non-changing presence checks.
- [Ecosystem review](reference/02_ecosystem_review.md): source-based comparison
  of SHZK-PT, VTN4xx, VibWire-108 and FieldCore-node, including integration gaps.
- [Multi-vendor feasibility](reference/03_multi_vendor_feasibility.md): official
  manufacturer evidence behind the accepted general-library scope. Research
  coverage does not imply implemented device support.
- [CANopen feasibility and library boundary](reference/07_canopen_feasibility.md):
  accepted separate-library decision, common motion contract, CL86-C evidence
  and protocol-specific work. This analysis does not add CANopen support.
- [Serial protocol comparison](reference/08_serial_protocol_review.md): five
  downloaded manufacturer manuals, contrasting framing/limits/read effects,
  probe candidates and consequences for the ESS codec boundary.
- [ESS timing and gap audit](reference/09_timing_and_gap_audit.md): framing,
  response deadlines, stop/configuration timing, register gaps and remaining
  firmware qualification work.
- [Runner platform review](reference/10_runner_platform_review.md): inspected
  historical FieldCore/ESP32 mechanisms, PSRAM placement and the original Python
  automation plan; current scenarios are in [bench automation](bench_scenarios.md).

## Work tracking and bench

- [Platform scope and optional I/O audit](reports/2026-10-03_platform_scope_audit.md):
  product decoupling, explicit adapter wiring, disabled I/O semantics and regression evidence.
- [Repository/folder rename](repository_rename.md): prepared metadata,
  remote update and fresh build/IDE paths while preserving bench evidence.
- [Release roadmap](roadmap.md): delivery order, release scope, completion gates
  and the hardware regression required as each implemented feature reaches the bench.
- [ESS release prompt set](prompts/ess_release/README.md): 30 numbered blocks
  from bus ownership through motion, native ESS coverage and release evidence;
  shared audit/testing rules and a requirement-to-prompt coverage map.
- [Prompt preparation and source audit](reports/2026-10-03_promptset_audit.md):
  actual FieldCore conventions, scope differences and corrected dependencies.
- [Prompt/source re-audit](reports/2026-10-03_promptset_reaudit.md): deadline,
  recovery, operation ownership, cache age and mixed-device integration gaps.
- [Capture and load audit](reports/2026-10-03_capture_load.md): independent wire
  fixtures, timer capture, measured load and remaining timing qualification.

- [Software verification](verification.md): current quick/full entry point,
  clean ZIP/install consumers, public-header isolation and hosted CI, followed
  by explicitly historical verification records.

- [Features, implementation tasks and open questions](backlog.md): current
  roadmap, with design milestones separated from implementation and testing.
- [COM13 motor bench](hardware_bench.md): secured free-shaft setup, continuing
  test authorization, inspected settings and firmware/evidence ownership.

## Manufacturer reference pack

- [ESS register catalogue](reference/05_ess_register_catalog.md): complete
  appendix transcription, generated C++ descriptors, choices and unresolved
  source differences. This is metadata, not operational command coverage.
- [Encoder and unit conversion](reference/06_encoder_units.md): exact source
  facts, explicit bench assumptions and use of the current API.
- [ESP32-S3 bench configuration](reference/04_esp32_bench.md):
  explicit pins, SDK/build settings and separation from the reusable library.

Start with the [inventory](reference/00_document_inventory.md) and the
[implementation reference](reference/01_implementation_reference.md).
The [machine-readable source manifest](reference/sources.json) records download
URLs, the successful mirror URLs, retrieval times, sizes, page counts, and
SHA-256 checksums.

## Essential manufacturer documents

- [Modbus function manual V1.0](vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf): commands, operating modes, parameters, and register appendices.
- [ESS23-RS10/RS20 hardware manual V1.0](vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf): power, terminals, wiring, DIP switches, and alarms.
- [RS20 full datasheet](vendor/ESS23-RS20_Full_Datasheet.pdf).
- [RS10 full datasheet](vendor/ESS23-RS10_Full_Datasheet.pdf).

## Supporting files

- [Modbus application protocol V1.1b3](standards/Modbus_Application_Protocol_V1.1b3.pdf).
- [Modbus serial-line specification V1.02](standards/Modbus_Serial_Line_V1.02.pdf).
- Mechanical models: [RS20 STEP](vendor/cad/ESS23-RS20.STEP), [RS10 STEP](vendor/cad/ESS23-RS10.STEP).
- [Official tuning-software ZIP V1.2.7](vendor/software/Y_Series_Stepper_Driver_Debug_Software_V1.2.7.zip), linked from both motor product pages despite its Y-Series filename. See [software notes](vendor/software/README.md).

## Searchable versions

[PDF extracts](pdf-extracted-md/README.md) retain physical page boundaries for
quick searching. Original PDFs remain the reference for tables and diagrams.
These documents are reference material, not hardware qualification or a
completed driver design.
