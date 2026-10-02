# ESS23-RS documentation

## Architecture baseline

- [Architecture and ownership](architecture.md): intended core layout,
  function names, buffer/status contracts, motor workflows and implementation
  stages. This is a design, not an implemented library.
- [Standalone CLI contract](cli_contract.md): shared ecosystem vocabulary,
  host/device distinctions, diagnostics, health and explicit motor operations.
- [Ecosystem review](reference/02_ecosystem_review.md): source-based comparison
  of SHZK-PT, VTN4xx, VibWire-108 and FieldCore-node, including integration gaps.
- [Multi-vendor feasibility](reference/03_multi_vendor_feasibility.md): official
  manufacturer evidence and a proposed common interface for selected serial
  motion drives. This is a scope recommendation, not implemented support.

## Manufacturer reference pack

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
