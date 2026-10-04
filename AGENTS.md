# MotorControl-RS engineering guidance

## Current scope

Implementation is authorized in small, tested blocks. Implemented foundations
are configurable units, the complete documented ESS register catalogue,
standalone build/board settings and checked ESS wire codecs with a minimal
probe. The standalone runner under `examples/common/` now has native fake
tests. The ESP32-S3 adapter supports polling and GPTimer capture; the
read-only probe/load CLI has native tests and measured bench evidence. External timing qualification, motion sequences,
discovery orchestration and the full CLI follow; do not add placeholder APIs.
The accepted scope remains a general framework-independent serial motion
library: common axis API, drive profiles and application integration. Implement
ESS first; design against Leadshine iEM-RS without claiming it is implemented
or qualified. Preserve original downloaded references. The checkout directory
name is not part of the library API; `MotorControl-RS` is the recommended name.

CANopen belongs in a separate future motion library, initially targeting the
verified CL86-C subset. Keep one documented motion vocabulary and behavioral
contract across the libraries, with independent protocol implementations and
application bus owners. Extract shared units/types only after the second
implementation demonstrates concrete reuse; do not add a universal transport
engine or a speculative common framework here. The accepted package name is
`MotorControl-RS`; C++ namespace, include directory and CMake identity are
`MotorControlRS`. Repository/board metadata is prepared for the user's GitHub
rename to `janhavelka/MotorControl-RS`. Keep the working Git remote until that
endpoint exists, then update it. See [rename steps](docs/repository_rename.md)
for generated-cache handling; never delete bench evidence with build caches.

## Delivery workflow

Session boundary, confirmed by the user on 2026-10-03: treat the FieldCore-node
repository as read-only reference. Do not edit it during this session. Develop
and test the transport reference in this repository; eventual FieldCore changes
belong to a separately authorized integration step.

The user requests a commit and sync after each prompt or completed logical
block. Validate the block, commit the task-owned changes, and push to the
configured upstream before declaring it finished. Fetch/check the remote
state, preserve unrelated work, and resolve ordinary synchronization issues
without rewriting published history. Report a real push/authentication
failure rather than claiming the work is synced. Do not make empty commits
for read-only replies or unfinished intermediate edits. The root agent owns
staging/committing shared work when subagents are used.

For every implementation or audit prompt, inspect the actual source and affected
callers/tests in this repository and the relevant current FieldCore source
read-only. Check conventions and integration boundaries against code, not only
historical reports. Record deliberate differences; matching FieldCore does not
mean importing its framework types or replacing its other device protocols.

## Backlog and available bench

Maintain [the implementation backlog](docs/backlog.md) as features, open
questions and verification work are resolved. Mark implementation and
hardware evidence independently; a documented contract is not completed code.
Keep [the release roadmap](docs/roadmap.md) current as implementation and
qualification sharpen its milestones. Run a short hardware regression on the
authorized bench when new behavior reaches the board, plus focused feature
checks. Record unperformed hardware checks explicitly; quick regressions do
not replace the later soak and electrical qualification gates.

When hardware tests expose a bug or unexpected behavior, rerun the failing
scenario deliberately and add the diagnostics needed to establish its root
cause. Retain the firmware/settings, raw traffic, timing, task/service gaps,
errors and resource measurements relevant to distinguishing competing causes.
Use bounded diagnostics and account for their effect on timing. Do not treat
a later passing run, guessed cause, relaxed timeout or hidden retry as a fix.
Once the cause is established, apply the simplest proper fix or refactor the
affected code rather than adding a workaround that only hides the symptom.
Rerun the original failing hardware scenario and a short regression of existing
functionality; add a native regression where the failure can be represented.
Record the cause, evidence, fix and verification in the relevant report/backlog.
If evidence is insufficient, keep the issue explicitly unresolved and state
which measurement is missing. Deliberate test reruns must still respect the
existing prohibition on automatically replaying an uncertain motor write.

The user has authorized future communication and motion testing on the motor
bench described in [hardware bench notes](docs/hardware_bench.md). As reported
on 2026-10-02, COM13 connects to a board running CO2control firmware with the
motor on its RS485 bus. The motor has a green indicator, was left at defaults,
is bolted to the table, and has a free shaft. This authorization persists for
that setup; routine tests within it do not need repeated permission. Inspect
the current port/firmware and actual settings when testing becomes relevant.
This is recorded availability, not a claim that communication has been tested.

Reconfirmed on 2026-10-04: only power and RS485 are connected to the ESS23-RS20.
The motor is bolted securely to the table and its shaft is free and uncoupled;
there are no external switches, output loads or driven mechanics. Standing
authorization includes bounded physical free-shaft tests without another
permission question. Establish the tested command/stop prerequisites from actual
evidence; optional unwired I/O is not a blanket blocker for serial-only tests.
Unwired terminals do not establish disabled drive function assignments.

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
   `docs/reference/09_timing_and_gap_audit.md` for framing and unresolved
   response, configuration and motion timing before transport/workflow work.
   Read `docs/runner.md`, `docs/esp32_probe.md`, the current bench report and
   `docs/reference/10_runner_platform_review.md`
   before runner, ESP32 adapter or automated bench work.
   Read
   `docs/reference/04_esp32_bench.md` before board/example work.
   Read `docs/reference/08_serial_protocol_review.md` before extending codecs
   to another manufacturer; contrasting manuals are preserved under
   `docs/vendor/contrasts/` with their own source/hash manifest.
5. The original vendor function and hardware manuals in `docs/vendor/`.

The original PDFs are authoritative for documented motor behavior. Searchable
extracts may scramble tables, omit figures, or lose notation. Check the actual
PDF page before turning an ambiguous table into constants or code. The
function manual covers multiple product families: use the ESS-RS material,
not unrelated DM-PR registers. Record unresolved documentation/firmware
differences instead of guessing. Only the recorded read-only bench probes have
hardware evidence; do not generalize that to motion or full timing qualification.

## Intended structure and architecture

- This library targets RS485-controlled motors through independent drive
  profiles. FieldCore is one future consumer; its motor device/product does
  not exist yet. Only its RS485 owner/module/backend workflow is a reference.
  Do not import E2 sensor-bus, CO2control product, cloud, settings service or
  product composition requirements. Historical bench labels are not APIs.
- Keep MCU-specific adapters and load fixtures outside the reusable core.
  The ESP32-S3 adapter takes explicit TX/RX/DE pins and direction polarity;
  the standalone application supplies its bench wiring. Neither an MCU nor
  PSRAM is a prerequisite for consuming the core or portable runner.
- External drive I/O may be unused. Distinguish unconnected wiring from a
  disabled function assignment; use the profile's documented no-function
  value rather than polarity tricks. ESS input/output function 0 means no
  function. Require physical inputs only for operations that need them, and
  never silently disable inputs, limits or stops during serial motion setup.
- Common public headers: `include/MotorControlRS/`, namespace
  `MotorControlRS`; ESS headers: `include/MotorControlRS/profiles/ess_rs/`, namespace
  `MotorControlRS::ESS_RS`; implementation: `src/`.
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
- Keep the agreed generic builder/parser vocabulary; use short, direct helper
  names such as `readWord`, `checkReply` and `writeHeader`. All ESS
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
- ESS FC03 permits at most 16 reviewed readable words; FC06 excludes paired
  halves. FC10 admits the four documented start/count windows: 0x0024/2,
  0x0021/5, 0x001D/3 and 0x0031/6, not a claimed device-wide maximum.
  Probe reads model register 0x0000/one word. Generate compact codec access
  policy from the same register ledger; basic codec use must not require
  linking the descriptive catalogue strings.
  Raw codecs validate wire shape/access, not value meaning, motion readiness
  or persistence. Keep generic RTU mechanics private and profile policy explicit.
- Keep board pins and platform adapters under `examples/common/`.
- Preserve the runner's physical TX completion and RX interval/watermark
  contracts. Polling timestamps bound observations; they are not exact wire times.
  Ambiguous intervals fail explicitly. The bench turnaround exception is not
  a family-wide timing guarantee. No automatic retries or silent
  late-response recovery; FRAME still requires the checked profile parser.
- Keep fixed storage caller-owned. Prefer PSRAM for larger task-context
  traces, retained frames and caches in ESP32 applications; keep
  ISR/cache-disabled/driver-required storage and stacks internal as required
  by the actual platform contract. Measure sizes and memory watermarks.
- Maintain the bounded Python probe/stress/health tools with the console;
  add motor-state automation when typed state reads exist.
- Maintain native protocol tests and build/package metadata with code changes.
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
Its gap ledger accounts for every word through 0x013F. Undocumented gaps are
not registers to invent or include in read/write windows.

`scripts/prepare_serial_contrasts.py` preserves the additional manufacturer
manuals and verifies `serial_contrasts_sources.json` offline with `--check`.
It also refuses changed snapshot bytes. These references inform the design;
they do not establish support for the compared drives.
