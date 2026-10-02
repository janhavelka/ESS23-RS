# RS485Motion engineering guidance

## Current scope

Implementation is authorized in small, tested blocks. The first block is
configurable unit conversion, the complete documented ESS register catalogue,
and standalone build/board foundations. Protocol helpers, motion sequences,
discovery and the full CLI follow in later blocks; do not add placeholder APIs.
The accepted scope remains a general framework-independent serial motion
library: common axis API, drive profiles and application integration. Implement
ESS first; design against Leadshine iEM-RS without claiming it is implemented
or qualified. Preserve original downloaded references. The repository directory
remains `ESS23-RS`.

## Delivery workflow

The user requests a commit and sync after each prompt or completed logical
block. Validate the block, commit the task-owned changes, and push to the
configured upstream before declaring it finished. Fetch/check the remote
state, preserve unrelated work, and resolve ordinary synchronization issues
without rewriting published history. Report a real push/authentication
failure rather than claiming the work is synced. Do not make empty commits
for read-only replies or unfinished intermediate edits. The root agent owns
staging/committing shared work when subagents are used.

## Backlog and available bench

Maintain [the implementation backlog](docs/backlog.md) as features, open
questions and verification work are resolved. Mark implementation and
hardware evidence independently; a documented contract is not completed code.

The user has authorized future communication and motion testing on the motor
bench described in [hardware bench notes](docs/hardware_bench.md). As reported
on 2026-10-02, COM13 connects to a board running CO2control firmware with the
motor on its RS485 bus. The motor has a green indicator, was left at defaults,
is bolted to the table, and has a free shaft. This authorization persists for
that setup; routine tests within it do not need repeated permission. Inspect
the current port/firmware and actual settings when testing becomes relevant.
This is recorded availability, not a claim that communication has been tested.

## Read before implementing

1. `README.md` and `docs/README.md`.
2. `docs/architecture.md`, `docs/axis_contract.md`,
   `docs/profile_contract.md`, `docs/cli_contract.md`, and
   `docs/reference/02_ecosystem_review.md` for the inspected sibling contracts
   and intentional ESS choices.
   Read `docs/discovery_contract.md` for non-changing probes/discovery,
   `docs/backlog.md` for remaining work, and `docs/hardware_bench.md` before
   live tests.
3. `docs/reference/00_document_inventory.md` and `sources.json`.
4. `docs/reference/01_implementation_reference.md`,
   `docs/reference/05_ess_register_catalog.md`, and
   `docs/reference/06_encoder_units.md`. Read
   `docs/reference/04_co2control_platform.md` before board/example work.
5. The original vendor function and hardware manuals in `docs/vendor/`.

The original PDFs are authoritative for documented motor behavior. Searchable
extracts may scramble tables, omit figures, or lose notation. Check the actual
PDF page before turning an ambiguous table into constants or code. The
function manual covers multiple product families: use the ESS-RS material,
not unrelated DM-PR registers. Record unresolved documentation/firmware
differences instead of guessing. No hardware behavior has been validated yet.

## Intended structure and architecture

- Common public headers: `include/RS485Motion/`, namespace
  `RS485Motion`; ESS headers: `include/RS485Motion/profiles/ess_rs/`, namespace
  `RS485Motion::ESS_RS`; implementation: `src/`.
- Follow the current stateless codec boundary in `../SHZK-PT`, `../VTN4xx`,
  and `../VibWire-108`. Do not copy their device registers or constants.
- Profile codecs are bounded frame builders, validators, decoders and checked
  response parsers using caller-supplied buffers and capacities. The common
  axis layer supplies typed motion intent, explicit units and checked
  conversion; optional reusable sequencing advances bounded caller-owned
  state from supplied events/time and yields work without performing I/O.
- Keep reusable code independent of Arduino, ESP-IDF, GPIO, UART, clocks,
  FreeRTOS, logging, heap allocation, retries, and storage.
- Applications own transport, DE/RE, framing/timing, timeouts, scheduling,
  shared-bus arbitration, retries, commissioning and machine workflows.
  They execute yielded profile sequences and retain contexts, caches and
  health policy. Arduino, native ESP-IDF and FieldCore are independent
  consumers; no framework or FieldCore types enter the reusable core.
- Steps/counts, turns, degrees, radians and configured linear travel are
  public API concepts, not CLI-only conversions. Distinguish motor steps,
  command subdivisions, encoder counts, motor shaft and load coordinates.
  Require explicit scale/origin, checked range/rounding and angle path policy
  when preparing motion. The current pure conversion API handles signed
  displacement, velocity and acceleration; it does not establish an origin.
  Unresolved conversion or unsupported capability must fail before writes.
- Expose the complete documented native command set for each supported
  family/model/protocol/firmware through typed profile extensions. A generic
  register escape hatch is not full command coverage. Track unresolved fields,
  implementation coverage and hardware qualification separately; never invent
  commands for capabilities available only through external inputs.
- Use the long generic builder/parser names from the architecture. All ESS
  frame parsers require request expectations; output capacity is not the
  expected register count. Payload outputs stay unchanged on error, with an
  explicit output count reset to zero.
- Keep codec `Status`, decoded motor alarms/flags, command outcome, and
  application health/freshness separate. The codec has no lifecycle or health
  service and retains no caller buffers.
- Validate expected slave address, function, exact length, byte count, CRC,
  and applicable write echoes before publishing decoded results.
- Make motion, enable/disable, homing, reset, persistent writes, and
  communication-setting changes explicit operations.
- A write acknowledgement does not prove motion completion. A timeout after
  transmission may leave execution unknown; do not inherit sensor retry or
  recovery replay policies. Local cancellation is not a motor stop.
- Stop can interrupt an active operation with explicit deceleration and queue
  semantics while the bus owner settles any in-flight transaction. Profiles
  expose required service deadlines; missed refreshes do not prove a stop.
- Example `status`/`health` are cached; explicit read/check commands refresh
  them. `reset` clears local statistics only, `recover` is host transport
  recovery, and motor operations use explicit names on every platform.
  Common and native CLI commands call the same public API available to upper
  firmware. Equivalent Arduino/ESP-IDF builds share command semantics and
  profile coverage; platform adapters own I/O.
- Each drive profile must account for discovery and an optional minimal,
  non-changing presence probe. `probe`/`ping` use `prepareProbe`; full identity
  reads remain separate. Manufacturer filtering selects reviewed profiles,
  not a universal manufacturer command. Unsupported probes return clearly
  before TX; discovery never guesses writes, clears read-to-clear state, or
  treats an ambiguous responder as a confirmed model. Applications own bounded
  scans and bus scheduling; see the discovery contract.
- Future FieldCore integration belongs in its device module and existing bus
  owner. Its current FC06 echo stripping, eight-byte TX capacity and
  measurement-only contracts need review before claiming motor compatibility.
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
library license. The ESS JSON catalogue is the register transcription source;
regenerate its C++ tables with `scripts/generate_ess_registers.py` and check
them with `--check`. Keep firmware uncertainties visible in that source.
