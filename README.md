# RS485Motion architecture seed

Framework-independent serial motion library design, with STEPPERONLINE
ESS23-RS as the first implementation target and Leadshine iEM-RS as the
contrasting design reference. The repository directory remains `ESS23-RS`;
`RS485Motion` is the working common library and namespace name.

The accepted design has a common axis API, explicit drive profiles with full
documented native commands, and application-owned transport. Upper firmware
uses the same motion vocabulary across supported profiles. Arduino, native
ESP-IDF, other firmware and future FieldCore integration are independent
consumers of the same core.

This folder contains architecture/API/CLI contracts and development references.
No motor library, public API, examples, tests, or build configuration has been
implemented yet.

## Start here

- [Documentation and downloaded files](docs/README.md)
- [Library architecture and ownership](docs/architecture.md)
- [Common axis API and units](docs/axis_contract.md)
- [Drive profiles and full command coverage](docs/profile_contract.md)
- [Standalone example CLI contract](docs/cli_contract.md)
- [Manufacturer research and design evidence](docs/reference/03_multi_vendor_feasibility.md)
- [RS485 ecosystem review and FieldCore integration gaps](docs/reference/02_ecosystem_review.md)
- [Source inventory](docs/reference/00_document_inventory.md)
- [Implementation reference and open questions](docs/reference/01_implementation_reference.md)
- [AI coder instructions](AGENTS.md)

The first implementation target is **ESS23-RS20**. The **ESS23-RS10** references are
included so the eventual library can account for both variants. These are the
RS485 models; the pulse-controlled ESS23-10/ESS23-20 are separate products.

## Current contents and planned layout

Only documentation and folder seeds exist. Public code will use the neutral
layout in the architecture; the existing empty `include/ESS23_RS/` directory
comes from the earlier ESS-only seed and contains no API.

```text
include/               Future RS485Motion common and per-profile headers
src/                   Future common units, sequence and profile codecs
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

Follow the existing `../SHZK-PT`, `../VTN4xx`, and `../VibWire-108` stateless
codec conventions. Add explicit unit conversion and bounded caller-owned
sequence logic above that boundary. UART, DE/RE, timing, scheduling, retries,
cached readings and health policy stay in the application or example layer.

The public axis design covers absolute/relative positioning in explicit
steps/counts, turns, degrees, radians and configured linear travel, plus
velocity, stop, enable/release, homing, fault clear and observations. Profiles
declare supported operations; conversions require established scales and
reference state. Native extensions expose family-specific tuning, I/O,
configuration and other documented commands without forcing them into the
common subset. No listed operation is implemented or hardware-qualified yet.

The standalone console maps to that same public API and owns its transport.
Native ESP-IDF consumption requires neither Arduino nor FieldCore. Future
FieldCore integration needs changes to its write framing, request capacity
and control contracts; those limits do not constrain this library's API.

## Original product pages

- [ESS23-RS20](https://www.omc-stepperonline.com/ess-series-2-2nm-311-55oz-in-nema-23-integrated-rs485-closed-loop-stepper-servo-motor-24-48vdc-1000ppr-ess23-rs20)
- [ESS23-RS10](https://www.omc-stepperonline.com/ess-series-1-2nm-169-93oz-in-nema-23-integrated-rs485-closed-loop-stepper-servo-motor-24-48vdc-1000ppr-ess23-rs10)

Downloaded vendor files and standards retain their original owners' rights.
The tuning archive contains a Windows executable, not library source code.
