# MotorControl-RS documentation

## Architecture baseline

Release prompts 01–04 deliver the application owner, responsive console and
[capture review](reports/ess_release_04_2026-10-04.md): native/build checks and
7/37-byte read-only bench evidence pass. Capture costs about 20–21% of one
core; independent electrical timing remains NOT RUN.

Implemented blocks are pure unit conversion, an ESS register catalogue,
checked wire codecs with a minimal probe and standalone build foundations.
The standalone RTU runner and ESP32-S3 adapter have native fake tests. The read-only
probe CLI has bench evidence; external timing qualification, typed commands,
motion workflows, discovery orchestration and full CLI coverage remain future
work. See the [root README](../README.md)
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
  FieldCore/ESP32 mechanisms, PSRAM placement and future Python bench automation.

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

- [Software verification](verification.md): foundation and codec checks with
  explicit limits on what was tested.

- [Features, implementation tasks and open questions](backlog.md): current
  roadmap, with design milestones separated from implementation and testing.
- [COM13 motor bench](hardware_bench.md): user-reported CO2control/RS485 setup,
  free-shaft mounting, continuing test authorization and unverified settings.

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
