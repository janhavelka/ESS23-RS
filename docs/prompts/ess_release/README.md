# ESS release implementation sequence

The [debug refactor](../../reports/debug_refactor_2026-10-04.md) replaces the
console `sniff` spelling with `debug off|raw|decoded`, keeps one production
execution path and consolidates diagnostics/profile state. Prompts23/24 reuse
this interface and its same-session harness; no subsequent numbered prompt is
executed by this refactor.

Prepared 2026-10-03 against MotorControl-RS `4ae0c4d` (0.6.0).
Preparing this set executed no numbered prompt. **Prompts 01-27 have implementation and available verification dispositions;
28-30 remain prepared and unexecuted. Prompt23 integration, prompt24 scenarios and prompt26's available platform subset are verified while named prerequisite/native-family gaps remain open. Independent electrical and shaft-motion qualification remain open; short functional motion has drive-reported evidence.**
The current core, runner, ESP32-S3 capture and probe/load tools are existing work;
do not rebuild them from an old prompt as if they were missing.

Read [the execution contract](execution_contract.md) before every prompt.

[Regular API/sniff integration](../../reports/regular_api_sniff_2026-10-04.md)
removes the special functional image and flags, adds passive raw/decoded traffic
copies and verifies ordinary motion with observation enabled. Prompts23/24 must
reuse those paths; this work does not execute the remaining numbered prompts.

The later [short functional campaign](../../reports/functional_motion_2026-10-04.md)
adds drive-reported enable/release, positive/return motion and moving normal/direct
stop evidence to08-10. The later regular-API integration removes its special firmware mode;
functional evidence does not qualify calibrated units, exact feedback or electrical timing. Historical
NOT RUN dispositions below remain historical; several-hour testing was omitted.
The [roadmap](../../roadmap.md), [backlog](../../backlog.md) and current
[architecture](../../architecture.md), [axis](../../axis_contract.md),
[profile](../../profile_contract.md), [CLI](../../cli_contract.md) and
[discovery](../../discovery_contract.md) contracts remain authoritative.
The [coverage map](coverage.md) connects those obligations to prompt owners.
The historical [preparation/source audit](../../reports/2026-10-03_promptset_audit.md)
and [follow-up source audit](../../reports/2026-10-03_promptset_reaudit.md)
retain earlier source evidence. Names and board references in those reports
predate the platform-boundary cleanup. Current paths and requirements below
supersede their implementation instructions; timing observations remain evidence.

## Dispatch

Start with **01**. Execute numbered prompts in ascending order, one at a time.
Do not parallelize numbered blocks; use subagents within the selected block
on disjoint work. Each prompt has its own bounded deliverable, negative tests,
hardware disposition, independent review and commit/push handoff.

Example dispatch:

> Execute docs/prompts/ess_release/01_bus_admission_and_results.md under its
> execution contract. Recheck current code and predecessor evidence. Complete
> its scoped implementation, tests, audit, report and commit/sync only.

Copy or refer to one full numbered file together with its shared contract.
A prompt filename alone is not evidence its prerequisites were delivered.
At each handoff update this table with actual implementation status and a
report link; record hardware PASS/FAIL/NOT RUN separately inside that report.
Never mark all preceding physical work qualified just because code compiles.

| Prompt in execution order | Roadmap stage | Implementation status | Handoff |
| --- | --- | --- | --- |
| [01 — Bounded bus admission and retained results](01_bus_admission_and_results.md) | 3 | Implemented and freshly audited; native PASS; Runner regression PASS, owner hardware NOT RUN | [Handoff](../../reports/ess_release_01_2026-10-03.md), [fresh audit](../../reports/ess_release_01_audit_2026-10-03.md) |
| [02 — Fair scheduling, cancellation and urgent work](02_bus_scheduling_and_cancellation.md) | 3 | Implemented and freshly audited; native/build PASS; owner hardware NOT RUN | [Handoff](../../reports/ess_release_02_2026-10-03.md), [fresh audit](../../reports/ess_release_02_audit_2026-10-03.md) |
| [03 — Connect the bus owner to the standalone console](03_standalone_owner_and_responsive_console.md) | 3 | Implemented and freshly audited; native/build PASS; read-only timer owner/load/console bench PASS; upper 20-ms delay fails closed before TX | [Handoff](../../reports/ess_release_03_2026-10-03.md), [fresh audit](../../reports/ess_release_03_audit_2026-10-04.md) |
| [04 — Review capture cost and qualify available timing](04_capture_cost_and_timing_qualification.md) | 2 / 8 | Implemented and freshly audited; capture retained at measured 20–21%; native/build and 7/37-byte read-only bench PASS; independent electrical timing NOT RUN | [Handoff](../../reports/ess_release_04_2026-10-04.md), [fresh audit](../../reports/ess_release_04_audit_2026-10-04.md) |
| [05 — Typed identity/configuration reads and coverage tracking](05_identity_configuration_and_coverage.md) | 4 | Implemented and freshly audited; native/package/build PASS; read-only typed COM13 PASS; model/firmware/units unresolved | [Handoff](../../reports/ess_release_05_2026-10-04.md), [fresh audit](../../reports/ess_release_05_audit_2026-10-04.md) |
| [06 — Typed state, feedback and separate health observations](06_state_feedback_and_health.md) | 4 | Implemented and freshly audited; native/build PASS; stationary read-only hardware PASS | [Handoff](../../reports/ess_release_06_2026-10-04.md), [fresh audit](../../reports/ess_release_06_audit_2026-10-04.md) |
| [07 — Exact target preparation, coordinates and limits](07_coordinates_and_target_preparation.md) | 5 / 6 | Implemented and freshly audited; native/package/build PASS; host API plus 23-frame read-only COM13 PASS; physical origins/travel/motion unqualified | [Handoff](../../reports/ess_release_07_2026-10-04.md), [fresh audit](../../reports/ess_release_07_audit_2026-10-04.md) |
| [08 — Bounded actions, enable/release and priority stop](08_operations_enable_release_and_stop.md) | 5 | Implemented and freshly audited; native/package/build PASS; read-only COM13 and zero-TX action gates PASS; physical actions NOT RUN pending timing/echo evidence | [Handoff](../../reports/ess_release_08_2026-10-04.md), [fresh audit](../../reports/ess_release_08_audit_2026-10-04.md) |
| [09 — First finite relative move and dynamic stop](09_first_relative_motion.md) | 5 | Implemented and independently re-audited; native/package/build and read-only COM13/gate checks PASS; physical movement/dynamic stop NOT RUN pending qualified timing/sign/basis/ramp/input prerequisites | [Audit](../../reports/ess_release_09_audit_2026-10-04.md), [handoff](../../reports/ess_release_09_2026-10-04.md) |
| [10 — Absolute positions, wrapped angles and coordinate changes](10_absolute_angle_and_origins.md) | 6 | Implemented and freshly audited; native/package/build and corrected-image read-only COM13/zero-TX gates PASS; physical move/clear/unit comparisons NOT RUN, linear travel unqualified | [Handoff](../../reports/ess_release_10_2026-10-04.md), [fresh audit](../../reports/ess_release_10_audit_2026-10-04.md) |
| [11 — Velocity operation and verified ramp semantics](11_velocity_and_ramp_semantics.md) | 6 | Implemented and freshly audited; native/package/build and read-only COM13/gates PASS; physical velocity/ramp/stop NOT RUN behind qualification gates | [Handoff](../../reports/ess_release_11_2026-10-04.md), [fresh audit](../../reports/ess_release_11_audit_2026-10-04.md) |
| [12 — Resolve paired-register write support before expansion](12_paired_register_write_policy.md) | 7 prerequisite | Freshly audited all 20 writable pairs; generator address consistency fixed; four-window policy retained; native/package/build and read-only COM13 PASS; new pair forms/order and physical writes remain unresolved/NOT RUN | [Handoff](../../reports/ess_release_12_2026-10-04.md), [fresh audit](../../reports/ess_release_12_audit_2026-10-04.md), [per-pair disposition](../../ess_pair_writes.md) |
| [13 — Typed driver settings and software limits](13_driver_configuration_and_limits.md) | 7 | Implemented and freshly re-audited; native/package/build and corrected-image read-only COM13/gates PASS; pair setters unsupported, physical writes/limits NOT RUN | [Handoff](../../reports/ess_release_13_2026-10-04.md), [fresh audit](../../reports/ess_release_13_audit_2026-10-04.md), [API](../../ess_driver_settings.md) |
| [14 — Homing methods and reference establishment](14_homing_and_reference_establishment.md) | 6 / 7 | Implemented33/34/35 and freshly audited; all35 method dispositions; native/package/build and corrected-image read-only gates PASS; physical homing NOT RUN | [Handoff](../../reports/ess_release_14_2026-10-04.md), [fresh audit](../../reports/ess_release_14_audit_2026-10-04.md), [API](../../ess_homing.md), [methods](../../ess_homing_methods.md) |
| [15 — Optional digital I/O and external-control configuration](15_digital_io_and_external_controls.md) | 7 | Implemented; native/package/build PASS;190-frame stored-I/O paths/13 updates and restoration PASS; external switch/load/electrical tests NOT RUN | [Handoff](../../reports/ess_release_15_2026-10-04.md), [API](../../ess_io.md) |
| [16 — Stored position/speed records and external triggers](16_stored_position_and_speed_segments.md) | 7 | Implemented with guarded pair/sign forms and freshly audited; native/package/four builds PASS; all48 indexed reads and prior scalar restoration PASS; shared-start mismatch FAIL/unresolved, triggers NOT RUN | [Handoff](../../reports/ess_release_16_2026-10-04.md), [fresh audit](../../reports/ess_release_16_audit_2026-10-04.md), [combined16/17 audit](../../reports/ess_release_16_17_audit_2026-10-04.md), [API](../../ess_segments.md) |
| [17 — Algorithm, encoder, current and lock settings](17_algorithm_encoder_current_and_lock.md) | 7 | Implemented with exact-source guards and freshly audited; native/package/four builds PASS; all8reads and lock-delay restoration PASS; mode/encoder/current effects NOT RUN | [Handoff](../../reports/ess_release_17_2026-10-04.md), [fresh audit](../../reports/ess_release_16_17_audit_2026-10-04.md), [combined17/18 audit](../../reports/ess_release_17_18_audit_2026-10-04.md), [API](../../ess_control_settings.md) |
| [18 — Filters, tracking and tuning parameters](18_filters_tracking_and_tuning.md) | 7 | Implemented and independently reviewed;45 native/package/four-build checks PASS; all20 COM13 reads and input-filter stored restoration PASS; physical effects unqualified, collision access/firmware gaps retained | [Handoff](../../reports/ess_release_18_2026-10-04.md), [fresh17/18 audit](../../reports/ess_release_17_18_audit_2026-10-04.md), [API](../../ess_tuning.md) |
| [19 — Host serial settings and adapter capability limits](19_host_serial_tuple_support.md) | 7 / 8 | Implemented and freshly audited;47 native/package/four builds PASS; all16 host setups/restoration/ten probes PASS; strict mismatch traffic FAIL/unresolved; alternate motor tuples/electrical timing unqualified | [Handoff](../../reports/ess_release_19_2026-10-04.md), [fresh audit](../../reports/ess_release_19_audit_2026-10-04.md), [API](../../host_serial.md) |
| [20 — Explicit drive communication commissioning](20_device_communication_commissioning.md) | 7 | Implemented and freshly audited;52 native/package/four builds and37 corrected-image read-only COM13 frames PASS; physical settings/activation/restoration NOT RUN pending restart/route-back fixture | [Handoff](../../reports/ess_release_20_2026-10-04.md), [fresh audit](../../reports/ess_release_20_audit_2026-10-04.md), [API](../../ess_communication.md) |
| [21 — Save, restore and persistence evidence](21_save_restore_and_persistence.md) | 7 | Implemented and independently reviewed;59 native/package/four builds and48 final-image read-only COM13 frames PASS; physical save/restart/factory restoration NOT RUN pending backup/recommissioning/restart procedure | [Handoff](../../reports/ess_release_21_2026-10-04.md), [API](../../ess_persistence.md) |
| [22 - Bounded discovery and minimal probe capabilities](22_bounded_discovery.md) | 7 | Implemented and independently audited; native/build and bounded COM13 scan/restoration PASS; collisions and alternate motor tuples unqualified | [Handoff](../../reports/ess_release_22_2026-10-05.md), [audit](../../reports/ess_release_22_audit_2026-10-05.md) |
| [23 — Complete public API and CLI coverage](23_cli_and_capability_parity.md) | 6 / 7 | Integrated and freshly independently audited; native/Python/package/build and COM13 features/finite-motion/loaded stop PASS; session restoration verified separately from archived initial-target cleanup; eight named native-family gaps remain open | [Handoff](../../reports/ess_release_23_2026-10-05.md), [fresh audit](../../reports/ess_release_23_audit_2026-10-05.md), [API/CLI coverage](../../ess_api_cli_coverage.md) |
| [24 — Consolidate automated feature and regression testing](24_python_feature_and_regression_scenarios.md) | 8 | Implemented and freshly independently audited;65 CTest suites/three required firmware builds PASS;461 implementation and322 audit COM13 frames, finite motion/stop, exact session restoration, raw proof and retained failures; broader physical/native gaps remain open | [Scenarios](../../bench_scenarios.md), [handoff/evidence](../../reports/ess_release_24_2026-10-05.md), [fresh audit](../../reports/ess_release_24_audit_2026-10-05.md) |
| [25 — Native ESP-IDF standalone consumer](25_native_esp_idf_consumer.md) | 8 | Implemented and independently reviewed; 66 CTest/three Arduino/clean native firmware/core consumer builds PASS; 58 final-image read-only COM13 frames and resources PASS; available native motion/load parity qualified in26 | [Handoff](../../reports/ess_release_25_2026-10-05.md), [build/recovery](../../esp_idf_probe.md), [Fresh25/26 audit](../../reports/ess_release_25_26_audit_2026-10-05.md) |
| [26 — Qualify platform parity and document portability](26_platform_parity_and_portability.md) | 8 | Implemented and independently reviewed;68 CTest/233 Python/three Arduino builds PASS; matched S3 capture/load/state/finite motion/both stops/settings restoration; clean S2 core/portable and installed desktop builds; retained mismatch failure and unqualified fixtures remain explicit | [Handoff/evidence](../../reports/ess_release_26_2026-10-05.md), [S2 compile fixture](../../../test/portability_consumer/README.md), [Fresh25/26 audit](../../reports/ess_release_25_26_audit_2026-10-05.md) |
| [27 — Repeatable verification, CI and clean packaging](27_repeatable_verification_and_packaging.md) | 8 | Implemented and freshly audited;72 checks, strict ZIP/install consumers and eight builds PASS; command/assertion/exit guards and local-link fixes; hardware unchanged | [Handoff](../../reports/ess_release_27_2026-10-05.md), [fresh audit](../../reports/ess_release_27_audit_2026-10-05.md) |
| [28 — Integrated architecture, code and coverage audit](28_integrated_code_and_coverage_audit.md) | 8 | Prepared | — |
| [29 — Fault, load and endurance qualification](29_fault_and_endurance_qualification.md) | 8 | Prepared | — |
| [30 — Release candidate and FieldCore integration handoff](30_release_candidate_and_fieldcore_handoff.md) | 8 | Prepared | — |

## Dependency and qualification rules

The user's 2026-10-04 instruction adds the execution contract's short unattended
functional acceptance policy. Analyzer/human shaft observation is not an admission
prerequisite for that finite free-shaft subset. Keep checked drive-report
acceptance separate from unmeasured electrical/shaft evidence, prepare all
stop/restoration commands in advance, and defer the several-hour campaign.

- 01–02 establish native bus admission/scheduling; 03 connects it to the actual
  console, load fixture and UART owner. No ESS stop or motion is implied by a
  fake urgent-request scheduling test.
- 04 selects/reviews the capture cost and obtains available electrical evidence.
  A missing analyzer does not prevent 05–07's independent read-only/pure work,
  or native development later. It does prevent claiming unmeasured timing.
  Short functional action/motion tests use the explicit unattended policy;
  independently measured timing and shaft claims remain gated by their evidence.
- 05–06 establish typed identity/configuration/state. Unknown `0x4EEA`,
  signedness, feedback/ramp units and encoder provenance remain explicit;
  a defaults helper or green motor LED does not settle them.
- 07 adds real exact preparation; 08 implements action sequencing and stop;
  09 is the first live finite move only when its prerequisites are established.
  10–11 build on those same operations rather than duplicating them.
- 12 must dispose every required paired-write uncertainty before 13–18 expose
  the corresponding setter. The four current FC10 windows are not permission
  for all paired writes. Unresolved fields stay unavailable with a reason.
- 08–09 use serial-only control when the observed drive input configuration
  permits it. They do not require external switches or rewrite I/O silently.
  If explicit input disable/reassignment is needed, hold only dependent hardware
  cases for 15, then revisit them. 14 likewise records input-dependent methods
  for 15 to revisit; serial-only operation is not held behind an absent fixture.
- 19 supports actual host tuples before 20 device communication changes and
  22 tuple-scanning discovery. 21 treats persistence explicitly; no step
  silently saves/restores a motor to make another test pass.
- CLI access and small Python feature scenarios accompany each implemented
  feature. 23–24 consolidate and verify completeness; they do not defer all
  testing or invent missing families in a single large integration step.
- 25–26 provide a real native IDF consumer and platform evidence; component
  registration alone is not a working firmware example. 27–30 provide repeatable
  verification, integrated audit, endurance evidence and a release candidate.
- Missing physical fixtures stay named open rows. Independently implementable
  code can proceed, while dependent hardware actions and release claims cannot.
  Do not silently shrink the requested ESS coverage to make a release appear done.
- If a block reveals a substantial prerequisite gap, identify its owning prompt
  and update downstream dependencies. Keep the current block's verified work
  reviewable; do not add speculative placeholders or auto-execute the next block.

## Baseline and scope

At preparation, the recorded baseline is 13 CTest suites including 44 Python
cases and four PlatformIO builds. Read-only COM13 probes and loaded timer capture
have evidence; motion is not implemented. Historical test totals are not
future acceptance criteria. Recheck the actual port/image every bench session.

MotorControl-RS serves serial motors through a common axis API and explicit
manufacturer profiles. The reusable core is framework/platform-independent.
The standalone RS485 owner, caches, timing, health and platform tasks remain
application-owned; ESP32-S3 is the current physical test platform only.

The reference workflow deliberately uses bounded requests, yielded transactions
and waits, and retained results so firmware such as FieldCore can adapt its
RS485 task using tested examples. It does not recreate FieldCore products or
other buses. Its future motor device is not implemented here or there. FieldCore
reads are reference/audit only; the final handoff edits no FieldCore files.
Other serial manufacturers wait for actual hardware/need. CANopen belongs in
its separate future library. Publication of a tag/GitHub release is outside
these implementation prompts; 30 produces a reviewable candidate and handoff.

The set follows the structure of FieldCore's
[settings-transfer prompts](../../../../FieldCore-node/docs/prompts/settings_transfer/README.md):
shared contract, serial prerequisites, actual-source audit, independent review
and evidence handoffs. It does not import FieldCore-specific verifier commands,
product settings, queue sizes, task architecture or an approval workflow.
