# Motor bench and testing authorization

Latest [default-position boundary follow-up](reports/2026-10-07_position_limits.md)
passed408 matrix cases plus the ordinary relative/absolute/rejection regression.
The installed Arduino image now enforces combined RPM/rate/ramp defaults; hash
and full evidence are in that report. Final subdivision51200, profile
`[30,500,500,60,0,32000]`, raw position240971966, speed0/motion1/alarm0.
Load/monitor/debug off, no queued/reserved/retained operation, UART/DE settled,
COM13 free. Motor firmware remains0x0029; no persistence writes.

Earlier [rate-boundary/endurance experiment](reports/2026-10-07_rate_boundary.md)
completed 4h08s active testing and 4,580 cases in the stated <=2,000-rpm,
<=200,000-increment/s ramp envelope. Broader high-speed tests reproduced four
motor reset-like failures, including two below 200 kHz; their cause remains open.
Final restoration: subdivision 51,200; profile `[30,500,500,60,0,32000]`;
raw position 216,080,428, speed0, motion1, alarm0. Monitor/load/debug off,
UART/DE settled, no queued/autonomous motion, COM13 free. Same ESP32 image and
motor firmware0x0029; no persistence writes. Full failed and passing evidence
and restored settings are in the report.


**Latest, 7 October overnight follow-up:**
[Full disposition and final image](reports/2026-10-07_overnight_hil.md).
The overnight campaign failed on a captured HWCDC transmit stall after9,325
matrix cases. Both platforms now share the existing IDF USB console driver;
10,000-query/motion/stop and loaded reopen regressions pass. COM13 is closed,
no queued/autonomous motion, fresh speed0/alarm0/nonrunning, load/debug off.
Subdivision51200; native profile500/500ms,60rpm,target32000. Host preferences
60rpm/100/100ms are pending for the next move. These are recorded ending values,
not a claim that every original overnight parameter was restored. New RAM
session zero, no NVS coordinates. Replacement-driver overnight qualification
and high-speed/high-subdivision completion remain open.


**Latest, 6 October completion investigation:**
[Retained failures and finite comparisons](reports/2026-10-06_completion_discrepancy.md)
confirm fast-stop cleanup. Subdivision restored to the user's51200, new RAM
zero raw728310; speed/alarm0, non-running/enabled, no queued motion, COM13 closed.
51200/2000-rpm completion and normal-stop behavior remain unresolved;1600/2000
and51200/60 finite cases passed. No new firmware was flashed. Earlier snapshots
below are historical.

**Latest, 6 October one-command subdivision follow-up:**
[workflow and evidence](reports/2026-10-06_simple_subdivision.md) pass three
consecutive finite campaigns: 36 ordinary state reads, 1600/51200/1600 subdivision
changes, small moves/returns and stop. Subdivision is restored to the user's 1600;
RAM-only zero is raw62346, assumed host scale1600. Speed/alarm0, enabled/nonrunning,
no queued/autonomous motion, DE low, load/monitor/debug off, COM13 closed.
Earlier snapshots below are historical. Failed diagnostic campaigns and the
unexecuted staged-target disposition are preserved in the report.


**Latest, 6 October subdivision/origin follow-up:** the
[corrected image and retained evidence](reports/2026-10-06_subdivision_and_origin.md)
pass absolute moves after profile restoration and subdivision 1000/1600/1000
write/readback. Original settings are restored. After a controller reset, the
RAM-only boot zero is confirmed at raw 62060; `moveto 0 deg` sends no motion.
Speed/alarm zero, enabled/nonrunning, DE released, owner/output queues empty,
load/monitor/debug off, COM13 closed. Earlier snapshots below are historical.

**6 October rounding follow-up:** [nearest-step regression](reports/2026-10-06_rounding_and_live_speed.md)
passes on the installed normal Arduino image:100deg→278increments, rounded-zero
no-op, return, explicit stop and original profile restoration. Final raw62060,
speed/alarm0, nonrunning/enabled, no pending owner/output work, DE released,
load/monitor/debug off, COM13 closed. In-motion speed replacement was not tested.
Earlier final-state snapshots below are historical.

**Current state after the 6 October stop/resume audit:**
[ordinary move → fast stop → move](reports/2026-10-06_stop_and_resume.md) and
repeated stops passed. The normal Arduino image is installed; after explicit
controller reset, a read-only check established RAM-only zero at raw 61810.
Speed/alarm zero, enabled/nonrunning, DE released, owner/output queues empty,
load/monitor/debug off, COM13 released. Earlier snapshots below are historical.

**Current state, 6 October after the firmware-health checks:** the
[health follow-up](reports/2026-10-06_firmware_health.md) restores the normal
Arduino image with owner supervision and the capture crash-path fix. Final
read-only verification establishes RAM-only zero at raw position 61430;
speed/alarm zero, enabled/nonrunning, DE released, owner/output queues empty,
load/monitor/debug off, COM13 released. `moveto 0 deg` was a no-op. The original
overnight trigger remains unresolved. The following earlier state is historical.

The [motion/USB follow-up](reports/2026-10-06_motion_console_and_usb.md) flashes the corrected Arduino image and verifies four
quarter-turn commands, return to RAM-only boot zero, repeated-target no-op,
stop and exact profile restoration. The subsequent 100,000-query USB test and
explicit reopen passed; the original USB root cause remains open. After a final
controller reset, RAM-only zero is established at raw position50887, speed/alarm0,
nonrunning/enabled, DE released and owner empty. `moveto 0 deg` was a no-op;
load/monitor/debug are off and COM13 is released. Host defaults are60rpm/100ms ramps.

**Historical fault state, 6 October 09:54 CEST (user subsequently reset the board):** the
[overnight HIL runner](reports/2026-10-05_overnight_hil.md) ended early at 04:24
after 377 motion cases because a local USB-console `stats` query received no
reply. PID 36772 has exited. COM13 still enumerates but also failed a fresh
read-only `version` query this morning. No reset or new move was sent; root
cause and final cleanup remain unresolved. Last recorded drive state was
nonrunning, speed/alarm zero, raw position 49890, DE released and no pending
owner work. These are historical values; the user independently reports the
motor stationary this morning. Do not treat the failed overnight aggregate
as PASS. Its raw evidence is preserved; the user reset the controller before subsequent diagnosis.

The overnight request authorized extended finite tests previously deferred,
without adding switches, restart control or shaft/electrical measurement fixtures.


The [consolidated settings follow-up](reports/2026-10-05_clear_motor_settings.md)
leaves the new Arduino timer image on COM13 after four completed finite moves,
including cold boot defaults, 90 rpm and 300 increments. Direct stop and exact
profile restoration pass. Final raw position 10465, speed/alarm zero, enabled and
not running; profile `[30,100,100,60,0,100]`. Host defaults are 60 rpm/100 ms ramps
and an explicitly ASSUMED 1000 command steps/turn. Load/monitor/debug off, DE
released, no pending/retained owner work or recovery requirement. Readback
subdivision 1000 is displayed separately from the host scale. Wider accepted
parameter ranges do not establish maximum-speed or physical angle qualification.

The [simple console follow-up](reports/2026-10-05_simple_motion_console.md) leaves
its final Arduino timer image on COM13 after four completed finite moves and
an explicit stop. Final raw position 9866, speed/alarm zero, enabled/non-running;
actual saved profile `[30,100,100,60,0,100]` restored, host command scale unknown.
Node 1/115200/8N1, load/monitor/debug off, DE released, owner/results empty and no
recovery or queued motion. The initial child-correlation checker failure, its
one completed move and separately recorded cleanup remain preserved.

The [repeated-motion comparison](reports/2026-10-05_repeat_motion_timing.md)
leaves the new Arduino timer image on COM13 after 33 completed finite moves,
direct stop and exact profile restoration. Final position 9265, alarm 0,
enabled/non-running, speed 0; profile `[30,100,100,60,0,250]`, node 1/115200/8N1.
No queued motion, recovery or retained results; load/monitor/debug off and DE
released. The first campaign stopped after two completed moves because a single
post-arrival speed sample was nonzero; its failure and successful cleanup remain
separate from the corrected campaign using bounded zero-speed observation.

The [unit-aware move follow-up](reports/2026-10-05_user_move_functions.md)
uploads the new Arduino timer image and passes a finite degree-to-native move,
normal-stop interruption and exact profile/host-scale restoration. Ending
position 5764, alarm 0, enabled/non-running, speed 0; node 1/115200/8N1,
profile `[30,100,100,60,0,250]`. Load/monitor/debug off, DE released, owner/results
empty, no recovery or queued motion. Its 105 checked frames establish functional
feedback only; independent angle, travel and electrical timing remain unmeasured.

The [optional-age/native-command follow-up](reports/2026-10-05_optional_age_and_native_position.md)
uploads the new Arduino timer image (SHA256 in that report). A corrected finite
campaign passes a move with31-second-old observations, normal/direct stops and
profile restoration. Ending position5457/alarm0/non-running/speed0, enabled,
profile `[30,100,100,60,0,250]`, node1/1152008N1. Debug/load/monitor off, DE released,
owner/results empty, no recovery. The initial host-parser failure and explicit
cleanup remain separate evidence; no uncertain move was replayed.

[Prompt30's fresh audit](reports/ess_release_30_audit_2026-10-05.md) adds39 checked
read-only frames with no new errors, writes or uploads. Ending profile
`[30,100,100,60,0,250]`, lock delay200, position4881/alarm0/non-running/speed0;
node1/1152008N1, DE released, empty pending/retained/reserved storage and no
recovery. Load/monitor/debug remain off. These checks confirm the reported
ending state, without promoting physical/endurance gaps to PASS.

[Prompt30's final read-only inspection](reports/ess_release_30_2026-10-05.md)
leaves the existing Arduino timer image unchanged. Fresh profile
`[30,100,100,60,0,250]`, lock delay200, position4881/alarm0/non-running/speed0;
node1/1152008N1, debug/load/monitor off, DE released, no recovery and empty
pending/retained/reserved storage. No motor writes or uploads occurred in30.
Ten quick probes and the corrected final inspection pass; two failed local
inspection attempts remain archived. Console version is not image-hash
attestation. Physical/endurance gaps below remain open.

The [prompt29 qualification](reports/ess_release_29_2026-10-05.md) restores the
same Arduino timer SHA-256
`8de8f7198d0cccd44ebf5a3ddd0c0a4801fe6bb82d998c23872e42e4500c1c4f`
after matched Arduino/native-IDF load and finite feature campaigns. All1,200
planned load reads pass;1,760 checked frames include correction/restoration
regressions. Each image's deliberate20ms owner-delay probe fails closed with
zero TX and explicit recovery; the initial Arduino settings refusal remains a
failed aggregate and was reproduced/corrected by refreshing stale identity.
Final fresh readback: raw position4881, alarm0, enabled/non-running, speed0;
profile `[30,100,100,60,0,250]`, lock delay200, node1/1152008N1. Debug/load/monitor
off, DE released, owner/results empty and no recovery required. The restored
image's33-frame read-only regression has zero errors. Motor power was not
interrupted; independent shaft/electrical, physical fault fixtures and multi-hour
endurance remain unqualified/deferred. Earlier entries are historical endings.

The [prompt28 audit](reports/ess_release_28_2026-10-05.md) leaves COM13 on Arduino
timer SHA-256 `8de8f7198d0cccd44ebf5a3ddd0c0a4801fe6bb82d998c23872e42e4500c1c4f`.
Its107-frame campaign passes configuration/discovery, lock-delay restoration and
one finite100-increment move with direct stop and exact profile restoration.
Final checked position3738, alarm0/non-running/speed0; profile
`[30,100,100,60,0,250]`, lock delay200, node1/1152008N1. Debug/load/monitor off,
DE released, owner/results empty, no recovery. Initial occupied-port/full-result
refusals remain archived; the five remaining settled results were archived and
released before a fresh baseline and upload. Independent electrical/shaft and
physical fault injection remain unmeasured. Earlier entries describe dated
campaign endings.

The [human-console update](reports/human_console_2026-10-05.md) leaves COM13 on
Arduino timer firmware SHA-256 `e5989e3b2f6a4afe51e97c8d5628bdeaf0daf6e88538a924594a75f4e31ffab3`.
Its read-only regression retains raw position3644, speed0/alarm0 and unchanged
drive settings; debug/load/monitor off, DE released, no retained results or
required recovery. No motion or setting write was performed in this block.

The [fresh25/26 audit](reports/ess_release_25_26_audit_2026-10-05.md) repeats
matched loaded capture, finite motion, both stops, native settings restoration
and explicit stopped-state host mismatch/recovery. COM13 ends on the Arduino
timer image below, node1/1152008N1, raw position3644, fresh alarm0/runningfalse/
speed0. Profile`[30,100,100,60,0,250]` and lock delay200 restored; load/debug/
monitor off, DE released, owner/results empty, no recovery. The final native
image also passes before Arduino restoration. USB RX overflow, independent
shaft/electrical measurements, endurance and unavailable fixtures remain open.

[Prompt26 platform qualification](reports/ess_release_26_2026-10-05.md) passes
matched Arduino/native IDF S3 loaded capture, finite motion, both stops and
reversible settings restoration. COM13 now runs Arduino timer firmware SHA-256
`4f6d5bc82af7ade90689be5d2405c0bba22e69bbded3c364e7ca7072afadbb11`, node1/115200
8N1. Fresh alarm0/motion0/speed0, raw position2870; profile
`[30,100,100,60,0,250]` and lock delay200 restored. Load/debug/monitor off,
DE released, owner/results empty, no recovery required. Native IDF ended equally
settled before Arduino upload. The original malformed native host-mismatch
attempt remains failed/unresolved, with separately verified explicit repair.
Independent shaft/electrical measurements and unavailable fixtures remain open.

[Prompt25 native ESP-IDF evidence](reports/ess_release_25_2026-10-05.md) records
58 checked read-only frames on its final image, exact unchanged raw identity,
configuration/state and zero transport/capture faults. First startup framing
failure was diagnosed and corrected; its failed raw evidence is retained.
At the prompt25 ending the native image was installed; alarm0/motion0/speed0, raw position1961,
load/debug/monitor off, DE released, owner/results empty. The preceding Arduino
full-flash backup is preserved. Prompt26 above records later motion/load parity.

The [fresh prompt24 audit](reports/ess_release_24_audit_2026-10-05.md) adds322
checked frames, bounded load, finite100-increment motion and a normal stop during
finite250-increment motion. Both restore `[30,100,100,60,0,250]`; final checked
alarm0/motion0/speed0, raw position1961, DE released and owner/results empty.
Independent shaft/electrical measurements and endurance remain unqualified.

[Prompt24 checks](reports/ess_release_24_2026-10-05.md) add461 checked frames on
the unchanged ordinary timer image, finite relative motion and normal moving
stop with exact current-session profile restoration. Filter2→3→2 restoration
passes after fixing harness prerequisite refresh/order; every failed attempt
and explicit known-value repair is retained. Final alarm0/motion0/speed0, raw
position1654; host1152008N1, original/current profile `[30,100,100,60,0,250]`,
debug/load/monitor off, DE released and owner/results empty. No independent
physical/electrical or endurance qualification is claimed.

The [fresh prompt23 audit](reports/ess_release_23_audit_2026-10-05.md) passes
229 checked frames on its final image, finite relative motion and normal stop
during finite motion with bounded task/console load. Local wiring declarations
and current-session profile restoration pass; final alarm0/motion0/speed0 reports
enabled standstill at raw position1367. Final staged target250 is distinguished
from archived prior targets100/5000: a failed harness attempt closed before
restoration and lost its volatile backup. No arbitrary archive import or motion
replay was used for cleanup. Drive configuration/I/O assignments remain unchanged;
load/debug/monitor off, host1152008N1, DE released and owner/results empty.
Independent physical/electrical measurements and endurance remain unmeasured.

The [prompt23 checks](reports/ess_release_23_2026-10-05.md) verify typed aliases, local selection/snapshot/polling controls, finite relative motion, enable/release and moving normal stop on the ordinary firmware. Absolute return passed on the earlier integration image; its later fixture-window refusal is retained separately. Session profile restoration passed, but USB reconnect lost the original host snapshot: the final staged target is100 rather than the initial5000; ramp/speed/flags are unchanged and the drive reports stopped. Final-image read-only regression passes. Independent shaft/electrical measurements and endurance remain separate. The dated entries below retain their original scope.

The [fresh prompt22 audit](reports/ess_release_22_audit_2026-10-05.md) repeats
bounded read-only scans, host restoration and ten probes on the corrected image.
Explicit recovery interrupts a scan without resuming it; motor settings and
stationary state remain unchanged. Collision/alternate-motor-tuple proof remains open.

Prompt22 adds [bounded ESS discovery](ess_discovery.md), minimal public probes and retained
scan evidence with explicit recovery/restoration. [Verification](reports/ess_release_22_2026-10-05.md) records
COM13 address/tuple scans, budget limits and unchanged motor settings/state.

[Prompt21 evidence](reports/ess_release_21_2026-10-04.md): typed persistence
plans, zero-TX save/restore gates, exclusive nine-read snapshot, strict Python
checks and ten probes pass on the final image. Forty-eight frames, zero faults;
settings and stopped state unchanged, no nonvolatile write or restart. The motor
restart/backup/recommissioning procedure remains unavailable; physical save and
factory restoration are NOT RUN.

[Latest debug-refactor evidence](reports/debug_refactor_2026-10-04.md): the normal
timer image passed short finite forward/return moves, both moving stops,
release/enable, exact profile restoration and loaded long-frame reads. Final303
frames have zero transport/capture failures; alarm0, motion1, raw speed0 and
position0. Load/monitor/debug off, DE released and owner/results empty. Raw/decoded
debug observes the same production path; electrical/independent shaft evidence
and the omitted soak remain separate.

[Latest regular-firmware evidence](reports/regular_api_sniff_2026-10-04.md):
finite relative and nonzero absolute moves, returns, enable/release and both
moving stops passed while raw/decoded sniffing was active. Original profile and
configuration restored; final alarm0/motion1/speed0/position0. No separate
functional image or analyzer admission flag remains.

[Current functional evidence](reports/functional_motion_2026-10-04.md): short
enable/release, positive/return finite moves and normal/direct moving stops PASS
by drive reports. Final310/310 checked frames have zero errors. Original motion
settings restored; ending alarm0, motion1, raw speed0, raw position1. Actual shaft
motion was not independently observed; electrical timing remains unmeasured.

On 2026-10-04 the user explicitly authorized short **unattended functional**
free-shaft tests and removed analyzer/person-at-bench requirements for that scope.
Prepare bounded finite moves, explicit stop/status checks and parameter restoration
in advance. Functional acceptance uses drive feedback and firmware/transport
evidence; independent shaft motion/electrical timing remain unmeasured. Do not
run the several-hour test in this session. This does not provide remote motor
power control or authorize unbounded motion/link-loss experiments.

[Prompt20 fresh audit](reports/ess_release_20_audit_2026-10-04.md) repeats37
read-only frames on the corrected image with zero failures and unchanged
configuration. No communication write/save/motor restart occurred; physical
activation/restoration still requires an actual restart and qualified route back.

[Prompt20 final-image evidence](reports/ess_release_20_2026-10-04.md) passes
37 read-only frames with unchanged identity/configuration/state and zero errors
at node1/1152008N1. Address/baud/format begin gates reject before TX; no motor
write, save or restart occurred. Physical commissioning remains NOT RUN without
an actual motor restart and qualified route back. Prompt19's malformed mismatch
traffic remains unresolved; original-tuple reads do not resolve its source.

[Prompt19 fresh audit](reports/ess_release_19_audit_2026-10-04.md) records the
corrected image, all sixteen host setups, exact unchanged motor reads and ten
restored probes. Strict mismatch campaigns failed on reproduced malformed
traffic; explicit diagnostic recovery/restoration succeeded. Physical byte/UART
error sources remain unresolved. Final host1152008N1, owner empty, DE released,
load/monitor off; no motor write or motion. Alternate-tuple/electrical evidence
remains unqualified.

[Prompt19 implementation-image evidence](reports/ess_release_19_2026-10-04.md) passes
sixteen host-only tuple setups, retained historical context, two deliberate
nonresponse/recovery/restore scenarios and ten unloaded probes. Motor settings
and raw state are unchanged. Final 115200 8N1, owner empty, DE released,
load/monitor off; 51 frames and exactly two intended timeouts, no unexpected
transport/capture errors. Alternate-tuple motor communication and independent
electrical timing remain unqualified.

The [fresh prompts 17/18 audit](reports/ess_release_17_18_audit_2026-10-04.md) fixes
settings freshness, copied provenance, partial-refresh invalidation and strict
console evidence validation. All 45 native suites, installed consumption and
four firmware builds pass. The final COM13 image passes 104 frames and restores
input filter `2?3?2` and lock delay `200?201?200`; physical effects remain unqualified.

[Prompt18](reports/ess_release_18_2026-10-04.md) is a prior named timer
image: 108 cumulative frames, all20 native tuning reads and exact input-filter
2â†’3â†’2 stored restoration, zero transport/capture errors. Settings/state restored,
owner empty, DEreleased, load/monitor off. Physical tuning effects and repeated
out-of-range collision0/0 remain unqualified; no motion or gain change was sent.

The [fresh16/17 audit](reports/ess_release_16_17_audit_2026-10-04.md) is a prior
named image:114frames/1206RX bytes, all48 indexed reads, lock-delay200-to201-to200
stored restoration and zero transport/capture errors. Settings/state unchanged,
owner empty, DEreleased and load/monitoroff. Original shared-start write failure,
external triggers and mode/current/encoder/torque effects remain unqualified.

## Prior prompt17 settings evidence

[Report](reports/ess_release_17_2026-10-04.md) retains66checked frames/684RX bytes on the497760-byte timer image, zero errors and exact lock-delay200-to201-to200 stored restoration. Control words3/4000/5600/100/40/100/40/200, I/O/config/state are unchanged afterward. Mode/encoder/current settings were not changed. Owner empty, DEreleased, load/monitor off; physical torque/feedback/lock-transition effects remain NOT RUN.


## Current physical setup and standing authorization

Reconfirmed by the user on 2026-10-04: the ESS23-RS20 is bolted securely to the
table. Only power and RS485 are connected. The shaft is free and uncoupled;
no external input switches, output loads or driven mechanism are connected.
The motor housing is secured; the shaft remains available for rotation tests.

Physical motor tests are authorized on this setup without repeated permission
requests. Run bounded communication, settings and free-shaft motion tests when
their actual command/stop prerequisites are established. Missing optional
external wiring does not itself block serial-only tests. Unwired terminals do
not imply disabled drive assignments; verify the affected configuration rather
than silently changing it. Tests of external switches/loads or linear mechanics
still require those fixtures. Record physical observations separately from
RS485 acknowledgements and readback.

Prompt14 [fresh audit image/evidence](reports/ess_release_14_audit_2026-10-04.md) repeats38 read-only frames and7 zero-TX gates after feedback-reference and host-evidence fixes. Settings/state unchanged; owner empty, DE released and no transport/capture faults. Physical homing remains NOT RUN.

Prompt14 [homing delivery/evidence](reports/ess_release_14_2026-10-04.md) passes38 read-only frames and7 zero-TX home gates on its reviewed timer image. All35 method dispositions are queryable; software33/34/35 are implemented. Raw configuration/state unchanged, owner empty and DE released. Physical homing/reference/return remains NOT RUN behind explicit qualification gates.

Prompt13 [fresh audit image/evidence](reports/ess_release_13_audit_2026-10-04.md)
passes 38 read-only frames and six zero-TX setter gates after source/cache fixes.
Load/monitor off, DE released, owner empty, no capture/transport errors. Physical
settings/restoration and software-limit movement remain NOT RUN.

Prompt13 [implementation evidence](reports/ess_release_13_2026-10-04.md) adds
typed driver-settings reads and zero-TX setter gates. No settings or motion
changed; load/monitor off, DE released, owner empty and no recovery need.
Physical settings/restoration and software-limit movement remain NOT RUN.


## Prompt15 current stored-I/O evidence

[Current-image report](reports/ess_release_15_2026-10-04.md) retains190 checked
frames,13 explicit passive/unloaded settings updates with matching readbacks,
and exact original restoration. Initial/final assignments X0-X3 are1/2/3/0,
Y0/Y1 are0/0, input/output polarity and custom mask0. Logical I/O0/0,
alarm/motion0/1, position/speed0 are unchanged. Every FC06 echo retains
unconfirmed source/unknown execution; the explicit unwired policy settles only
the subsequently observed stored word. No motion, input trigger, save or device
clear was sent. Owner empty, DE released, no faults, load/monitor off.
External electrical/switch/load behavior remains NOT RUN; optional unwired
terminals do not impose a blanket serial-only control requirement.

## User report on 2026-10-02

The [fresh prompt 12 audit](reports/ess_release_12_audit_2026-10-04.md) passes ten
additional read-only model probes on the unchanged image (48 cumulative frames,
428 RX bytes, zero transport/capture errors). Load remains off, DE released and
owner empty. No physical pair write or motion qualification was added.

Latest [prompt 12 image and evidence](reports/ess_release_12_2026-10-04.md):
generated FC10 policy preserves the exact four reviewed windows. The final timer
image passes 38 read-only frames/358 RX bytes with zero transport/capture errors,
unchanged raw configuration/state and five zero-TX action gates. Load/monitor
off, DE released, owner empty. Physical paired writes/readback/restore remain
NOT RUN pending the recorded timing/source and stopped-state/input prerequisites.

Current [prompt10 image and evidence](reports/ess_release_10_2026-10-04.md) add
absolute/wrapped/clear software while preserving physical gates. The timer image
passes28-frame read-only regression plus10 probes; total47 frames/455 RX bytes
include9 successful reads before a campaign-script field-lookup correction.
All transport/capture errors remain zero, motor raw settings/state unchanged,
load/monitor off, DE released and owner empty. Pure step/degree/radian previews
agree under an explicit ASSUMED host scale; this is no physical motion proof.
Physical absolute/angle movement, position clear and origin establishment remain
NOT RUN pending the recorded command-reference/timing/stop prerequisites.

Latest prompt08 check: [fresh action/stop audit](reports/ess_release_08_audit_2026-10-04.md).
Audited timer image passes 28-frame read-only regression, ten further probes and
five action-admission rejections with zero TX. Final totals are 38 frames/358 RX
bytes, zero transport/capture errors; configuration and raw state unchanged.
Load/monitor off, DE released, owner empty. Physical actions were NOT RUN on that historical image. Subsequent finite-motion
and action evidence appears at the top of this document; the regular firmware
uses its declared wiring and software timing contract. The earlier
[implementation report](reports/ess_release_08_2026-10-04.md) retains its separate
image, counters and report-script correction.

| Item | Reported setup |
| --- | --- |
| Host port | COM13 |
| Current board firmware | CO2control |
| Physical board | User-confirmed E2, revision 2.0.0 |
| RS485 pins | User-confirmed DE GPIO21, TX GPIO47, RX GPIO48 |
| Motor connection | Motor connected to the board's RS485 bus |
| Indicator | Green light visible on the motor |
| Settings | User left the motor at its defaults |
| Mounting | Motor bolted to the table; shaft is free |
| Available testing | Communication and motion experiments when implementation is ready |

These are user-reported facts, not measurements made by this library. The
exact connected model, firmware, address, serial format, subdivision and
word order have not been read. ESS23-RS20 is the project's initial target;
that alone does not establish the identity of the attached motor. The LED
observation is recorded without assigning an unverified ready/alarm meaning.

The user explicitly permits motor testing on this setup, including motion,
and describes the free-shaft arrangement as suitable for experimentation.
This authorization persists for the described bench; routine communication,
commissioning and motion tests within that scope do not need another
permission question. Use documented commands and record what actually ran.
This note does not start a test session or claim any completed validation.

## User-confirmed model on 2026-10-04

The user confirms the attached motor is **ESS23-RS20**, using its official
product link. Its documented encoder is incremental, differential, three-channel,
1000 PPR; the nominal four-times count convention agrees with configured
resolution4000 already read over RS485. The datasheet gives 1.80-degree full
steps (200/revolution). See [source reconciliation](reference/11_ess23_rs20_identity.md).
This supersedes the initial uncertainty about the physical model and basic
encoder specifications. It does not decode firmware version0029 or algorithm3,
establish shaft accuracy, or turn subdivision-equivalent feedback into raw
encoder counts. No additional label/chip identification is needed for pure
preparation. Historical reports and raw evidence remain unchanged.

## Establish the host connection when testing starts

COM13 originally exposed the board running CO2control; see current status below.
Do not assume it is a
transparent USB-to-RS485 adapter or send raw RTU frames to an unknown console.
Inspect that firmware's available CLI/bridge and board configuration first.
Record the actual path from host to motor and any firmware change needed to
run the standalone test application. Preserve enough firmware/build
information to reproduce or restore the bench setup.

The USB console baud rate and the motor RS485 baud rate are separate facts.
The user-confirmed revision 2.0.0 bench uses DE21/TX47/RX48. The standalone
application selects those pins explicitly; its adapter has no board preset.
See the [bench configuration](reference/04_esp32_bench.md). The original E2
board label is historical identification, not a motor-bus requirement.
Direction timing, echo behavior, wiring/termination, power supply and current
DIP positions still need their own qualification.

The ESS manual lists 115200 baud and 8N1 defaults, but address selection and
actual settings require verification. Treat those values as candidate
commissioning settings, not measurements. The user report of defaults does
not resolve the manual's address/DIP or scaling inconsistencies; see the
[ESS implementation reference](reference/01_implementation_reference.md).

## First test sequence when real code exists

1. Verify that COM13 still identifies the intended board and establish its
   current firmware/console or bridge path. Keep one owner of the port/bus.
2. Use an implemented, reviewed non-changing probe and bounded discovery as
   needed. Record raw requests/replies, timing, local echo and identity
   confidence. No response does not establish that the drive is absent.
3. Read identity and relevant configuration/status. Resolve address, word
   order, command/feedback units and reference state before converted motion.
4. Exercise explicit enable/release, small position/angle/speed commands and
   stop behavior, then broaden to the documented native commands and desired
   operating modes. Record configuration and physical observations with the
   command's acknowledgement/completion evidence.
5. Exercise interruption, timeouts, lost acknowledgements and recovery with
   retained uncertainty. Track persistent and communication changes explicitly
   so subsequent tests use the actual active configuration.

Tests use the application-owned transport and caller-owned library contexts.
Neither this authorization nor a green indicator replaces protocol or motion
evidence. Hardware qualification records must name the exact tested model,
firmware, host firmware/build, pins/adapter, serial tuple, scale, test commands
and observed results. Logs must separate protocol success from physical
completion and record changes to the setup for the next session.

## Current status

- [Prompt10 fresh audit](reports/ess_release_10_audit_2026-10-04.md): corrected
  timer image, 38 read-only frames/358 RX bytes with zero transport/capture errors,
  unchanged drive settings/state and zero-TX motion/clear gates. Owner/capture
  maximum gaps158/56us, owner/worker stack headroom3572/3268 bytes. Load and
  monitoring off, DE released, owner/results empty, no recovery requirement.
  Physical motion, clear and equivalent-unit shaft comparisons remain NOT RUN.

- [Prompt07 fresh audit](reports/ess_release_07_audit_2026-10-04.md): corrected timer
  image rejects malformed fractions before host effects and prepares zero radians
  exactly. Repeated 23 FC03 frames/209 RX bytes pass with zero transport errors
  and unchanged drive settings; load/monitor off, DE released, owner empty.
- [Prompt07](reports/ess_release_07_2026-10-04.md): its timer image verifies pure
  host preparation/configuration and 23 FC03 frames/209 RX bytes, zero transport
  errors and unchanged drive configuration. Explicit operator scales remain
  assumptions; origin/soft-limit establishment rejects unresolved native feedback.
  Monitoring/load off, DE released, owner empty, no recovery needed.
- [Prompt06 fresh audit](reports/ess_release_06_audit_2026-10-04.md): updated timer image,50 checked FC03 frames/470 RX bytes with zero errors. State/health, superseded configuration confidence, finite polling, loaded state and model regressions pass. Raw state/configuration unchanged; monitoring/load off, DE released, owner empty, no recovery needed.

- [Prompt06](reports/ess_release_06_2026-10-04.md): new timer image, stationary state/health, finite polling, loaded/delayed state and existing probe checks passed. Raw alarm0/motion1/IO0/position0/speed0; algorithm3 keeps position source unresolved. All traffic FC03; configuration readback unchanged. Workload/monitor disabled, DE released and no retained/pending work or recovery requirement at cleanup.

- [Prompt 05](reports/ess_release_05_2026-10-04.md), 2026-10-04, uploaded the typed-read timer image. Five identity/config pairs (including loaded/common/profile routes), 13 model probes and one long capture read passed: 44 frames/468 RX bytes, no transport errors. Model4EEA/version0029/node1/DIP0, subdivision1000, configuredencoder4000 and unknown algorithm3 are retained without inferred mapping. Workload is off, DE released and no pending/retained work or recovery requirement remains; wiring/input levels and physical encoder/model/firmware qualification remain unknown.

- Latest verification: [prompt 05 fresh audit](reports/ess_release_05_audit_2026-10-04.md),
  2026-10-04: one identity/config pair and ten model probes passed
  (16 transactions/138 RX bytes), unchanged timer image. Load remains off,
  DE released, no pending/retained work or recovery requirement.

- [Prompt 04 fresh audit](reports/ess_release_04_audit_2026-10-04.md),
  2026-10-04, repeats 140 successful reads plus expected pre-TX timeout/recovery
  with the stricter host harness on the unchanged image. Workload is off, DE
  released and no pending/retained work or recovery requirement remains.
- [Prompt 04](reports/ess_release_04_2026-10-04.md), 2026-10-04,
  uploaded the recorded final timer image to inspected COM13. 140 read-only
  transactions passed (64 fixed 37-byte replies); the expected 20-ms pre-TX
  failure and explicit recovery passed. Ending load0/0/0, DE released, no
  pending/retained work or recovery requirement. Original backup unchanged.
  No independent analyzer was available; electrical timing remains NOT RUN.
- The [fresh prompt 03 audit](reports/ess_release_03_audit_2026-10-04.md)
  uploaded the corrected timer console and passed 37 checked read-only probes
  plus the expected zero-TX 20-ms-delay failure and explicit recovery.
  Workload is disabled, DE settled, no pending/retained work at cleanup.

- Prompt 03 connects the real BusOwner and responsive console. Current timer
  unloaded/load/interleaved and delayed-observation evidence, exact image and
  the reproduced 20-ms setup-budget boundary are in
  [the prompt 03 report](reports/ess_release_03_2026-10-03.md). Workload is disabled
  at cleanup; no motor writes or motion occurred.

- The [fresh prompt 01 audit](reports/ess_release_01_audit_2026-10-03.md) uploaded
  the corrected Runner and passed 10 unloaded, 10 loaded and 3 final checked
  read-only probes; workload is disabled. Exact current-image evidence is there.
- Prompt 01 rebuilt the Runner/timer console and passed 10 unloaded, 10 loaded
  and 3 final read-only probes. The exact retained image and evidence are in
  [the prompt 01 report](reports/ess_release_01_2026-10-03.md). Workload is disabled;
  bus-owner integration/absolute-deadline hardware tests remain for prompt 03.
- Bench access and motion-test authorization: recorded from the user.
- COM13 inspected on 2026-10-03; original CO2Control-node 1.3.0 flash backed up.
- Board now runs the MotorControl-RS 0.6.0 timer-capture load/probe console;
  workload is explicitly disabled at the end of testing. See the
  [capture/load audit](reports/2026-10-03_capture_load.md).
- The [platform-boundary audit](reports/2026-10-03_platform_scope_audit.md)
  rebuilt and flashed `bench_s3_load_timer` with explicit pin configuration.
  Read-only probes and the loaded regression passed; motor settings were unchanged.
- The [0.5.1 audit](reports/2026-10-03_audit.md) adds 20 checked probes and
  timeout/recovery/alias/reset evidence; the original flash backup is unchanged.
- Repeated checked model reads succeed at node 1, 115200 8N1, TX47/RX48/DE21.
- User-confirmed physical model: ESS23-RS20. Raw model `0x4EEA` and version
  `0x0029` are recorded for this bench; a universal wire-code/firmware mapping
  remains undocumented. Typed configuration readbacks are retained in the reports.
- External TX/RX/DE timing qualification remains open; see the explicit bench
  turnaround exception and sampling assumptions in the report.
- Motion, conversion, stop and persistence qualification: not performed.

See the [dated bench report](reports/2026-10-03_e2_probe.md) for evidence, firmware
backup/restore information, measured intervals, memory and qualification limits.

Track subsequent work in [the backlog](backlog.md). This dated setup note is
the starting point; update it with observations instead of silently treating
old settings as current measurements.

Prompt09 [finite-position evidence](reports/ess_release_09_2026-10-04.md) uploaded
the recorded final timer image and passed38 read-only frames plus zero-TX move
and action gates. A whole-record stack-temporary regression was diagnosed and
fixed; the original campaign rerun restored owner stack headroom from36 to3636
bytes. Load/monitor remain off, DE released, no pending/retained work or recovery
requirement. Physical moves and dynamic stopping remain NOT RUN behind the
documented timing/sign/basis/ramp/input qualification gates.

The [fresh prompt09 audit](reports/ess_release_09_audit_2026-10-04.md) rechecked
COM13 identity, uploaded its recorded final timer image, and passed another38
read-only frames with identical configuration/state payloads and zero-TX action
gates. Owner/capture gaps were139/53us; owner/worker stack headroom3604/3268
bytes. Load and monitoring remain off, DE released, no pending/retained work or
recovery requirement. Physical moves and dynamic stop remain NOT RUN.

Prompt11 [velocity handoff](reports/ess_release_11_2026-10-04.md) uploaded the recorded
final timer image and passed38 checked read-only FC03 frames, four velocity
zero-TX gates and finite harness rejection. Settings and raw state match the
previous baseline; transport/capture errors0, owner/capture gaps140/55us,
owner/worker stack headroom3476/3268 bytes. Load/monitor off, DE released, owner
empty and no recovery requirement. Physical velocity/ramp/stop tests remain
NOT RUN pending electrical/response-source, native speed/ramp and independent
physical stop qualification. No motion or motor settings writes were sent.

The [fresh prompt 11 audit](reports/ess_release_11_audit_2026-10-04.md) rechecked
the existing image, then uploaded the help correction and passed 19 checked
read-only FC03 frames on the final image, four velocity admission gates and
finite Python rejection cleanup. Model/version/configuration/raw state remain
unchanged; errors and console drops are zero. Owner/capture gaps are 142/52 us,
owner/worker stack headroom 3508/3268 bytes. Load/monitor off, DE released,
owner empty and no recovery requirement. Physical velocity/ramp/stop remains
NOT RUN; no motor writes or physical-motion qualification occurred.

Prompt16 [current record evidence](reports/ess_release_16_2026-10-04.md) uses the final timer image and140 frames/1387 RX bytes with zero transport/capture errors. PT1/PV16 scalar updates were read back and restored; shared-start1 native1 read back0 and remains FAIL/uncertain with preserved raw traffic. Delayed non-changing reads corroborate original settings; no replay or external trigger occurred. Load/monitor off, DE released, owner/results empty. Starting-speed firmware/source semantics and external-trigger fixtures remain open.
