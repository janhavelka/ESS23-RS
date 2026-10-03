# Features implementation tasks and open questions

This is the working backlog for `MotorControl-RS`. The three-layer architecture
is accepted; implemented blocks supply units, register metadata and checked
ESS codecs with a minimal probe. Checked design decisions below remain documentation milestones,
not claims of complete motion API or hardware support. Keep this list current
after each completed logical block, and commit
and sync that block under [the repository guidance](../AGENTS.md).

## Accepted design

- [x] General framework-independent common axis API, drive profiles and
  application integration; ESS first, Leadshine iEM-RS as the design contrast.
- [x] Keep CANopen in a separate future library, initially for the verified
  CL86-C subset; maintain one motion contract and independent bus ownership.
  Extract common units/types only after concrete reuse in both implementations.
- [x] Stateless bounded codecs and optional caller-owned finite sequencing;
  application-owned transport, timing, scheduling, retry policy and health.
- [x] Common step/count/angle/travel modes with explicit units, gearing,
  origin, rounding, limits and multi-turn versus wrapped-angle semantics.
- [x] Complete documented native command access per supported model/protocol/
  firmware, with typed extensions and API/CLI parity.
- [x] Separate acknowledgement, completion, uncertain execution, device state
  and communication/readiness/freshness health.
- [x] Interruptible stop with explicit deceleration/queue behavior; unsupported
  operations fail before transmission.
- [x] Discovery and minimal non-changing probe requirements for every drive
  profile/manufacturer grouping, with explicit capability/evidence gaps.
- [x] Record COM13 bench availability, free-shaft mounting and testing
  authorization in [hardware bench notes](hardware_bench.md).

The authoritative contracts are [architecture](architecture.md),
[axis API](axis_contract.md), [profiles](profile_contract.md),
[discovery](discovery_contract.md) and [CLI](cli_contract.md).

## Implementation blocks

1. **Completed ESS protocol core:** FC03/FC06 and four reviewed FC10 windows,
   checked replies, independent CRC/frame tests and raw word conversions.
   Probe reads model register 0x0000/one word. General FC10 limits and typed
   field meanings remain unresolved where documented; no hardware I/O occurred.
2. **Read-only E2 bring-up:** runner, polling adapter, probe CLI, native tests
   and a 100-probe bench run are complete. Existing CO2control flash was backed
   up before upload. Next verify the RX timing assumptions with an external
   TX/RX/DE trace and resolve raw model `0x4EEA`; add explicit identity/state
   reads after reviewing their semantics. See the dated bench report.
   Develop the load-tolerant capture and bus-owner reference here before
   eventual FieldCore integration; see the staged transport work below.
3. **First motion and stop:** target quantization/reference/limits, required
   ESS setup and bounded operation sequencing, interrupting stop and uncertain
   outcomes. Test failure events natively before qualifying small step/angle
   moves and confirming the encoder/subdivision assumptions on the bench.
4. **Expand and integrate:** velocity/homing and complete native ESS operations,
   discovery scans and CLI parity; standalone Arduino/ESP-IDF qualification,
   then FieldCore integration in its own repository.

The package is `MotorControl-RS`; namespace/includes/CMake use `MotorControlRS`.
The checkout and GitHub URL remain `ESS23-RS` pending the user's remote rename.

## Immediate next implementation block

The independent native wire fixture, E2 load fixture and optional GPTimer
capture are implemented and have bench evidence in the
[capture/load audit](reports/2026-10-03_capture_load.md). External TX/RX/DE
measurements, cache-off qualification and the production CPU budget remain open.
The [release roadmap](roadmap.md) defines the delivery order and release gates.

The next block adds the small bounded bus owner: pending requests, explicit
queue limits, retained results, fairness and priority for a pending stop after
in-flight transport settlement. Reuse the runner, keep this code in the
application layer, and leave FieldCore read-only. Typed identity/state reads
and then first motion/stop follow the reviewed transport and drive prerequisites.

## ESS source and profile work

- [x] Transcribe the complete ESS appendix, named choices/bitfields and indexed
  records into a canonical JSON ledger and generated C++ catalogue: 221 logical
  records / 242 words. Complete typed operational and CLI coverage remain unimplemented.
- [x] Implement checked raw ESS codecs and their reviewed access/window policy.
  Raw FC06 words are not typed value validation or complete native command coverage.
- [x] Audit every appendix address: 78 undocumented words in 15 intervals,
  16 explicitly reserved words and two words with unspecified access are
  recorded and checked by the generator. No missing named register was found.
- [x] Extend FC10 to the position, speed and homing examples on pp16/18/20;
  correct p16's CRC-valid but malformed request in independent fixtures.
- [x] Audit RTU framing and motion/configuration timing in the
  [timing reference](reference/09_timing_and_gap_audit.md). Keep unspecified
  response, save and startup deadlines for later hardware qualification.
- [x] Fix unit conversions to require gearing/lead only across relevant
  bases, and combine bounded scale/time factors before the input value to
  avoid intermediate underflow on binary64 `long double` platforms.
- [x] Re-audit original ESS wire/probe pages and document retained ambiguities;
  download/audit five contrasting serial manufacturers in the
  [protocol review](reference/08_serial_protocol_review.md).
- [ ] Resolve or explicitly retain every ambiguity in the
  [implementation reference](reference/01_implementation_reference.md), using
  original PDF pages and later exact-model readback.
- [ ] Establish read/write windows and counts, widths, signedness, ranges,
  paired-word order, reserved bits and exception semantics.
- [ ] Establish command/feedback/encoder units, speed and ramp scaling,
  host-origin mapping and configuration dependencies.
- [ ] Define typed ESS identity, telemetry, motion, homing, stop, auxiliary,
  I/O, segment, limits, tuning, communications and persistence operations.
- [ ] Preserve external-input-only segment execution and model-specific I/O
  counts; expose available serial configuration without fictional triggers.
- [ ] Map common operations to ESS with exact prerequisites, completion
  evidence, read side effects, partial-write outcomes and replay rules.
- [ ] Complete the Leadshine design comparison before fixing common API
  signatures; implementation/hardware qualification remain separate work.

## Common API and desired modes

- [x] Implement pure displacement, velocity and acceleration conversions with
  independent unit preferences (including steps/s², degrees/s², radians/s²,
  rpm/s), caller-owned rational scales and documented bench defaults.
- [x] Record 1.8° / 200 full-step geometry, default 1000-line / 4000-count
  encoder evidence, scaled-feedback distinction and unknown encoder part.
- [x] Check conversion errors without mutating outputs; preserve exact native
  integers through separate range/narrowing helpers. Full target preparation,
  origins, rounding and motion-limit reports below still need implementation.

- [ ] Implement capability/configuration inspection and exact native value
  preservation, with structured unsupported/unresolved/unimplemented reasons.
- [ ] Implement absolute/relative moves in command steps, full steps and
  identified encoder counts where mappings exist.
- [ ] Implement turns, degrees and radians: unwrapped multi-turn positioning
  and wrapped orientations with direction and half-turn tie policy.
- [ ] Prepare and execute configured linear-travel moves, initially mm, using
  the existing velocity/acceleration conversions and rational scale/gear/lead.
- [ ] Implement conversion provenance, precision/rounding reports, overflow,
  effective-target/path limits and reference/configuration generations.
- [ ] Implement velocity/jog, explicit ramps and profile keepalive deadlines;
  optional torque/current modes only where the profile supports them.
- [ ] Implement enable/release, homing methods, host-origin changes, documented
  device-counter changes, alarm clear and state observations.
- [ ] Implement caller-owned contexts, operation correlation, retained
  uncertain outcomes, stop preemption and explicit queue disposition.
- [ ] Preserve per-field actual/commanded, valid/unknown/stale and raw/native
  evidence; keep application health and clock ownership outside the core.

## Discovery and minimal probes

- [ ] Record discovery/probe/identity support for every supported profile and
  manufacturer group, including explicit unsupported or unresolved entries.
- [x] Select and implement the smallest documented ESS query: FC03 model word
  0x0000/one word, with checked raw reply. No consuming side effect is documented.
- [ ] Qualify ESS probe latency, firmware behavior and communication-watchdog
  interaction on hardware before claiming a measured non-changing fast probe.
- [ ] Design the contrasting Leadshine query with the same requirements;
  do not consume its read-to-clear status as a generic presence probe.
- [ ] Implement `getDiscoveryCapabilities`, `prepareProbe` and structured
  responsiveness/identity/ambiguity results separately from full identity reads.
- [ ] Implement application-owned bounded address/baud/format discovery with
  profile/manufacturer filters, limits, cancellation and partial results.
- [ ] Validate compatible query candidate sets; never assume safe automatic
  protocol mixing or infer a manufacturer from a valid CRC alone.
- [ ] Preserve host settings/selection and restore them after scans; expose
  restoration failure, collisions and unresolved matches.
- [x] Add read-only CLI `probe`/`ping` through the existing public ESS probe
  builder and checked parser. The common discovery preparation API is still planned.
- [ ] Add CLI catalog, explicit `read identity` and bounded `discover` through
  the same public APIs as upper firmware.
- [ ] Qualify latency and non-changing behavior per exact model/firmware;
  retain one-attempt quick probing and explicit bounded scan retry policies.

## Standalone examples packaging and integration

- [x] Revisit current ownership and dependency boundaries and record the
  [architecture report](architecture_report.md), including the actual file
  map, debugging flow and current FieldCore integration gaps.
- [x] Add real public headers/source, native tests, version/package metadata,
  CMake builds and an offline desktop/Arduino units preview.
- [x] Audit FieldCore build settings and record the user-confirmed E2 HW2.0
  RS485 pins TX47/RX48/DE21 separately from its current CO2control product profile.
- [ ] Implement common CLI inventory/dispatch and full native command coverage
  through public APIs, cached status/health and retained operation results.
- [x] Implement example-owned RTU runner with fake callback tests: physical
  TX drain, wire-timed receive framing, explicit echo, exception length,
  cancellation, retained diagnostics and recovery admission.
- [x] Implement the E2 UART adapter with physical TX-idle intervals, RX
  capture intervals, UART error reporting and actual-source SDK fake tests.
- [ ] Externally qualify RX FIFO/stop sampling and idle-watermark assumptions,
  DE setup/hold and load tolerance. Probe success is not full timing qualification.
- [x] Allocate larger task-context trace/frame/cache storage in PSRAM in the
  standalone application; document fallback, retain required internal storage, and
  record free/minimum/largest-block memory and stack measurements.
- [x] Add Python bounded probe/stress and cached health/memory watching with
  JSONL evidence, no retry/recovery and native fake serial tests.
- [x] Audit the E2/console/harness path: fix RX silence races, fault-result
  availability, exception recovery policy, cached codec details and host
  evidence validation; test the actual application loop with shared SDK fakes.
  See the [0.5.1 audit](reports/2026-10-03_audit.md); external timing remains open.
- [ ] Extend automation to actual motor state once typed state reads exist.
- [ ] Build standalone ESP32-S2/S3 Arduino consumers and a first-class native
  ESP-IDF consumer with equivalent command semantics for equivalent features.
- [x] Verify framework-free native consumption, self-contained headers and
  a clean core source package; exclude vendor downloads from that package.
  Embedded package consumers beyond the compiled Arduino preview remain future work.
- [ ] Add later FieldCore adapter work in that repository: typed motor control,
  FC06 echo handling, larger TX frames, exception framing, exact integers,
  receive timing evidence, scheduling and stop priority. Standalone work must
  not depend on this step. Recheck its current product composition as well as
  the matching physical board pins.

## Transport reference before FieldCore integration

FieldCore is read-only for the current session. Major transport development
and testing belong here, with the public motor library remaining independent
of the standalone bus owner. See the
[development route](reference/10_runner_platform_review.md#develop-the-integration-reference-here).

- [x] Extend native fixtures for delayed task service and captured wire events;
  check lost timing, UART overflow, late replies and bounded terminal results.
- [x] Add an E2 task/USB/load fixture and measure service gaps, CPU cost, errors,
  transaction latency and memory. Implement GPTimer capture and physical DE
  release while the owner sleeps; retain atomic completion/release observations.
- [ ] Independently qualify TX/RX/DE and RX publication/idle assumptions; measure
  cache-off/interrupt starvation behavior and decide the production CPU budget.
  Current 20-us capture is a measured reference, not a blanket timing guarantee.
- [ ] Build a small standalone bus-owner reference with bounded queue/admission,
  multiple request producers, retained results, fair scheduling and priority
  for a pending stop. Settle in-flight transport and recovery explicitly.
- [ ] Exercise FC06 echo/ack, all reviewed FC10 request sizes, exception and
  late-response behavior using fake responders; add physical write qualification
  only with reviewed typed motor operations and their prerequisites.
- [ ] Package repeatable scenarios and a mapping to FieldCore's request/result,
  module and backend boundaries. Preserve its other serial framing modes in
  future integration regressions; do not clone the whole FieldCore task here.
- [ ] Later, under separate authorization, adapt FieldCore's existing owner
  and test real concurrency, sensor regressions and product load there.

## Verification and COM13 bench work

- [ ] Add one repeatable full verification command and CI for native suites,
  generated files, header isolation and package consumption. Require Python
  checks for repository/release validation; ordinary C++ consumers remain
  independent of Python. Record the supported compiler/platform matrix.
- [x] Inspect COM13's CO2control firmware, preserve flash, build/upload the E2
  probe and establish communication at address 1, 115200 8N1, TX47/RX48/DE21.
- [x] Record the first minimal model read, raw frames, interval evidence and
  explicit 304 us bench turnaround exception; complete repeated probe tests.
- [ ] Establish exact motor/firmware identity and echo behavior for writes.
  Raw model `0x4EEA` differs from the manual example; no mapping is invented.
- [x] Validate codec golden frames and malformed inputs with unchanged payload
  outputs on errors; test exception/echo/word-order/count boundaries.
- [ ] Validate conversion, angular-path, capability, sequencing and interruption
  behavior independently of hardware; exercise failure/timeout events.
- [ ] Qualify small moves in steps/angle/travel, velocity, homing where available,
  enable/release, stopping, limits and stale/reference handling on the bench.
- [ ] Qualify native I/O/segment configuration, tuning and persistence commands
  as applicable, recording the before/after active and saved configuration.
- [ ] Exercise lost acknowledgements, partial setup, late replies, cancellation,
  power/reconnect and communication changes without accidental motion replay.
- [ ] Record exact hardware/firmware/build/settings and distinguish software
  test success, protocol observations and physical motion outcomes.

## Open questions

Resolve from references, existing firmware and bench observations where
possible. This list is not a request for the user to answer everything now.

| Question | Evidence or next action |
| --- | --- |
| When does the GitHub repository URL change? | The user will rename the remote; update metadata then. Local package/API rename is complete. |
| What motor/model/firmware is actually connected? | Read documented identity; compare model markings if identity is insufficient. The project's RS20 target is not a bench measurement. |
| What firmware/host path is active on COM13? | Resolved for the recorded bench: MotorControl-RS 0.5.1 JSONL probe console, with original CO2control backup retained. Recheck identity at each new hardware session; it is not a raw RTU bridge. |
| What are the board pins, DE/RE polarity, echo topology and bus wiring? | User confirms E2 HW2.0 TX47/RX48/DE21. Matching FieldCore HW200 source uses UART2/active-high DE. Live polarity, echo and wiring qualification remain. |
| Which address/baud/format is active despite reported defaults? | Replies are observed at node 1, 115200 8N1. Device configuration/DIP readback and bounded discovery remain pending. |
| Is the selected ESS probe qualified on the connected firmware? | Repeated checked FC03 0x0000/one-word replies have bench evidence. Exact model/firmware, external timing and communication-watchdog interaction remain unqualified. |
| Can discovery distinguish a manufacturer/model or only a responder? | Record exact reply evidence and retain ambiguous candidates; no guessed selection. |
| Which candidate protocols can be probed on the same bus? | Review query effects for the actual attached families; use an isolated target when compatibility is unknown. |
| What are the actual ESS command/feedback subdivisions and signed limits? | Resolve p69-70 inconsistencies with configuration readback and measured movement. |
| What are the speed/ramp units and valid ranges? | Reconcile command prose and ESS appendix before physical-unit conversion. |
| What are the exact FC10 limits and partial-write effects? | Vendor evidence plus bounded firmware qualification; do not use generic maximums as device facts. |
| Which state/alarm/completion reads are non-consuming and correlated to a new operation? | Profile access ledger and controlled sequencing tests. |
| What persists, applies immediately, waits for save or needs a power cycle? | Resolve access notation and test exact firmware; retain unknown application state after lost writes. |
| How do collision-homing duplicate parameters and outside-map references apply? | Reconcile original pages/model scope; no guessed aliases or addresses. |
| What happens on communication loss, release, restart and external shaft motion? | Qualify per profile; update stop/reference/keepalive contracts with observations. |
| Which Leadshine model/firmware and hardware will qualify the contrast? | Select a concrete target before advertising support; current comparison is documentary. |

## Later extensions

- [x] Assess adding selected CANopen/CiA 402 drives alongside Modbus RTU;
  [feasibility review](reference/07_canopen_feasibility.md) records the accepted
  separate-library boundary and the user's Lichuan CL86-C candidate.
- [ ] In the separate future CANopen library: obtain firmware-matched CL86-C
  manual/EDS, qualify a CAN transport/stack adapter, define its event/state/stop
  contracts, then implement identity/position/stop against the shared motion
  vocabulary. Keep host-streamed cyclic modes and cross-bus synchronization separate.
  Manufacturer manual/EDS listings were found; direct downloads were unavailable
  during review, so exact object semantics and loss-of-controller behavior remain open.
- [ ] Once the second library implements real operations, verify matching
  motion semantics and extract genuinely shared units/types with independent
  protocol dependencies. No speculative common package is needed now.

- [ ] Qualify additional explicit families from the
  [manufacturer review](reference/03_multi_vendor_feasibility.md), with full
  native coverage and their own discovery/probe evidence.
- [ ] Add non-Modbus framing only for a concrete profile, preserving common
  API semantics and independently reviewing shared-bus compatibility.
- [ ] Consider blending, online target updates, queued paths and advanced
  ramps as declared capabilities rather than silent approximations.
- [ ] Treat coordinated multi-axis timing/trajectories as a separate design
  with explicit bus and device guarantees.
