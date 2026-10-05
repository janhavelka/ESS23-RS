# Prompt23 fresh independent audit — 2026-10-05

Baseline: `5293fbd0b583e9668895d39afffd72141ad822a5`, clean/synchronized
`main`. This report's containing commit identifies the final source. The full
prompt23 and execution contract were reread; reviewers inspected source,
callers, tests and the integrated diff independently of the earlier handoff.
No subsequent prompt was started.

## Findings, corrections and evidence

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Host tuple changed after owner terminal but before application harvest | `hostSerialImpl` keeps the pending profile/request ownership guard through harvest | Real SDK/runner result-before-harvest regression; zero UART mutations until harvested | Ordinary final-image host/session diagnostics PASS | Fault injection remains simulated |
| Selection clears declarations but offered no redeclaration route | Optional synchronous `Host::wiring`, `WiringRequest`/`WiringSnapshot`, `wiring` callback and one console route | Bounds, all three wiring states, missing hook, faults, idle/generation checks, no TX, old settings/results retained | Node1→2→1, six explicit unconnected declarations and checked I/O/configuration reads PASS; bus counters unchanged by local controls | Switch/load electrical behavior NOT RUN: no external wiring |
| Source uncertainty collapsed into unsupported | Shared example `driverAdmissionStatus` used by checked CLI value validation and actual application preparation | Unresolved source enums, missing effect prerequisites and unsupported pair policy remain separate | Output function11 rejected as `unresolved` before TX | Unknown current/segment/collision semantics remain guarded |
| Missing explicit ESS profile route | `profile ess_rs motion-profile ...` normalizes into the existing parser/callback | Same callback and byte-identical inspection reply, help/dispatch/inventory tests | Bare route snapshot/inspect/restore/forget PASS | Alias wire-equivalence is native evidence |
| Failed restore could lose its sole uncertain write evidence | `serviceMotionProfile` retains bytes, accepted count, closure, UNKNOWN, original write deadline and three generations; `restoreUnsettled` reserves the axis | CRC failure, no replay/forget/rebind, explicit recovery, stale stationary refusal, readback disagreement and matching read-only reconciliation | Normal acknowledged restoration/readback PASS | Lost-ACK injection NOT RUN on motor |
| Read refresh could misattribute uncertainty or erase unsent attempt | Fresh read resets its uncertainty from historical write only; history retained by write frame length, including zero accepted bytes | Failed-read→successful-read and cancelled zero-TX restore regressions PASS | Normal repeated inspection/readback PASS | Simulated boundary evidence |
| Python rejected repaired-tuple reconciliation | Strict `_check_motion_profile` retains historical write tuple/context while allowing an explicit new reconciliation read after repair | Lost-ACK lifecycle, immutable metadata mutations and repaired serial-generation tests PASS | Normal correlated restoration PASS | Framing failure still ends the session; no retry |
| Coverage hid arbitrary profile staging behind original snapshot restoration | Inventory explicitly adds the09/11 partial staging/archive obligation; inspect/forget/wiring receive no device-operation credit | Complete inventory checker plus26 adversarial tests PASS | Archive import remains unavailable | Eight named prerequisite gaps remain open |
| Stop responsiveness, cached commands and diagnostic isolation | Same production move/action/owner/parser path; five passive commands before priority stop | Full native suite includes actual app pressure, strict parsing, generations and reset/recover uncertainty cases | Finite100 move and two loaded moving-normal-stop campaigns PASS; final-image ten probes after each cleanup PASS | Physical shaft/electrical latency and endurance NOT RUN |

The implementation adds no core platform dependency, second console, queue,
UART owner, conversion path or arbitrary address setter. `wiring` is a host
declaration, not a terminal assignment or voltage observation. Inputs remain
assigned1/2/3/0, outputs0/0; optional unwired inputs do not prevent the qualified
serial-only finite path. All admitted device work still uses checked public
profile APIs. Storage capacities and cooperative calling rules are unchanged.

## Restoration and ownership contract

`MotionProfileView` keeps current read evidence separately from historical
write evidence (`writeDeadlineUs`, configuration/serial/binding generations,
raw TX/reply, accepted count and UNKNOWN). On accepted restore TX, dependent
axis knowledge invalidates once and the axis remains reserved until matching
checked readback. `restoreUnsettled` blocks new restoration, forget, target
rebinding and competing motor writes. Stop, passive inspection and explicit
transport repair remain available.

After failed restoration: repair transport if necessary, refresh configuration
and stationary state, then issue `motion-profile read`. It performs a new
read-only reconciliation, never replays FC10. A lost/uncertain acknowledgement
also requires stationary evidence observed after the original write delivery.
Mismatch, stale evidence or failed refresh retain the reservation. Matching
stored words and fresh standstill settle that reservation without changing
historical UNKNOWN into acknowledged execution. Explicit forget is possible
after settlement. Original write deadlines never renew when a new read begins.

The optional wiring callback consumes the request synchronously and fills a
caller-owned snapshot. Its six declarations copy bounded data only. Mutation
requires idle ownership, increments host configuration generation and clears
dependent preparation/reference confidence. Raw drive settings, binding and
retained historical results survive. Query remains passive during faults.

## Hardware and retained failed attempts

COM13 was checked as USB303A:1001, serial `3C:0F:02:CD:6B:98`, ESP32-S3 N16R8,
UART2 TX47/RX48/DE21, ESS node1 at1152008N1. User-confirmed ESS23-RS20 remains
wire model `0x4EEA`, firmware `0x0029`. Observed direction0, subdivision1000,
word order0, algorithm3, encoder setting4000 and I/O assignments are unchanged.
Unknown algorithm/source/sign/engineering units are preserved.

Final uploaded timer image: **575424 bytes**, SHA256
`4ae880946615c4828992f864e983da36417123df39c15c82a045dd38f30487d3`.
The earlier audit image hash
`a45bbc45afdb56c15ace85437246786ab9254c3d23f202951e999b1bcbff7f28`
belongs only to retained failed harness attempts. The original firmware backup
and prior prompt reports remain untouched.

Two audit-script errors were retained. Attempt1 omitted `set` in the I/O
argument tuple; Python rejected it before any move. Attempt2 asked for `queue`,
which is not a command (`drv` supplies queue diagnostics), during a finite
motion. The prepared direct-stop cleanup succeeded, with fresh alarm0/running
false/raw speed0. These were harness `ValueError`s, not UART/motor faults. The
script was corrected to the actual command inventory and its failure path now
also restores the same-session profile before closing a usable connection.

Attempt2 closed before profile restoration, losing its volatile original
target100. Successful final-image sessions captured/restored their actual
`[30,100,100,60,0,250]` snapshot exactly. This is session restoration, **not**
restoration of the audit's starting target100 or historical target5000. Both
originals remain archived. No arbitrary register write or motion replay was
used to hide that limitation. The final staged target250 has no pending trigger;
the motor reports enabled standstill. Arbitrary archived staging remains the
explicit09/11 gap. Future experiments must keep the backup/session open through
cleanup; a USB restart does not restore motor parameters.

The final image passed an unloaded finite100-step/60-native-rpm move and two
distinct finite250-step moving-normal-stop experiments. Each obtained new
RUNNING before stop; interrupted operations retained cancellation/uncertainty
separately from successful stop evidence. Five cached/control queries ran
before stop with load1000us, owner-delay0, console generation64bytes. A second
loaded experiment captured counters before turning load off (changing load
resets its measurement window). Both ended with fresh standstill and exact
current-session profile readback, followed by ten one-attempt probes.

Loaded stop intervals from firmware admission through observed stop were
**115033us** and **118193us**. The last host send-to-full-state interval was
391ms, including console transport and subsequent three-block state reads.
These are application/drive-report intervals, not independent shaft
deceleration or electrical latency. The last loaded window recorded66 generated
lines/zero fixture drops, owner gap3079us, capture gap54us, capture high-water11
under64-byte read budget, worker stack free3268bytes. Native pressure cases
verify bounded output disposition; hardware reported no input drops, short
writes or blocked output.

Final cumulative statistics on the final image: **229/229 frames**,2244RX bytes,
zero failures/timeouts/capture faults/RX errors/echo bytes. Sessions3/4 supplied
126 and103 frames respectively; no statistics reset/recovery hid a failure.
Local selection/declarations left counters8→8 in session3 and134→134 in session4.
Final driver snapshot:264 accepted console lines/4094bytes, zero dropped lines,
output empty, pending/retained/reserved0, DE released, recovery clear. Raw alarm0,
motion0, speed0, position1367; position is not relabelled a calibrated encoder or
command coordinate. Host1152008N1, debug/monitor/load off; final forget explicitly
released the settled volatile snapshot. No save, factory restore, motor restart,
continuous velocity or uncertain write replay ran.

Actual baud115211; character bounds85..89us, capture20us, RTU750/1750us,
bench-only first reply304us, response200ms/request500ms, TX20ms and recovery500ms
are unchanged. Larger app/results/trace storage remains in PSRAM; required
driver/capture/stacks remain internal. Final internal free/min/largest
336608/331448/278516bytes; PSRAM free/min/largest8177196/8177196/8126452bytes;
owner stack free2036bytes. Timer build static RAM29384bytes, flash569028bytes.
Diagnostic trace overwrites remain display loss, independent of retained
protocol/results.

## Verification, independent review and handoff

- PASS64/64 native CTest suites, including installed core-only consumers.
- PASS220 Python transport/feature tests,21 finite-campaign tests,26 inventory
  failure tests and18 generator tests; version/catalogue and five contrast hashes.
- PASS probe/poll-load/timer-load/units PlatformIO builds and final upload.
- PASS final-image local declarations/reads, finite motion, loaded moving stops,
  exact current-session restoration and short read-only regression.
- Independent inventory/API and state/effects reviewers audited production
  code; a failure reviewer inspected edge cases and strict Python evidence.
  Final re-review found no remaining source blocker after fixes. The lead
  verified findings against source/diff and reran the actual failure cases.

FieldCore HEAD `60023936c2caf166eaf43f82254c1d9540b8522f` was inspected read-only:
RS485 task/transaction/backend/CLI conventions remain comparisons. Cooperative
ownership and explicit result provenance are useful; sensor retry/echo stripping,
firmware modules and framework types are deliberately not imported.

Coverage stays **221 records/135 choices/49 operations/130 installed free
functions**;69 choices implemented,65 not implemented and one unsupported.
Read192 implemented/nine missing; write170 implemented/two missing/three partial/
18 guarded unsupported. Eight named gaps return to09/11,14,12/13/16 and18,
as listed in [coverage](../ess_api_cli_coverage.md). No denominator was reduced
and this audit does not declare the whole native family/release complete.

Prompts24–26 inherit the same command/result contract, explicit wiring callback,
immutable restoration evidence and strict Python checks. External switches/loads,
independent shaft/electrical measurements, motor restart/persistence proof and
several-hour soak remain NOT RUN. See [machine summary](ess_release_23_audit_2026-10-05.json)
and [raw evidence archive](ess_release_23_audit_2026-10-05_evidence.zip).
