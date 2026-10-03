# RS485Motion documentation

## Architecture baseline

The first implementation is pure unit conversion, an ESS register catalogue
and standalone build foundations. The contracts below describe the larger
intended library; wire commands, motion workflows, discovery and CLI remain
future work. See the [root README](../README.md) for current code and builds.

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

## Work tracking and bench

- [Software verification](verification.md): first implementation checks and
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
- [E2 board and FieldCore build audit](reference/04_co2control_platform.md):
  verified source pins, user-confirmed bench wiring and integration boundaries.

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
