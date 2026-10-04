# Features implementation tasks and open questions

## Regular API and passive traffic observation

- [x] Removed the separate functional firmware/flags; regular preparations and console routes handle actions and native finite positioning.
- [x] Typed position-profile snapshot/staging/restoration; native absolute targets retain unknown displacement when a reference is unnecessary.
- [x] Installed caller-owned TrafficCapture and checked ESS translation; console `sniff off|raw|decoded`, independent reader cursors and explicit diagnostic loss.
- [x] Observer on/off/overflow native equivalence; ordinary firmware motion/stop/readback with raw/decoded display. [Evidence](reports/regular_api_sniff_2026-10-04.md).


## Short unattended functional campaign (2026-10-04)

- [x] Prepared bounded host phases (now exercised through the regular firmware API); no analyzer or person-at-bench prerequisite for this scope.
- [x] Drive-reported release/enable, finite positive/reverse activity and normal/direct stops during motion; exact profile restoration and ten-probe regression. [Evidence](reports/functional_motion_2026-10-04.md).
- [x] Preserve UNKNOWN FC06 execution and observed completion separately; literal native-zero experiment keeps displacement unknown; bounded read-only speed settling retains transient values.
- [ ] Independent shaft/electrical measurements, algorithm3 and feedback/arrival precision remain unresolved; no calibrated displacement claim. Several-hour soak intentionally NOT RUN.


## Prompt20 drive communication commissioning

The [fresh independent audit](reports/ess_release_20_audit_2026-10-04.md) fixes
confirmation retention on unchanged host selection and exact Python
candidate/session correlation. Native/package/four builds and37 corrected-image
read-only frames PASS; physical activation/restoration gates remain open.

- [x] Typed address/baud/format preparations, exact before/requested/readback/responding contexts, exclusive owner lease and finite explicit confirmation; no hidden save, restart or replay.
- [x] Native core/owner/actual application/CLI/Python failure cases, installed consumer and four firmware builds PASS; final COM13 image passes 37 read-only frames and all three zero-TX qualification gates. See [handoff](reports/ess_release_20_2026-10-04.md) and [API](ess_communication.md).
- [ ] Physical setting changes, activation and restoration NOT RUN: actual motor restart and qualified route back are unavailable. Address save activation goes to prompt21; baud/format save requirement and acknowledgement/activation timing remain unresolved.
- [ ] Two exhausted confirmation slots retain ownership/evidence for operator/application handoff; automatic scans or write replay are not a recovery route.

## Prompt19 host tuple support

- [x] Sixteen reviewed SDK/ESS tuples, UART divider readback, character/gap/capture budgets and default-tuple-only first-reply exception.
- [x] One idle configuration lease; queued/active/DE/continuation exclusion, failed setup/restoration interlock and explicit repair, preserving unread results.
- [x] Independent serial versus endpoint/configuration generations; historical tuple retention and current confidence invalidation without erasing host coordinates.
- [x] Matching application callback, console `host` commands and finite Python checks; 47 native suites, 184 Python cases, installed consumption and four firmware builds PASS.
- [x] COM13 sixteen host setups and two deliberate mismatched reads/recovery/restoration PASS; ten subsequent probes PASS, unchanged motor settings/state. [Evidence](reports/ess_release_19_2026-10-04.md).
- [ ] Alternate-tuple successful motor communication, external clock/FIFO/final-stop/TX/RX/DE qualification and loaded tuple-change/endurance tests remain open. Device commissioning/discovery belong to 20/22.
- [x] Fresh audit fixes retained cross-read configuration comparison, pinned SDK timer transition/fake repair and immutable Python diagnostics; 12 cache cases and 32 complete tuple replies PASS. [Audit](reports/ess_release_19_audit_2026-10-04.md).
- [ ] Strict NO_RESPONSE mismatch campaigns fail on reproduced LENGTH/F9 at 9600, LENGTH/E5 at 19200 and RX_ERROR at 38400 8E1. Explicit diagnostic recovery/restoration and unchanged readback/ten probes PASS. Physical malformed-byte/UART-error source remains unresolved; raw error bitmap and wire measurement are missing. No retries or relaxed production check.

The [fresh prompts 17/18 audit](reports/ess_release_17_18_audit_2026-10-04.md) fixes
settings freshness, copied provenance, partial-refresh invalidation and strict
console evidence validation. All 45 native suites, installed consumption and
four firmware builds pass. The final COM13 image passes 104 frames and restores
input filter `2?3?2` and lock delay `200?201?200`; physical effects remain unqualified.

## Prompt18 filters, tracking and tuning

- [x] Twenty reviewed typed native reads/writes, four bounded groups and two-stage LA helpers; one existing settings sequencer and public/CLI/Python validation path.
- [x] Whole candidates, exact model/effects evidence, local-mask isolation, partial/uncertain updates and immutable deadline/progress; 45 native suites, installed consumer and four firmware builds PASS.
- [x] Independent cache/effects review; per-group baselines preserve external threshold-change detection and failed-refresh history. All20 named fields and58 actual Console/Python terminals PASS.
- [x] COM13 all-field readback and finite input-filter2→3→2 stored restoration PASS; originals restored, zero transport/capture errors and owner empty. [Evidence](reports/ess_release_18_2026-10-04.md).
- [ ] Earlier collision003B/003C access remains unavailable; later collision0/0 is outside documented ranges on firmware0029 and preserved unknown. Exact firmware applicability remains unresolved.
- [ ] Physical filter timing, gain stability/scaling, arrival/deviation and collision effects NOT RUN. No gain sweep, alias, engineering-unit guess or universal tuning default.

## Prompt17 control settings

The [fresh16/17 audit](reports/ess_release_16_17_audit_2026-10-04.md) fixes late
ACK/exception certainty, Python parity and optional-host help availability;
actual CLI recovery under full result pressure retains interrupted uncertainty.
42 native suites, installed/four-build checks and114 corrected-image bench frames
PASS; lock delay restores exactly. Remaining source/physical gaps below stay open.

- [x] Eight typed native reads/writes through the bounded settings sequence, checked exact model/effects context and whole/intermediate candidate validation.
- [x] Zero closed-loop encoder-scale guard, external-scale invalidation, immutable old results, partial/uncertain updates and CLI/Python parity;42CTest suites and installed/four-build checks PASS.
- [x] COM13 all-field readback, nine zero-TX gates and reversible lock-delay200-to201-to200 stored restoration PASS; [report](reports/ess_release_17_2026-10-04.md).
- [ ] Algorithm3, model effective-current ceiling, percent denominator and active torque/encoder/lock effects remain unresolved/NOT RUN. Peak1-4A is not an effective-current conversion; no feedback disable/current increase was performed.


## Prompt16 stored records

The [combined fresh audit](reports/ess_release_16_17_audit_2026-10-04.md) verifies
all48 indexed records on COM13 and adds actual CLI recovery/no-continuation
coverage. Shared-start write FAIL and guarded pair/sign semantics remain unchanged.

Fresh [independent audit](reports/ess_release_16_audit_2026-10-04.md) fixes host scenario vocabulary, verifies actual terminal correlation and repeats53 non-changing bench frames; starting-speed acceptance remains unresolved.

- [x] All16 position/speed/shared-start reads and native scalar settings; one indexed helper per layout and existing bounded settings sequence; public/CLI/Python parity.
- [x] Reserved slots and all16 pulse-pair write blockers; storage16 versus maximum8 input selections; no serial trigger.
- [x] Native/package/four firmware builds and boundary COM13 reads; PT1/PV16 scalar writes/readback/restoration PASS.
- [ ] Shared-start1 native0 to1 readback0: hardware FAIL/uncertain, no replay; source-confirmed FC06 acceptance/exact firmware semantics missing. Negative signed encodings and paired writes remain guarded.
- [ ] External-trigger behavior NOT RUN: inputs physically unwired and stop/fixture qualification absent. See [report](reports/ess_release_16_2026-10-04.md).


## Prompt 15 optional I/O and external controls

[Handoff/evidence](reports/ess_release_15_2026-10-04.md), [public API](ess_io.md):

- [x] Existing settings sequencer supports four indexed inputs/two outputs,
  explicit function-zero disabling, checked polarity/custom masks, unknown raw
  values, whole-candidate prerequisites, partial effects and immutable progress.
- [x] Direct/CLI/Python routes share preparation; finite retention/correlation,
  strict provenance and uncertain echo/readback distinctions are tested.
- [x]190-frame COM13 campaign,13 explicit passive/unloaded settings updates,
  exact original restoration and short existing non-changing regression PASS.
- [x] Optional-unwired serial move admission and no-terminal method35 regressions;
  switch-dependent methods retain visible capability and reject before TX.
- [ ] External switches/output loads, electrical state/activation, control
  precedence and physical trigger/homing transitions remain NOT RUN/unqualified.
- [ ] Output11/custom2 and crossmapped custom actuation remain unavailable.
  Prompt16 owns external segment sequencing; setter success cannot establish
  a real connected trigger/selector fixture or synthesize its edges.

## Prompt 14 homing and reference

[Fresh audit](reports/ess_release_14_audit_2026-10-04.md) fixes the first post-home feedback baseline and strict host readiness/failure evidence; checked trigger exceptions now have actual console parity. Native33/33, Python167, parity15 and corrected-image read-only COM13/gates pass; physical qualification remains open.

[Handoff](reports/ess_release_14_2026-10-04.md), [API](ess_homing.md) and [35-method table](ess_homing_methods.md):
- [x] Typed internal-index33/34 and current-position35 staging/trigger/observation/zero sequences, installed API and common/profile console routes.
- [x] Fresh low-to-high homed evidence, bounded status polls, stop preemption, copied provenance and reference-generation exhaustion guards.
- [ ] Physical homing/zero/return qualification: timing/echo, method mechanics, native rate/ramp, active auxiliary7 and native-coordinate correspondence remain missing.
- [ ] External-switch methods: prompt15 must revisit actual wiring, existing assignments/polarity, initial levels, branch-specific edges and return observations; no setter or switched trajectory is claimed here.
- [ ] Method18 conflicting limit diagram, collision negative encoding/parameter conflicts, nonzero offset order/sign/scale remain unresolved.

## Prompt 13 driver settings and software limits

[Handoff](reports/ess_release_13_2026-10-04.md): typed reads and seven stopped-state
single-word settings, whole-candidate validation, retained partial progress and
configuration effects are implemented/native tested. Current-image read-only
COM13/gates pass. Physical writes/restoration remain NOT RUN behind timing/FC06/
input qualification. Positive/negative pair writes remain explicitly unsupported;
soft-limit movement needs qualified encoding, homing/reference and fixture
evidence. External-switch homing evidence and optional input changes remain with15 and fixture qualification.

[Fresh audit](reports/ess_release_13_audit_2026-10-04.md) fixes unconfirmed
readback settlement, shared driver/configuration baseline ordering, secondary
target cache eviction and stale input qualification. Native/package/build and
corrected-image read-only verification are recorded separately from physical
settings/restoration and limit-motion qualification.


## Prompt 12 paired-write disposition

- [x] [Fresh audit](reports/ess_release_12_audit_2026-10-04.md) fixes hexadecimal
  address consistency in catalogue emitters; 27 native suites, installed consumer,
  four firmware builds and ten additional read-only probes pass. Pair gates unchanged.

- [x] Inventory all 20 writable pairs and preserve unresolved order/sign/range
  and partial-application reasons in the ledger and derived coverage.
- [x] Generate the exact four FC10 windows from the same ledger; native exhaustive
  admission/paired-half tests and a catalogue-independent codec link pass.
- [x] Independent PDF/access and wire reviews, 27 native suites, installed consumer,
  four firmware builds and 38 read-only COM13 frames pass.
- [ ] Establish admissible limit/stored-target write forms and home-offset order;
  no new typed setters or FC10 spans are justified by current source evidence.
- [ ] Physical pair write/readback/restore remains NOT RUN behind timing/source
  and stopped-state/input prerequisites. No device write was attempted.

See [per-pair restrictions and experiment](ess_pair_writes.md) and
[prompt 12 evidence](reports/ess_release_12_2026-10-04.md). Prompt 13 implements
independently supported single-word settings while retaining these pair guards.

This is the working backlog for `MotorControl-RS`. The three-layer architecture
is accepted; implemented blocks supply units, register metadata and checked
ESS codecs with a minimal probe. Checked design decisions below remain documentation milestones,
not claims of complete motion API or hardware support. Keep this list current
after each completed logical block, and commit
and sync that block under [the repository guidance](../AGENTS.md).

## Accepted design

- [x] General framework/platform-independent common axis API, drive profiles
  and application integration; ESS first, Leadshine iEM-RS as design contrast.
  ESP32-S3 is the available standalone bench, not a required platform or product.
- [x] Scope the reference workflow to RS485 request/wait/result ownership.
  FieldCore's future motor device does not exist yet; its RS485 task is read-only
  integration reference. Other buses and product composition are outside scope.
- [x] Accept optional external motor I/O: distinguish wiring from drive function
  assignment, support explicit documented disable and require inputs only for
  operations that use them. Typed I/O setters remain planned; this records the design decision.
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
2. **Read-only ESP32-S3 bench bring-up:** runner, polling adapter, probe CLI, native tests
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
Metadata is prepared for the user's GitHub/folder rename to `MotorControl-RS`;
see the [rename steps](repository_rename.md). This does not change the API.

## Immediate next implementation block

The independent native wire fixture, ESP32-S3 load fixture and optional GPTimer
capture are implemented and have bench evidence in the
[capture/load audit](reports/2026-10-03_capture_load.md). External TX/RX/DE
measurements and the production CPU budget remain open.
[Prompt 04](reports/ess_release_04_2026-10-04.md) retains the measured 20-us
capture, enforces timer starvation failure and extends available read-only
bench evidence to fixed 37-byte replies. Cache-off capture is explicitly
unsupported. Prompts 05–06 implement typed identity/configuration/state reads
and separate health. [Prompt 07](reports/ess_release_07_2026-10-04.md) implements
exact host target preparation. [Prompt 08](reports/ess_release_08_2026-10-04.md)
implements bounded actions and priority stop with retained uncertainty and
application axis reservation. Prompt 09 is next when separately dispatched;
physical actions/motion retain their timing/echo and operation-specific prerequisites.
The [release roadmap](roadmap.md) defines the delivery order and release gates.

Release prompt 01 is implemented: [BusOwner](bus_owner.md) provides FIFO
admission, copied frames/expectations, reserved retained results, stable IDs,
checked parser settlement and absolute closure deadlines through the real Runner.
Native tests pass; the rebuilt Runner passed read-only bench regression.
Prompt 03 now connects the actual owner and has read-only timer bench evidence;
absolute boundary/fault combinations remain explicitly native where not injected live.
See [the prompt 01 handoff](reports/ess_release_01_2026-10-03.md).
Its [fresh independent audit](reports/ess_release_01_audit_2026-10-03.md)
corrected earlier-budget timeout precedence and retained final-gap evidence
on relative byte expiry; focused regressions and current-image probes pass.

Prompt 02 is implemented: cyclic producer fairness, deferred per-producer FIFO,
reserved urgent admission/results, local cancellation, sequence invalidation and
explicit recovery that cancels old queued work. Its separate recovery result
survives full ordinary/urgent result pressure; old writes never resume.
See [the prompt 02 handoff](reports/ess_release_02_2026-10-03.md). Owner hardware
is measured through the actual console in [prompt 03](reports/ess_release_03_2026-10-03.md).
Its [fresh independent audit](reports/ess_release_02_audit_2026-10-03.md) fixes
delayed cancellation versus later expiry and retains recovery timing evidence
across drain passes; native regressions and firmware builds pass.
FieldCore remains read-only; typed identity/state and motion/stop retain their gates.

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
- [x] Implement explicit ESS input/output disable through documented function 0
  (`UNDEFINED`: no function), sharing the typed function setter with API/CLI.
  Preserve unknown output electrical state and serial/external-control precedence;
  cable absence and polarity inversion are not disabling. Prompt15 delivers setters and selected stored-readback bench evidence; external effects remain separately qualified.
- [ ] Verify serial-only operation with optional I/O unwired or explicitly
  disabled, using readback prerequisites from 05 and admission tests in 08/09.
  Never rewrite input functions implicitly during startup, probes or motion.
- [ ] Preserve external-input-only segment execution and model-specific I/O
  counts; expose available serial configuration without fictional triggers.
  Missing/disabled homing or trigger inputs block only dependent operations.
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
  integers through separate range/narrowing helpers and exact rational target
  preparation, explicit rounding, checked origins and endpoint limits in 07.
- [x] Implement pure motor/load/native target preparation, identified encoder
  and configured travel mappings, precision provenance, generation checks and
  evidence-gated host configuration/origin APIs. Prompt10 now implements wrapped
  paths and shared finite position execution; physical qualification is separate.

- [ ] Implement capability/configuration inspection and exact native value
  preservation, with structured unsupported/unresolved/unimplemented reasons.
- [ ] Implement absolute/relative moves in command steps, full steps and
  identified encoder counts where mappings exist.
- [x] Implement turns, degrees and radians: unwrapped multi-turn positioning
  and wrapped orientations with direction and half-turn tie policy in prompt10.
- [ ] Prepare and execute configured linear-travel moves, initially mm, using
  the existing velocity/acceleration conversions and rational scale/gear/lead.
- [x] Implement conversion provenance, precision/rounding reports, overflow,
  effective-target/path limits and reference/configuration generations.
- [x] Implement finite signed serial velocity, configured native ramp snapshots,
  bounded observations and shared priority stop in prompt11; exact rate conversion,
  native/CLI parity and unknown cleanup are tested.
  The [fresh audit](reports/ess_release_11_audit_2026-10-04.md) fixes clean idle
  interruption cleanup and strict retained-evidence checks, with ten actual
  core/console outcomes checked by Python. Physical qualification stays separate.
- [ ] Qualify ESS speed/ramp factors and physical acceleration/stop. Engineering
  acceleration mapping, external JOG, jerk/blending/live updates, torque/current
  remain unsupported; no ESS refresh or communication-loss stop is established.
- [ ] Implement enable/release, homing methods, host-origin changes, documented
  device-counter changes, alarm clear and state observations.
- [x] Implement caller-owned action contexts, operation correlation, retained
  uncertain outcomes, stop preemption and explicit queue disposition.
- [x] Reserve same-axis operations across action staging/trigger/observation
  while allowing eligible unrelated bus work. Validate/reserve stop before
  superseding active work; rejected stop admission must not cancel it.
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
- [x] Record the standalone ESP32-S3 bench settings and RS485 pins TX47/RX48/DE21
  in [bench configuration](reference/04_esp32_bench.md). Adapter configuration is
  example-owned; no FieldCore product or other bus defines the motor API.
- [ ] Implement common CLI inventory/dispatch and full native command coverage
  through public APIs, cached status/health and retained operation results.
- [x] Implement example-owned RTU runner with fake callback tests: physical
  TX drain, wire-timed receive framing, explicit echo, exception length,
  cancellation, retained diagnostics and recovery admission.
- [x] Implement the ESP32-S3 UART adapter with physical TX-idle intervals, RX
  capture intervals, UART error reporting and actual-source SDK fake tests.
- [ ] Externally qualify RX FIFO/stop sampling and idle-watermark assumptions,
  DE setup/hold and load tolerance. Probe success is not full timing qualification.
- [x] Allocate larger task-context trace/frame/cache storage in PSRAM in the
  standalone application; document fallback, retain required internal storage, and
  record free/minimum/largest-block memory and stack measurements.
- [x] Add Python bounded probe/stress and cached health/memory watching with
  JSONL evidence, no retry/recovery and native fake serial tests.
- [x] Audit the adapter/console/harness path: fix RX silence races, fault-result
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
  not depend on this step. A future motor device and selected platform wiring
  belong to that integration; the current test board does not define them.

## Standalone RS485 workflow and later integrations

Major transport-reference development and testing belong here, with the public
motor library independent of the application bus owner and platform adapter.
FieldCore is a read-only RS485 workflow reference for the current session; the
same core and request/wait/result pattern must also work for other consumers. See the
[development route](reference/10_runner_platform_review.md#develop-the-integration-reference-here).

- [x] Extend native fixtures for delayed task service and captured wire events;
  check lost timing, UART overflow, late replies and bounded terminal results.
- [x] Add an ESP32-S3 task/USB/load fixture and measure service gaps, CPU cost, errors,
  transaction latency and memory. Implement GPTimer capture and physical DE
  release while the owner sleeps; retain atomic completion/release observations.
- [x] Review capture cost and retain the single 20-us sampler; expose timer/sample
  work separately and enforce starvation faults. Fixed non-consuming 0x0130/16
  read has native and physical 37-byte reply evidence in
  [prompt 04](reports/ess_release_04_2026-10-04.md).
  Its [fresh audit](reports/ess_release_04_audit_2026-10-04.md) requires complete
  capture diagnostics and rejects sticky sampling-gap violations in the host
  qualification campaign; repeated read-only hardware tests pass.
- [ ] Independently qualify TX/RX/DE, final stop-bit publication, idle and echo
  assumptions; characterize physical interrupt starvation and decide the
  production CPU budget. Analyzer evidence is NOT RUN; cache-off operation
  during capture is unsupported. FC06/FC10 physical frames remain gated by
  typed operations and relevant timing prerequisites.
- [x] Build the bounded cooperative FIFO owner with copied requests, reserved
  retained results, exact generation IDs and checked parser settlement. Native
  multi-request/failure tests pass; owner board integration is delivered by prompt 03.
- [x] Extend the owner with producer fairness, cancellation and urgent reservation
  (prompt 02); native mixed-producer/fault/pressure tests pass. No ESS stop yet.
- [x] Integrate the bounded responsive console, retained result/cancel/driver
  commands and owner recovery policy; timer read-only/load/interleaved hardware
  evidence is in [prompt 03](reports/ess_release_03_2026-10-03.md).
  Its [fresh independent audit](reports/ess_release_03_audit_2026-10-04.md)
  corrects deadline-gated adapter recovery, result reuse/inspection and cache
  attribution under failed reads and output pressure; new-image regression PASS.
- [x] Enforce immutable absolute deadlines through queue/setup/TX/closure without
  losing qualified on-time captured evidence; native evidence in prompts 01–02.
- [x] Extend recovery/cancellation generation disposition: terminally cancel old
  queued work, invalidate continuations, preserve unread results, settle TX and
  boundedly discard stale traffic; no implicit queued writes after recovery.
- [x] Separate qualified observation age, delivery and recovery-settlement in
  the probe application; delayed timer service and repeated cached queries keep
  historical age. Unsent cancellation and retained recovery preserve new data.
- [x] Exercise FC06 echo/identical acknowledgement, all four reviewed FC10 sizes,
  checked exceptions, malformed replies and deadline/late-capture behavior with
  fake responders through the actual owner/Runner (prompt 01).
- [ ] Add physical write qualification only with reviewed typed motor operations
  and their prerequisites; fake writes do not qualify the motor.
- [ ] Qualify longer reviewed non-consuming reads and the supported frame-size
  envelope separately from seven-byte model replies; preserve unavailable
  physical long-frame/write cases as open evidence.
- [ ] Package repeatable scenarios and a mapping to FieldCore's request/result,
  module and backend boundaries. Preserve its other serial framing modes in
  future integration regressions; do not clone the whole FieldCore task here.
- [ ] Later, under separate authorization, adapt FieldCore's existing owner
  and test real concurrency, sensor regressions and application load there.
  Do not add its unrelated bus abstractions or product registry to this library.

## Verification and COM13 bench work

- [ ] Add one repeatable full verification command and CI for native suites,
  generated files, header isolation and package consumption. Require Python
  checks for repository/release validation; ordinary C++ consumers remain
  independent of Python. Record the supported compiler/platform matrix.
- [x] Inspect COM13's CO2control firmware, preserve flash, build/upload the standalone
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
| When does the GitHub repository URL change? | Metadata is prepared for `janhavelka/MotorControl-RS`; the user performs the GitHub rename, then updates the clone's origin. Folder/cache steps are in [the rename guide](repository_rename.md). Package/API identity is already complete. |
| What motor/model/firmware is actually connected? | Read documented identity; compare model markings if identity is insufficient. The project's RS20 target is not a bench measurement. |
| What firmware/host path is active on COM13? | Latest recorded bench: MotorControl-RS 0.6.0 JSONL probe/load console with timer capture, with original CO2control backup retained. Recheck identity at each new hardware session; it is not a raw RTU bridge. |
| What are the board pins, DE/RE polarity, echo topology and bus wiring? | Bench pins are TX47/RX48/DE21, UART2/active-high DE; the selected example config owns them. Live polarity, echo and wiring qualification remain. |
| Which address/baud/format is active despite reported defaults? | Replies are observed at node 1, 115200 8N1. Typed configuration and raw DIP readback now have prompt05/06 evidence; DIP mapping and bounded discovery remain pending. |
| Is the selected ESS probe qualified on the connected firmware? | Repeated checked FC03 0x0000/one-word replies have bench evidence. Exact model/firmware, external timing and communication-watchdog interaction remain unqualified. |
| Can discovery distinguish a manufacturer/model or only a responder? | Record exact reply evidence and retain ambiguous candidates; no guessed selection. |
| Which candidate protocols can be probed on the same bus? | Review query effects for the actual attached families; use an isolated target when compatibility is unknown. |
| What are the actual ESS command/feedback subdivisions and signed limits? | Resolve p69-70 inconsistencies with configuration readback and measured movement. |
| What are the speed/ramp units and valid ranges? | Reconcile command prose and ESS appendix before physical-unit conversion. |
| What are the exact FC10 limits and partial-write effects? | Vendor evidence plus bounded firmware qualification; do not use generic maximums as device facts. |
| Which state/alarm/completion reads are non-consuming and correlated to a new operation? | Three reviewed non-consuming state blocks are implemented and tested in prompt06. Their reads are independent; correlation to future motion completion still requires controlled sequencing tests. |
| Which optional inputs are wired, disabled or still assigned an active function? | Read exact drive function/polarity/state settings and record external wiring separately. ESS function 0 means no function; serial/external release precedence remains unresolved. Gate only dependent operations. |
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

## Bench tooling issue from the platform-boundary audit

- [ ] Resolve repeated esptool 5.3.0 USB bootloader flash-read truncation at
  188,416 completed bytes of the tested application-region read. Traced and
  direct-file reruns reproduce an incomplete SLIP packet; exact cause remains
  open. Compare stub/ROM reads and capture USB at that packet before claiming
  a fix. Motor firmware is not running during this failure. Final-image upload
  verification and read-only RS485 regression pass independently; see the
  [audit and evidence](reports/2026-10-03_platform_scope_audit.md).

## Prompt 05 typed read handoff

The [fresh audit](reports/ess_release_05_audit_2026-10-04.md) rechecked the actual
public/core/application paths and retained evidence. No implementation defect
was confirmed; stale profile/discovery API descriptions were corrected.
Native/package/build checks and 16 additional non-changing COM13 transactions
passed on the unchanged image.

- [x] Pure caller-owned identity/configuration operations, exact gap-safe FC03 windows, checked atomic publication and copied provenance; installed-core consumer verification.
- [x] Same public preparation/event/decoder APIs through common/profile console routes and strict Python correlation, retention and release.
- [x] Complete ledger-linked operation inventory with separate source/model/read/write/action/API/CLI/native/hardware dispositions; generated descriptors remain unchanged.
- [x] User-confirmed ESS23-RS20 bench identity and documented incremental/differential/three-channel 1000-PPR encoder; nominal4000 decoded counts agrees with RS485 configured4000. See [source reconciliation](reference/11_ess23_rs20_identity.md). Encoder chip/label identification is not a prerequisite for preparation.
- [ ] Resolve universal model `0x4EEA`/firmware/DIP mapping, subdivision physical interpretation and measured shaft/feedback behavior. Product specifications and readback do not resolve undocumented algorithm3.
- [x] Prompt 06 state/input-level evidence; retain specific input-dependent homing, limit, enable/stop and trigger prerequisites without requiring external I/O for independent serial-only operations.
- [x] Prompt15 now delivers typed I/O changes;05 itself remains non-changing, and no implicit save/motion was added.

See [the typed read API](ess_reads.md) and [current report](reports/ess_release_05_2026-10-04.md) for exact readback and independent hardware disposition.

## Prompt06 disposition

- [x] Typed non-consuming MOTION/IO/FEEDBACK preparation and checked block decoders,
  retaining unknown alarms/bits and unresolved position/speed interpretation.
- [x] Per-block application cache with exact generations, independent attempts,
  conservative age, failure retention and passive status/health.
- [x] Explicit health check and finite disabled-by-default polling through the
  existing owner; cancellation, urgent scheduling and pressure tests pass.
- [x] Installed C++11 consumer and stationary COM13 raw/decoded evidence;
  [report](reports/ess_release_06_2026-10-04.md) distinguishes native and hardware.
- [ ] Establish algorithm3 semantics, feedback sign/scale, speed units and actual
  versus commanded source; these block dependent completion/conversion claims.
- [ ] External electrical capture, physical encoder/motion/stop qualification.

Logical input levels0 do not establish external wiring or limit/stop effects.
Identity success and clear alarms do not prove readiness or settle an uncertain
operation. Prompt07 adds host arithmetic without promoting that evidence into
physical coordinate, standstill or motion qualification.

The [fresh06 audit](reports/ess_release_06_audit_2026-10-04.md) separates historical feedback interpretation from raw freshness after new configuration readback and rejects contradictory/missing cached JSON fields. Native, package/build and50-frame read-only COM13 checks pass. Qualification limits above are unchanged.

## Prompt07 disposition

- [x] Installed `Axis.h` configuration/reference/request/result APIs; exact
  rational preparation shares factor selection with existing units conversions.
- [x] Native integer extremes, origin overflow, all rounding modes/ties, limits,
  source-dependent encoder/travel conversions, bounded radians and stale evidence
  tested; independent integer oracles verify rounding and binary64 allowances.
- [x] Existing console exposes strict host `axis`/`prepare` routes through the
  same public API, independent unit preferences and no bus traffic.
- [x] Native/package/four firmware builds and 23-frame read-only COM13 campaign
  pass; drive configuration unchanged. See [report](reports/ess_release_07_2026-10-04.md)
  and [preparation contract](axis_preparation.md).
- [ ] Resolve ESS source/sign/command mapping before using feedback to establish
  a native origin or physical endpoint. Operator scales remain assumptions.
- [ ] Qualify machine travel, encoder, physical limits, motion and stop with
  suitable fixtures and reviewed profile semantics; free shaft is insufficient.

The operation ledger gets no read/write/action credit for host arithmetic.
Reduced exact intermediates use bounded uint64 storage and reject overflow;
there is no implicit approximate fallback. Prompt08 is not executed here.

The [fresh07 audit](reports/ess_release_07_audit_2026-10-04.md) fixes requested
relative-displacement range checking with a reference, zero-radian exactness,
normal cancellation onto zero and malformed decimal fraction operands. Native,
installed consumer, four firmware builds and a repeated 23-frame read-only COM13
campaign pass; no device setting or qualification claim changed.

## Prompt08 disposition

- [x] Installed bounded enable/release/alarm-clear and normal/direct stop preparations,
  correlated events and separate TX/acknowledgement/completion evidence.
- [x] Shared application axis reservation, retained uncertainty across release/recovery,
  reserved stop capacity, unsent cancellation and settled in-flight evidence.
- [x] Common/native CLI and bounded Python one-attempt commands; blocked output
  preserves stop admission and both operation results.
- [x] Independent review and regressions for wrong/late events, bad FC06 responses,
  every relevant stop phase, pressure, stale knowledge and unrelated addresses.
- [x] Mark exactly five named action choices implemented/native PASS; full auxiliary
  and motion command registers remain partially implemented.
- [x] Native/package/four firmware builds and current-image read-only/action-gate
  checks; see [evidence](reports/ess_release_08_2026-10-04.md).
- [ ] Independent TX/RX/DE and FC06 echo-source qualification, then reviewed stopped-state
  enable/release/normal/direct-stop physical checks with before/after flags.
- [ ] Dynamic stop latency, moving commands, travel and independent bench stop proof
  remain dependent work. Missing fixtures do not block independent software for 09.

No new ramp/device queue behavior is inferred. Explicit unsupported policies
reject before writes; no motor writes or moves occurred in this prompt.

The [fresh prompt 08 audit](reports/ess_release_08_audit_2026-10-04.md) fixes
undocumented exceptions incorrectly clearing execution uncertainty and strengthens
harness evidence checks. Native/package/four firmware builds and 38 read-only
COM13 frames pass. New HOLD/partial-TX and interrupted-write-failure regressions
preserve priority, both outcomes and explicit recovery without replay. Physical
action gates and the operation coverage denominator remain unchanged.

## Prompt09 finite relative-position disposition

- [x] Exact common/native preparation and verified configured native ramp policy; complete parameter/precondition validation before writes.
- [x] Reviewed FC10 five-word staging then fixed relative trigger; retained partial setup, acknowledgement/unknown execution and fresh RUNNING then stopped/arrived completion reports.
- [x] Same application axis reservation, independent producer write adapter, priority stop/cancel/recovery and input/limit configuration invalidation; no duplicate queue or conversion.
- [x] Explicit-frame CLI, strict Python one-attempt finite scenario and retained cleanup evidence; public installed-core and failure-injection/application tests.
- [ ] Independent shaft displacement, calibrated ramp/sign semantics and physical stop latency remain unmeasured. Short native positive/absolute-return motion and drive-reported moving stops now have evidence; no analyzer admission gate remains. Uncertain writes never replay.

See [the contract](ess_position.md) and [prompt09 evidence](reports/ess_release_09_2026-10-04.md). The proposed standalone +/-250-native/60-RPM ceiling is a software limit, not a physically qualified envelope. Prompt10 extends the same executor.

The [fresh prompt09 audit](reports/ess_release_09_audit_2026-10-04.md) fixes
readiness through queue/setup/TX and deferred trigger admission, reevaluates
consumed reference age at operation admission, preserves valid same-tick
Python evidence, and marks interrupted transport cleanup unknown. Help and
capability routes require the complete move callback set. Native/application,
installed-package, firmware and read-only COM13 checks are recorded separately
from the still unperformed physical motion and dynamic-stop qualification.

## Prompt10 absolute coordinates and explicit clear disposition

- [x] [Fresh audit](reports/ess_release_10_audit_2026-10-04.md): preserve reference
  age across interpretation-preserving host edits; invalidate externally changed
  position/running evidence during stop and other non-move reservations. Clear
  ACK plus corrupt readback retains conflict through release/recovery without
  replay. Native/package/build and corrected-image read-only COM13 checks PASS.

- [x] Shared exact absolute/wrapped preparation with motor/load frames, preserved
  multi-turn targets, positive/negative/shortest paths, explicit half-turn ties,
  same-angle stay, requested/rounded limits and zero-displacement rejection.
- [x] Reuse the finite mover and same-axis staging reservation; absolute trigger5,
  copied fresh command-reference provenance, existing stop/uncertainty/deadlines.
- [x] Explicit zero-only device position clear uses reviewed auxiliary49 and new
  checked current-position pair zero; nonzero counter changes reject before TX.
  Host origin remains separate, evidence-gated and generates no motor traffic.
- [x] Invalidate coordinate confidence on accepted release/clear, external
  movement, lost/expired reference and relevant settings changes; trigger drops
  live pose confidence without cancelling its admitted observation sequence.
- [x] Common/profile console and Python routes, all requested spatial units,
  strict correlation, installed public headers and independent failure tests.
- [x] Available COM13 read-only regression, pure step/degree/radian equivalence
  under explicitly ASSUMED host scale, zero-TX motion/clear/origin gates and RAM
  measurements; exact traffic/settings retained in [report](reports/ess_release_10_2026-10-04.md).
- [ ] Physical equivalent-unit motion, origin/clear effects, independent shaft
  comparison and dynamic stop: NOT RUN pending independent timing/FC06 source,
  command/feedback sign/basis/ramp/input qualification and usable physical stop.
  Linear travel remains unqualified without a configured mechanism.

Prompt11 remains separately dispatched. Unknown algorithm3/firmware0x0029 and
unsigned feedback never become qualified command-reference or ramp semantics.
