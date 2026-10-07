# Roadmap to a supported ESS release

[Default position limits](reports/2026-10-07_position_limits.md) combine
2,000 rpm, 200,000 command increments/s and 100..2,000 ms native ramps.
This is a conservative preparation policy, not repaired or universally qualified
motor firmware. Raw codecs retain their existing access contract.

The [pulse-rate boundary study](reports/2026-10-07_rate_boundary.md) reproduces
motor-controller reset-like loss of volatile settings both above and below
200,000 command increments/s. Ramp controls narrow the failing combination,
but do not distinguish a supply transient from internal firmware failure.
A blanket 200 kHz speed limit is not a demonstrated fix. Four hours of active
endurance testing (4,580 cases) passed the narrower <=2,000-rpm/rate/ramp
envelope; one host-file interruption remains a failed session with clean cleanup. The measured practical
endurance envelope and failed broader cases remain separate qualification rows.


[Position/firmware follow-up](reports/2026-10-07_position_completion.md) identifies
raw drive firmware0x0029 and records34 parameter cases/repeats. Persistent RUNNING
remains unresolved, including normal-stop failure; fast stop confirmed cleanup.
Fifteen short moves pass the new shared exact-endpoint completion path, twelve
without observed RUNNING. This closes that demonstrated false uncertainty,
not all feedback quantization or high-speed cases. No motor firmware update
has been established or applied.

The [manual/FAQ/forum follow-up](reference/12_ess_rs_motion_web_review.md)
identifies an unresolved 200 kHz pulse-frequency specification in the exact RS
hardware manual. Its effect on internal serial motion needs vendor clarification;
short passing moves do not establish sustained requested speed. No matching
public firmware fix was found, and the motor-side issue remains open.


[7 October overnight evidence](reports/2026-10-07_overnight_hil.md) records9,325
matrix cases and2,799 simple commands. Normal-stop budgeting is corrected.
The failed campaign's USB TX stall was captured live, a matching HWCDC ISR race
reproduced, and both platforms now share the existing IDF USB console driver.
The replacement image still needs overnight qualification; high-subdivision
completion remains open; the exact-position short-move subset is addressed above. Historical failed runs
below retain their original disposition.


[High-subdivision completion investigation](reports/2026-10-06_completion_discrepancy.md)
reproduces a drive-reported running/arrival discrepancy at51200 subdivision,
2000 speed and128000 increments. It remains open; fast stop and the tested
1600/2000 and51200/60 alternatives work. The independent Python interleaved
reply-budget bug is fixed. No broad speed/subdivision qualification is claimed.

[One-command subdivision](reports/2026-10-06_simple_subdivision.md) adds automatic
preparatory reads and checked host scale/zero refresh, concise state output, and
delivered-success recycling for human commands. Three consecutive finite COM13
workflows pass (580 checked frames); subdivision 1600 is restored. Diagnosed
origin/setting-context failures remain recorded separately from the corrected runs.

[Subdivision/origin follow-up](reports/2026-10-06_subdivision_and_origin.md)
adds simple typed subdivision access and fixes profile restoration erasing the
boot zero. Actual subdivision changes still invalidate the old coordinate scale.

[Convenience rounding and live-speed review](reports/2026-10-06_rounding_and_live_speed.md)
adds bounded nearest-step simple moves. ESS position-command replacement while
running is documented; bare speed-write and smooth transition behavior remain
unqualified and are not exposed as a live-update API.

The [stop/resume audit](reports/2026-10-06_stop_and_resume.md) separates retained
results from current motor ownership. Move → fast stop → move and repeated
stops now pass on the ordinary firmware; failed outcomes remain inspectable.
This finite regression does not close the original overnight-stall investigation.

The [firmware health investigation](reports/2026-10-06_firmware_health.md)
adds owner-task supervision and fixes a reproduced capture-interrupt crash
during watchdog dump generation, plus native-IDF inherited USB enables.
The original overnight trigger and endurance rerun remain open; these fixes
do not turn the failed overnight campaign into a pass.

The [motion/USB follow-up](reports/2026-10-06_motion_console_and_usb.md) fixes rejection lockout, boot-session coordinates,
unit syntax and repeated completed-target handling. Its finite hardware regression
and full software verifier pass; USB root cause/endurance remain open.

The [overnight HIL experiment](reports/2026-10-05_overnight_hil.md) failed early
at 04:24 CEST on 6 October after 377 motion cases: the controller stopped
answering local console queries. Prior motor-bus errors were zero and memory
stable; final cleanup is unknown. A fresh morning local query also failed.
Console/controller diagnosis and the original endurance rerun remain open,
alongside missing external fixtures and unresolved native families.


The [canonical-command follow-up](reports/2026-10-05_fast_stop_console.md)
removes legacy aliases and duplicate dispatch paths. `stop fast` names the
existing serial emergency stop. Protocol-3 clients use the same public operations.

The [unified help follow-up](reports/2026-10-05_unified_console_help.md) replaces
separate simplified/advanced menus with one canonical grouped catalogue and
makes the serial emergency-stop policy explicit. Motor execution is unchanged.

The [settings follow-up](reports/2026-10-05_clear_motor_settings.md) consolidates
normal motor readbacks, makes subdivision visible, adds boot motion preferences
and replaces old experiment caps with reviewed positioning parameter ranges.
No broader physical speed, angle, travel or full-native qualification is implied.

The [simple motion console follow-up](reports/2026-10-05_simple_motion_console.md)
adds `moveby` / `moveto`, remembered speed/native ramps and concise outcomes.
Missing preparation and finite stopped-state polling use the same typed API and
owner. Four final-image finite moves, rejection diagnostics and restoration pass;
absolute/negative/physical measurement limits remain explicit.

The [repeated-motion timing follow-up](reports/2026-10-05_repeat_motion_timing.md)
adds explicit setup policies without changing the default. Ten trials per path
measure median start acknowledgement at 18.40 ms full setup, 18.48 ms read/compare
and 8.51 ms start-only. These software timings do not qualify shaft reaction time.

The [ESP32 C++ move example](move_example.md) adds `moveBy` / `moveTo` with units
and cached progress while the existing owner polls. Unit conversions are shared
with direct core/CLI paths; physical angle/linear calibration remains separate.
Its [verification record](reports/2026-10-05_user_move_functions.md) records
75 native checks, eight embedded builds and the restored finite bench regression.

The [optional-age/native-command follow-up](reports/2026-10-05_optional_age_and_native_position.md)
disables elapsed-age rejection by default and adds caller-owned desired native
position settings. Raw commands, setup/start acknowledgement and observed
completion remain distinct. Its new image and verification do not replace the
historical prompt30 artifact hashes or close physical/native-family gaps.

[Prompt30](release_candidate.md) delivers the unpublished0.6.0 partial candidate,
clean package and [FieldCore handoff](fieldcore_handoff.md). Its
[exact source/artifact/CI record](reports/ess_release_30_2026-10-05.md) passes
the clean full verifier,74 registered checks and eight embedded builds. Software/package
verification and functional evidence remain separate from full ESS/native
coverage, endurance and fixture qualification. Publication/integration are
separately dispatched work. Historical block summaries below retain their
original test totals; the candidate report records the current verifier.

The [fresh30 audit](reports/ess_release_30_audit_2026-10-05.md) corrects its
completed index disposition and FieldCore result-reclamation mapping, preserving
bounded motor outcomes and explicit identical-late-response ambiguity. Native
coverage and physical qualification gates remain unchanged.


The [29 qualification matrix](reports/ess_release_29_2026-10-05.md) records finite
Arduino/native-IDF S3 load/fault/feature evidence and explicit per-frame limits.
An initial stale-identity settings refusal remains a failed aggregate run;
the zero-TX cause was reproduced and the prerequisite correction verified.
The full74-check/eight-build verifier passes. Multi-hour endurance, physical
fault fixtures, independent electrical/shaft measurements and the nine named
native-family gaps remain open. Prompt30 prepares an unpublished partial candidate and current-source FieldCore handoff; see [candidate disposition](release_candidate.md).

The [integrated28 audit](reports/ess_release_28_2026-10-05.md) corrects immediate
configuration invalidation, separates ESS discovery evidence from common
headers, restores the umbrella velocity API and reconciles coverage roles.
The full verifier passes74 checks, strict source/install consumers with28 public
headers and eight firmware/core builds. Nine named native-family gaps remain;
prompt29's available qualification is recorded above;30 records candidate and handoff dispositions; full-release gates stay open.
The107-frame final-image COM13 regression passes grouped configuration reads,
discovery, reversible lock-delay and finite relative motion with stop/profile
restoration; physical fault injection, electrical/shaft and endurance proof remain open.

The [human console](console.md) now provides grouped help, examples and readable
operation/traffic output on the existing application path. `@ID` automation keeps
JSONL and per-operation correlation. [Verification](reports/human_console_2026-10-05.md)
covers native output pressure and a short read-only bench regression; motion and
electrical qualification dispositions remain unchanged.

The [fresh27 audit](reports/ess_release_27_audit_2026-10-05.md) tightens actual
test command/mode identity, assertion and exit-status guards, exported archive
consumption and maintained Markdown links. Qualified verifier compilers are
explicit; runtime and hardware dispositions remain unchanged.

Prompt27 supplies [repeatable quick/full verification](verification.md), hosted
compiler/firmware CI and clean static source/install packages.72 registered
checks, strict C++11/C++17 noRTTI consumers, all27 isolated public headers and
eight firmware/core/portable builds pass locally. The
[handoff](reports/ess_release_27_2026-10-05.md) retains deliberate missing-check
and early implementation failures; no hardware or endurance claim is added.
Hosted GCC/Clang/Arduino/IDF CI also passes; first-run defects and corrections
are retained. Prompt28's corrective audit is recorded above;29–30 remain separate.

The [fresh25/26 audit](reports/ess_release_25_26_audit_2026-10-05.md) rejects
forced GPTimer debug logging on the native correlated console, tests ordinary
reset during uncertain physical TX with blocked output on both frameworks,
and repeats clean firmware/core/S2/package and matched COM13 qualification.
69 native suites pass. Console input-discard counters do not measure USB RX
ring overflow; that qualification and earlier fixture/endurance gaps stay open.

Prompt26 [platform qualification](reports/ess_release_26_2026-10-05.md) verifies
matched Arduino/native IDF S3 read/state, loaded7/37-byte capture, finite motion,
normal/direct stop and reversible settings restoration.68 native suites and
233 Python probe cases pass; installed desktop/all27-header isolation and clean
S2 core/portable consumers establish compile-only portability. COM13 ends on
the documented unloaded Arduino image with fresh standstill. The retained
malformed native host-mismatch attempt, other fixture/native-family gaps,
electrical/shaft measurements and endurance remain open for release review.

Prompt25 delivers the [native ESP-IDF standalone consumer](esp_idf_probe.md),
shared complete application logic, clean firmware/core-component builds and
read-only COM13 evidence. [Verification](reports/ess_release_25_2026-10-05.md)
passes 66 native suites and records 58 final-image frames/resources; native
motion/load parity is recorded in26; endurance/release evidence remains open.

The [fresh prompt24 audit](reports/ess_release_24_audit_2026-10-05.md) closes
raw-probe proof and session/startup failure reporting gaps.65 CTest suites and
three firmware builds pass;322 additional COM13 frames include finite motion,
moving stop and exact parameter restoration. Broader qualification is unchanged.

Prompt24 implements [named finite scenarios](bench_scenarios.md), one strict
serial/session owner, capped incremental evidence and explicit cleanup.
[Verification](reports/ess_release_24_2026-10-05.md) passes65 native suites and
461 new checked COM13 frames, including current-session profile restoration,
finite motion/stop and corrected filter restoration. IDF/parity and endurance
remain later gates; earlier native-family gaps remain in the denominator.

Prompt23 reconciles [installed API and CLI coverage](ess_api_cli_coverage.md), local target selection, retained host controls and capability dispositions. [Verification](reports/ess_release_23_2026-10-05.md) and the [fresh audit](reports/ess_release_23_audit_2026-10-05.md) separate integration from eight named prerequisite gaps, including arbitrary archived profile staging. The audit preserves uncertain restoration evidence, adds explicit local wiring declarations and closes ownership/reporting defects. Prompts24–26 inherit the actual command/result contract; full native-family and release completion remain open.

The [fresh prompt22 audit](reports/ess_release_22_audit_2026-10-05.md) verifies
bounded ESS discovery, cancels scan continuations on explicit recovery and
strengthens Python failure-evidence checks. Physical collision exclusion and
alternate motor tuples remain unqualified.

Prompt21 delivers [explicit save/factory restore](ess_persistence.md) with
bounded retained outcomes, before-values, field-level uncertainty and reuse of
the communication commissioning lease. Software and read-only COM13 evidence
PASS; actual nonvolatile durability, restart activation and factory restoration
remain NOT RUN without backup/recommissioning and motor restart capability.
See the [handoff](reports/ess_release_21_2026-10-04.md).

The [debug refactor](reports/debug_refactor_2026-10-04.md) consolidates runtime
observation and profile state around the ordinary execution path. Debug changes
no motor prerequisites or wire policy; software timing remains distinct from
electrical measurements. See the [debug workflow](traffic.md).

The [regular API and sniff integration](reports/regular_api_sniff_2026-10-04.md)
removes the functional-test image and flags. Standard firmware performs the
reviewed actions and finite motion; passive raw/decoded traffic display runs
alongside them with bounded diagnostic storage and loss counters. Wiring is the
user's responsibility; command sequencing and software timing remain firmware
responsibilities. HIL diagnostics do not gate ordinary operation.

The [short unattended functional campaign](reports/functional_motion_2026-10-04.md)
now records drive-reported positive/reverse finite motion, enable/release and both
normal/direct stops during motion. Final-image310/310 frames passed, settings
were restored and zero speed/no alarm reported. Functional acceptance no longer
requires an analyzer or a person at the bench. Electrical/independent shaft
qualification and calibrated feedback remain open; the hours-long soak was
explicitly omitted. This does not qualify communication activation/restart.

The [fresh prompt20 audit](reports/ess_release_20_audit_2026-10-04.md) fixes
unchanged-host confirmation retention and strict CLI/Python evidence correlation.
All52 native suites, installed consumption, four firmware builds and37 corrected-
image read-only frames pass. Physical commissioning remains NOT RUN.

Prompt20 implements [drive communication commissioning](ess_communication.md)
with retained candidate contexts, exclusive owner admission and explicit bounded
confirmation/restoration. All 52 native suites, installed consumption and four
firmware builds pass; the final image passes 37 read-only COM13 frames and
zero-TX qualification gates. Physical changes/activation/restoration remain
NOT RUN without an actual motor restart and qualified route back. The
[handoff](reports/ess_release_20_2026-10-04.md) assigns explicit save-dependent
activation to prompt21 and preserves prompt19's unresolved mismatch evidence.

Prompt19 delivers [host serial selection](host_serial.md), exclusive failure
and restoration handling, per-request tuple evidence and independent logical
generations. Native/package/four-build checks and available COM13 host setup,
restoration and read-only regression PASS. The [fresh audit](reports/ess_release_19_audit_2026-10-04.md)
fixes cross-read settings reconciliation, faithful SDK failure settlement and
Python evidence. Strict NO_RESPONSE-only checks fail on reproduced malformed
mismatch traffic; its physical source and alternate-tuple/electrical evidence
remain unresolved/unqualified. Drive settings remain unchanged.

The [fresh prompts 17/18 audit](reports/ess_release_17_18_audit_2026-10-04.md) fixes
settings freshness, copied provenance, partial-refresh invalidation and strict
console evidence validation. All 45 native suites, installed consumption and
four firmware builds pass. The final COM13 image passes 104 frames and restores
input filter `2?3?2` and lock delay `200?201?200`; physical effects remain unqualified.

Updated 2026-10-04. This is the delivery order and the definition of done.
The [backlog](backlog.md) holds individual features, register questions and
verification tasks. Update both when evidence changes the plan; dates and
future version numbers are not commitments.

The [ESS release prompt set](prompts/ess_release/README.md) turns these stages
into 30 ordered implementation blocks with tests, independent audits and
evidence handoffs. Prompts 01-30 have implementation/candidate and available verification
dispositions. [Prompt12](reports/ess_release_12_2026-10-04.md) consolidates the
four reviewed FC10 windows into generated policy and records all paired-write
proof gaps. Its [fresh audit](reports/ess_release_12_audit_2026-10-04.md) fixes
generator address consistency and repeats native/build/read-only checks.
No new physical write qualification is implied. [Prompt13](reports/ess_release_13_2026-10-04.md)
implements typed single-word settings and raw limit reads; its
[fresh audit](reports/ess_release_13_audit_2026-10-04.md) corrects response-source
settlement and axis prerequisite caches. Prompt14 adds bounded methods33/34/35. [Prompt15](reports/ess_release_15_2026-10-04.md)
adds typed optional I/O and selected stored-setting disable/readback/restoration
evidence; external physical function qualification remains open. Prompt16 adds [indexed record operations](ess_segments.md), boundary reads and PT/PV scalar restoration. Shared-start readback mismatch and physical triggering remain open in [the report](reports/ess_release_16_2026-10-04.md).
Prompt17 adds [typed control settings](ess_control_settings.md), all-field readback and reversible stored lock-delay restoration. Mode/encoder/current effects and exact current semantics remain open in [the report](reports/ess_release_17_2026-10-04.md).
Prompt18 adds [typed tuning](ess_tuning.md), twenty native reads/writes and
input-filter stored restoration through the same bounded settings engine.
Per-group observation baselines retain threshold-change invalidation. Physical
effects and collision firmware/access differences remain open in
[the report](reports/ess_release_18_2026-10-04.md).
The [fresh16/17 audit](reports/ess_release_16_17_audit_2026-10-04.md) fixes
deadline certainty/help parity and verifies all48 indexed reads plus lock-delay
restoration on the corrected image. Software passes independently of unresolved
wire forms, shared-start acceptance and physical/current qualification.
[Prompt10](reports/ess_release_10_2026-10-04.md) adds absolute/wrapped
coordinates and device zero-clear while physical comparisons remain gated.
Its [fresh audit](reports/ess_release_10_audit_2026-10-04.md) fixes host-reference
retention and external-motion invalidation, with independent reviews and another
38 read-only frames on the corrected image. Physical motion/clear remains NOT RUN.
The [relative-position handoff](reports/ess_release_09_2026-10-04.md) supplies finite staging/trigger/completion APIs; its original physical disposition predates the later bounded bench campaigns. The [action/stop handoff](reports/ess_release_08_2026-10-04.md)
has native reservations, uncertainty and priority stop, plus read-only COM13 and
zero-TX gate evidence. Those historical reports predate the subsequent live finite-motion and moving-stop
checks. The regular API now uses declared wiring and software timing checks;
independent electrical measurements remain separate.
The [fresh prompt 08 audit](reports/ess_release_08_audit_2026-10-04.md) preserves
uncertainty for undocumented exceptions, checks action evidence chronology and
adds stop HOLD/partial-TX/late-fault regressions. Native/package/build and 38-frame
read-only verification pass; the physical qualification gates remain open.
The [fresh prompt 02 audit](reports/ess_release_02_audit_2026-10-03.md) corrects
cancellation cutoff precedence and recovery evidence continuity; hardware owner
integration now has [prompt 03 evidence](reports/ess_release_03_2026-10-03.md).
The [fresh prompt 03 audit](reports/ess_release_03_audit_2026-10-04.md) corrects
cache/result/deadline edges and repeats native/build and read-only timer campaigns.
The [fresh prompt 01 audit](reports/ess_release_01_audit_2026-10-03.md) corrects
two timeout-reporting defects with native and current-image regression evidence.
The [fresh prompt 05 audit](reports/ess_release_05_audit_2026-10-04.md) verifies
the typed read implementation, corrects stale API descriptions and repeats
native/package/build and unloaded COM13 checks on the unchanged image.
Preparing a prompt does not complete its milestone.
The historical [source re-audit](reports/2026-10-03_promptset_reaudit.md) adds
explicit gates for absolute deadlines, queued work across recovery, same-axis
staging ownership, honest observation age and long-frame evidence. Historical
board names remain in evidence reports; current adapters/prompts use platform
names and no unrelated product/bus requirements.

## Release scope

MotorControl-RS is a framework/platform-independent library for RS485 motors
from selected manufacturers. The first supported profile targets the identified
ESS-RS model/firmware. A standalone application on the available ESP32-S3 bench
qualifies that profile through the same public API that firmware consumers and
the CLI call. Raw register access alone does not complete the motor API.

The standalone RS485 workflow is a working reference for future integrations:
profiles yield transaction/wait work, one application bus owner schedules it,
and checked observations/results return to the caller. FieldCore can later use
that reference to extend its RS485 task and add a new motor device. No FieldCore
motor device or product wiring is assumed today; unrelated buses/products are
outside this library. Board pins, USB and memory placement stay in examples.

External motor I/O is optional. Support serial-only operation with known unwired
inputs and explicit documented no-function assignments. An unwired terminal is
not automatically disabled; only operations that actually need an external input
require it. Prompt15 implements explicit ESS I/O disable/configuration operations.

Other serial profile implementations wait for an actual motor and a concrete
need; the common API is reviewed against contrasting documented families.
CANopen remains a separate future library. FieldCore stays read-only during
this session; integrating there is a separate delivery step.

## Milestones and evidence

| Stage | Deliverable | Gate before calling it complete | Current state |
| --- | --- | --- | --- |
| 1. Protocol foundation | Units, complete ESS register ledger, checked FC03/06/10 codecs and non-changing model probe | Native validation, generated-ledger checks, explicit unresolved fields and access limits | Implemented; checked model reads on the bench |
| 2. Observable transport | Runner, independent capture, read-only CLI, load and failure tools | Delayed servicing/overflow/late-reply tests; measured load, memory and timing; no automatic replay | Implemented in 0.6.0; measured ESP32-S3 bench load envelope; electrical timing qualification remains open |
| 3. Small bus-owner reference | Bounded request queue, retained results, fairness, deadlines and priority for a pending stop after settling in-flight TX | Multiple simulated clients, full queue, cancellation and starvation tests; read-only hardware regression under load | Prompts 01–03 implemented/native/build PASS; actual owner console and read-only loaded/interleaved timer bench PASS; later26/29 functional stop evidence; electrical qualification open |
| 4. Typed ESS observations | Identity, firmware/configuration, alarms, readiness and position/velocity observations | Original-manual review; exact model readback; validity/freshness independent of communication health | Prompts 05–06 implement typed reads and separate health; bench SKU is user-confirmed ESS23-RS20;26/29 qualify a finite positive native-motion subset. Universal wire-code/firmware interpretation and full feedback source/sign/calibration remain unresolved |
| 5. First controlled motion | Explicit enable/release, small relative move and documented stop | Verified units and limits, acknowledgement vs completion, interrupted/lost replies, stop while another operation is active; bench measurements | Exact preparation and action/finite sequence software pass; short positive relative motion and normal/direct stops functionally qualify on both S3 frameworks in26; independent shaft/electrical, negative encoding and other fixture cases remain open |
| 6. Complete common motion API | Absolute/relative position, step/angle/travel modes, velocity, acceleration, homing and fault clear where supported | Same public API in firmware and CLI; unsupported operations fail before TX; origin/rounding/path policy tested | 07–11 implement exact preparation, origins, finite relative/absolute/wrapped positioning, zero clear and finite serial velocity/shared stop; bounded homing33/34/35 implemented in14; acceleration mapping, external-switch methods and physical qualification remain open |
| 7. ESS native coverage and discovery | Typed documented ESS extensions; bounded non-changing discovery | Coverage matrix for every documented command/field; uncertain firmware behavior marked explicitly; read side effects reviewed | Ledger/codecs and typed identity/config reads exist; complete operation inventory derives remaining obligations; bounded ESS discovery implemented; remaining native families and alternate motor tuple/collision qualification tracked separately |
| 8. Platform and release qualification | Arduino and native ESP-IDF examples, package/install checks, maintenance documentation | Clean consumer builds; repeatable hardware suite, extended soak, resource budgets, documented exact supported models and limitations | Shared Arduino/IDF applications; available S3 capture/load/finite motion/stop/settings parity PASS in26/29; S2 core/portable compile-only and installed core PASS.30 delivers a verified unpublished partial candidate and FieldCore handoff; full native coverage, electrical/fixture/endurance gates and release publication remain open |

Stages 3 and 4 can progress while independent electrical captures are being
arranged. Motion qualification must use reviewed transport evidence and the
actual drive configuration. No stage silently promotes a software test into
a hardware claim.

## What every implementation block includes

1. Implement one usable behavior with simple bounded ownership. Reuse the
   existing codecs, runner, capture and console rather than copying FieldCore.
2. Add tests for meaningful error and interruption paths, including uncertain
   execution for writes. Keep command acknowledgement separate from motion.
3. Run a short COM13 hardware regression whenever the feature reaches the
   board. Add a direct feature test where applicable. Record unavailable or
   unperformed tests; do not silently substitute native tests for them.
4. Retain latency, failures, service gaps, internal RAM, PSRAM and stack
   observations when resource use or scheduling changes. Longer stress runs
   are separate qualification work, not required after every documentation edit.
5. Audit the changed code and affected contracts, fix stale documentation,
   update this roadmap/backlog, then commit and push the tested block.

## Remaining release gates

- Bench model ESS23-RS20 and nominal encoder specifications are established
  ([sources](reference/11_ess23_rs20_identity.md)); resolve firmware/algorithm3,
  command/feedback semantics and a universal mapping of raw model `0x4EEA`.
  Physical direction/accuracy remain qualification work.
- Capture independent TX/RX/DE waveforms. Validate timestamp bounds, final stop
  bit/DE hold, receiver turn-on, short turnaround and framing under load.
- Establish an interrupt/cache-off policy. The current stock timer build is
  not qualified through flash writes. Measure whether its CPU cost is suitable
  for the eventual application before treating it as a production selection.
- Test stop/interruption and uncertain write execution without automatic replay.
  Cover drive alarms, loss of communication and stale feedback separately.
- Account for every documented ESS native operation as implemented and tested,
  firmware-dependent, unavailable on the selected model, or explicitly excluded
  with a reason. An undocumented register gap is not a missing implementation.
- Run an extended soak and disconnect/power-cycle campaign after functional
  behavior stabilizes. Quick regressions do not replace this work.
- Check public API stability, examples, package metadata, source provenance and
  release notes. State exactly what was qualified; do not claim universal motor
  support or industrial certification.

Prompt 04 is disposed in [the capture review](reports/ess_release_04_2026-10-04.md):
retain 20-us capture at measured 20–21% core cost, enforce starvation failure,
and extend checked read-only evidence to the fixed 37-byte reply. Independent
electrical timing remains NOT RUN; cache-off capture is unsupported. Independent
05–07 read-only/pure work is now implemented; dependent physical actions retain
their timing, settings, units and stop prerequisites.
Its [fresh audit](reports/ess_release_04_audit_2026-10-04.md) closes a host
diagnostic-validation gap; the unchanged image passes repeated read-only tests.

Prompt06 delivers [typed state, separate health and finite polling](reports/ess_release_06_2026-10-04.md). Stationary state reads pass; algorithm3, position source, signed encoding/physical units, motion completion and electrical timing remain unresolved.

The [fresh06 audit](reports/ess_release_06_audit_2026-10-04.md) verifies current code and fixes feedback configuration confidence and strict cached-state validation. No moving operation or settings write was added.

[Prompt07](reports/ess_release_07_2026-10-04.md) delivers exact target preparation,
reference/limit checks and host-only configuration through the installed API
and console. Software and read-only COM13 checks pass; native origin evidence
is unresolved on this drive, and no motion, drive setting or travel qualification
is implied by that historical report. Subsequent action/motion dispositions are
recorded in prompts08–26.

The [fresh07 audit](reports/ess_release_07_audit_2026-10-04.md) corrects numerical
and exact-parser boundary defects and repeats software/package/build and read-only
host API checks. Requested relative displacement and endpoint limits remain
independent; zero radians retain exact integer references without approximation.

Prompt09 [relative-position contract](ess_position.md) and [handoff](reports/ess_release_09_2026-10-04.md) implement the staging/trigger/observation chain with retained uncertainty and same-axis stop interruption. Prompts23–26 add bounded drive-reported motion and dynamic stop evidence; independent electrical/shaft measurements and other motion subsets remain unqualified.

The [independent prompt09 audit](reports/ess_release_09_audit_2026-10-04.md)
also enforces readiness during queued/deferred writes and current-time reference
validation; Python interruption and correlation checks preserve unknown cleanup.
Physical positive/negative moves and dynamic stop remain unqualified.

[Prompt10](reports/ess_release_10_2026-10-04.md) implements preserved multi-turn
absolute targets, explicit wrapped paths, shared finite staging/trigger and
zero-only device clear, with reference invalidation and native/CLI parity.
Software/package/firmware checks and current read-only COM13 regression pass.
Equivalent-unit physical motion and independent shaft/origin/clear proof remain
NOT RUN; linear travel requires a real configured mechanism. Prompt 11 retained
the unresolved ramp/sign/model prerequisites described below.

Prompt 11 implements [finite signed velocity](ess_velocity.md) using shared
exact preparation, native ramp snapshots and the existing priority stop.
Its [fresh audit](reports/ess_release_11_audit_2026-10-04.md) fixes strict host
evidence and safe idle-interruption cleanup, and checks actual C++ console
outcomes against Python. Native/package/firmware and read-only COM13 checks
pass; exact firmware speed/ramp semantics and physical stopping remain NOT RUN.
Prompt 12 [reviews every writable pair](ess_pair_writes.md) and generates the
four-window FC10 policy from the same ledger. No new span or typed setter was
justified; home-order, limits/stored-record forms and physical writes remain
open. Native/package/build and read-only regression pass.

Prompt13 implements [typed driver settings](ess_driver_settings.md), checked
ACK/readback/partial progress and shared settings-effect invalidation. Native,
installed package, firmware and read-only COM13 checks pass; actual settings
changes/restoration and software-limit movement remain NOT RUN. Pair setters
stay guarded. Prompt14 now implements the bounded33/34/35 subset; prompt15 remains separately dispatched.

Prompt14 [homing delivery](reports/ess_release_14_2026-10-04.md) reuses checked staging/trigger and priority stop with fresh transition/zero proof, explicit35-method coverage and conservative reference age. Native/package/firmware and current-image read-only/gate checks are separate from physical homing qualification, which remains NOT RUN. External input methods await15 and actual fixtures; collision and nonzero offset semantics remain unresolved.

Prompt14 [fresh audit](reports/ess_release_14_audit_2026-10-04.md) corrects historical feedback invalidation and host outcome checks, with native/package/firmware and corrected-image read-only/gate PASS. Physical homing prerequisites remain open; prompt15 remains separately dispatched.

- 6 October overnight finding: [normal-stop budget corrected](reports/2026-10-06_normal_stop_budget.md), with500/2000ms hardware comparisons and bounded/no-replay regressions. This does not close the ongoing endurance or high-subdivision profile discrepancy.
