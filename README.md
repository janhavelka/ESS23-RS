# ESS23-RS

Library seed for STEPPERONLINE ESS23-RS integrated closed-loop stepper motors
with RS485/Modbus control. Prepared on 2026-10-02.

This folder currently contains the project structure, architecture baseline,
and development references.
No motor library, public API, examples, tests, or build configuration has been
implemented yet.

## Start here

- [Documentation and downloaded files](docs/README.md)
- [Library architecture and ownership](docs/architecture.md)
- [Standalone example CLI contract](docs/cli_contract.md)
- [RS485 ecosystem review and FieldCore integration gaps](docs/reference/02_ecosystem_review.md)
- [Source inventory](docs/reference/00_document_inventory.md)
- [Implementation reference and open questions](docs/reference/01_implementation_reference.md)
- [AI coder instructions](AGENTS.md)

The initial hardware target is **ESS23-RS20**. The **ESS23-RS10** references are
included so the eventual library can account for both variants. These are the
RS485 models; the pulse-controlled ESS23-10/ESS23-20 are separate products.

## Layout

```text
include/ESS23_RS/       Future public headers
src/                   Future framework-neutral codec implementation
examples/common/       Future example-only transport and board helpers
test/                  Future native codec tests
scripts/               Reference download and PDF extraction helper
docs/reference/        Source inventory and concise implementation notes
docs/vendor/           Original manufacturer PDFs
docs/vendor/cad/       RS10 and RS20 STEP models
docs/vendor/software/  Vendor tuning-software archive
docs/standards/        Official Modbus specifications
docs/pdf-extracted-md/ Searchable extracts indexed by physical PDF page
```

Follow the existing `../SHZK-PT`, `../VTN4xx`, and `../VibWire-108` library
conventions: a bounded, transport-independent codec core, with UART, DE/RE,
timing, retries, and bus ownership in the application or example layer.

The architecture defines function naming, buffer/output contracts, protocol
status versus motor state and application health, and explicit motor command
workflows. The standalone console will own its transport and work without
FieldCore. Future FieldCore integration needs changes to its current write
framing, request capacity and control contracts; it is not implemented here.

## Original product pages

- [ESS23-RS20](https://www.omc-stepperonline.com/ess-series-2-2nm-311-55oz-in-nema-23-integrated-rs485-closed-loop-stepper-servo-motor-24-48vdc-1000ppr-ess23-rs20)
- [ESS23-RS10](https://www.omc-stepperonline.com/ess-series-1-2nm-169-93oz-in-nema-23-integrated-rs485-closed-loop-stepper-servo-motor-24-48vdc-1000ppr-ess23-rs10)

Downloaded vendor files and standards retain their original owners' rights.
The tuning archive contains a Windows executable, not library source code.
