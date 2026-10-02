# ESS23-RS engineering guidance

## Current scope

This is a documentation and folder seed. Do not implement the library or add
placeholder APIs/build metadata until implementation is requested. Preserve
the original downloaded references.

## Read before implementing

1. `README.md` and `docs/README.md`.
2. `docs/reference/00_document_inventory.md` and `sources.json`.
3. `docs/reference/01_implementation_reference.md`.
4. The original vendor function and hardware manuals in `docs/vendor/`.

The original PDFs are authoritative for documented motor behavior. Searchable
extracts may scramble tables, omit figures, or lose notation. Check the actual
PDF page before turning an ambiguous table into constants or code. The
function manual covers multiple product families: use the ESS-RS material,
not unrelated DM-PR registers. Record unresolved documentation/firmware
differences instead of guessing. No hardware behavior has been validated yet.

## Intended structure and architecture

- Public API headers: `include/ESS23_RS/`; implementation: `src/`.
- Follow the current stateless codec boundary in `../SHZK-PT`, `../VTN4xx`,
  and `../VibWire-108`. Do not copy their device registers or constants.
- Core operations are bounded frame builders, validators, decoders, and
  checked response parsers, using caller-supplied buffers and capacities.
- Keep reusable code independent of Arduino, ESP-IDF, GPIO, UART, clocks,
  FreeRTOS, logging, heap allocation, retries, and storage.
- Applications own transport, DE/RE, RTU timing, timeouts, shared-bus
  arbitration, commissioning, and motion workflows.
- Validate expected slave address, function, exact length, byte count, CRC,
  and applicable write echoes before publishing decoded results.
- Make motion, enable/disable, homing, reset, persistent writes, and
  communication-setting changes explicit operations.
- Establish supported register widths, signedness, scaling, word order,
  request limits, and exception behavior from the relevant vendor pages.
- Keep board pins and platform adapters under `examples/common/`.
- Add native protocol tests and build/package metadata when real code exists.
  Match sibling conventions for Doxygen API comments, ESP32-S2/S3 examples,
  and framework-neutral consumption. Do not claim unperformed hardware tests.

## Reference maintenance

`scripts/prepare_references.py` downloads references, validates file formats,
extracts PDF text, and records SHA-256 hashes. It refuses to replace an existing
file whose content differs. Review source changes before updating snapshots.
The software archive is a commissioning reference; do not execute it as part
of documentation preparation. Vendor files are not covered by any future
library license.
