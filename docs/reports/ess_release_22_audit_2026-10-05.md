# Prompt22 independent audit

Baseline `c4ca9d1`; final audit commit is the commit containing this report.
Prompt22, its execution contract and discovery contract were re-read in full.
Three fresh parallel reviewers inspected actual production source, caller paths,
tests and the implementation diff. Root reproduced/verified their findings and
reviewed the integrated corrections. Only prompt22 and bounded prerequisite
corrections were executed; prompts23-30 remain unexecuted.

| Requirement / failure | Actual production API/path | Native/build evidence | Hardware result/artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Non-changing inventory/probe/refinement | Discovery.h/.cpp, prepareProbe/checkProbe, ESS prepareIdentity; FC03 0/1 and 0/4 only | discovery, installed C++11 consumer with no examples include path | PASS raw identity and unchanged configuration/state | Model/version mapping and collision exclusion unresolved |
| Bounded candidates/deadlines/request/results | DiscoveryApp.h and existing cooperative owner/UART | discovery_app budget, pressure, delayed closure and setup checks | PASS default/refined, address1..2, result/request exhaustion, two host tuples | Alternate motor tuples unqualified |
| Recovery before scan delivery | main.cpp recover cancels scan continuations after accepted owner recovery | Regression fails without correction; before admission, unharvested responder and active TX pass with it | PASS active recover retains one probe, admits no refinement/next address, restores original tuple | No implicit continuation or replay |
| Refinement failures/cancellation | serviceDiscovery + advanceRead | Exception, CRC, wrong address, wrong length, late reply; cancel queued/setup/TX/receive | Simulated fault injection; ordinary physical refinement PASS | Physical malformed/collision fixture NOT RUN |
| Urgent preemption/exact tuple restoration | Existing reserved stop plus discovery restore | Stop during9600 scan restores115200 before FC06, unchanged deadline, retained9600 evidence; full pressure and expiry tests | Stop path tested natively; read-only restoration PASS | No motor stop/write sent in this audit |
| Strict retained wire/status/identity evidence | Python Console._check_discovery | 24 discovery cases including actual console JSON; rejected forged success, ambiguity/status and mutable zero-byte failure | PASS actual interleaved controls, discovery-check and stress | No auto retry/recovery after framing failure |
| Local inventory/help parity | ProbeConsole profile list/help, same public inventory | Minimal-hook help regression fails before correction; legacy hook-gate tests pass | Local routes available on current firmware | Other operation routes retain their own admission checks |

## Confirmed defects and corrections

Accepted recovery previously omitted the application's scan continuation.
Ordinary service often observed the transient recovery fault and stopped the
scan, but if the owner finished recovery before scan service, scanning resumed
with its saved old generation. Recovery now marks an owned scan cancelled
immediately after acceptance. Admitted transport still settles, findings stay
retained and exact original-tuple restoration waits for recovery settlement.
Rejected recovery has no new scan side effect.

Python previously trusted `identity_known` before validating refinement wire
metadata, accepted ambiguity without an identity attempt and accepted successful
status on a no-response record. It also allowed a settled zero-byte refinement
failure to change during inspection. It now checks codec/local failure evidence
independently, derives identity success from qualified on-time checked closure,
and freezes settled evidence by the reviewed read-count marker. This preserves
the real distinction: checkProbe rejects late/unqualified timing before parsing;
advanceRead retains the codec result even when timing prevents publication.

Local `profile list` needed no motor-operation hook, yet help omitted/refused
`profile` without those hooks. Help now recognizes local inventory availability;
typed device routes still check their actual hooks. The full-suite run exposed
one legacy assertion expecting inventory to be absent; that expectation was
updated while preserving no-hook device-command rejection checks.

Additional proof closes refinement-failure/cancellation, alternate-tuple stop
restoration and installed-core discovery consumption gaps. No second queue,
UART, retry engine, profile, heap allocation or motor write path was added.
The sequence introduction and stale discovery backlog wording were corrected.

## Source and integration review

The original function-manual physical p68 (printed66) was visually checked:
model/version/current-node/DIP at0x0000..0x0003 are read-only, with no documented
consumption. Unknown0x4EEA/0x0029 remain raw; no manufacturer/model mapping or
watchdog behavior was invented. Ledger/generated register files are unchanged.

Current FieldCore HEAD `60023936c2caf166eaf43f82254c1d9540b8522f` was re-read
without edits: Rs485Task queue/admission/results/recovery, ShzkDeviceModule parser,
ArduinoRs485Backend and Rs485Cli. Request/work/result vocabulary remains useful;
its sensor payloads, retry policy, eight-byte TX,8N1-only tuple assumptions and
request-echo stripping are deliberate differences. They are not imported into
the framework-independent core or this single-task cooperative application.

## Verification and current-image bench

Final63/63 CTest suites PASS,24 discovery Python cases PASS,203 existing Python
probe cases and18 generator cases PASS. Generated version/register/reference
checks PASS. Installed C++11 consumer compiles/links/runs Discovery APIs from
exported core includes only. All four PlatformIO environments PASS:
bench_s3_probe, bench_s3_load_poll, bench_s3_load_timer and bench_s3_units.
`git diff --check` passes. Evidence retains the earlier full-suite legacy-help
assertion failure alongside its corrected final run.

COM13 USB303A:1001 serial3C:0F:02:CD:6B:98, ESP32-S3 N16R8; UART2
TX47/RX48/DE21, secured free-shaft ESS23-RS20 with only power/RS485 connected.
Final timer image571072bytes, SHA256
`b48e9b6b056eff7daa632a085a9ddfe94747a67830342e8f8fe0d63205319b11`.
1152008N1 requested/115211 actual baud,20us capture,85us capture-gap limit,
304us recorded-tuple first-reply exception,200ms response/500ms request budgets.
Original board backup remains preserved; no motor restart or nonvolatile write.

The repeated feature session has42 admitted reads,40 checked frames and exactly
two deliberate NO_RESPONSE results (node2 and host96008N1 mismatch). Each fault
retains partial results, refuses premature restore, then uses explicit recovery
and exact original-tuple restoration. Request/result budgets, refinement and
ten final probes pass. Identity `[0x4EEA,0x0029,1,0]`, raw configuration and state
remain unchanged: alarm0,motion1,logical I/O0,speed0,position0.

Ten final probe durations5478..5521us, mean5497.4us. Owner gap1859us,capture
gap54us,high-water2; zero capture faults/RX errors/starvation flags. Console110
lines,0 dropped/backpressure. Internal free336640/min331480/largest278516bytes;
PSRAM free/min8177196/largest8126452bytes; owner stack free2800bytes,worker3268.
Load/monitor/debug off. Storage layout unchanged: S3 DiscoveryScan4536bytes,
App206496bytes in PSRAM; required capture/driver state and stacks stay internal.

A separate current-image explicit-recovery session retains one valid probe and
no identity/next-address admission, then restores/releases the scan and probes
normally (two checked reads,zero failures). Following CLI discovery-check adds
two checked reads; ten-probe CLI stress PASS. These are separate board/console
counter sessions, not one cumulative counter claim. Ending host1152008N1,
DE released, owner/results empty, scan released, no recovery/configuration fault.

Physical duplicate responders, alternate motor tuples, exact model mapping,
undocumented watchdog effects and independent electrical/shaft measurements
remain unqualified. Historical prompt19 malformed mismatch traffic is still
unresolved; passing read-only nonresponse tests do not resolve its source.
No motion, tuning, persistent write, new manufacturer or soak was performed.

[Machine-readable evidence](ess_release_22_audit_2026-10-05.json) and
[raw evidence archive](ess_release_22_audit_2026-10-05_evidence.zip) retain commands,
images, traffic, timing, resource snapshots and verification logs. Prompt23 can
use the verified public and CLI paths while preserving explicit restoration,
unknown identity confidence, copied evidence and one-owner access.
