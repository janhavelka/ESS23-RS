# Prompt 20: fresh independent audit

Baseline `6d033ef`; final revision is the commit introducing this report.
Re-read the complete [prompt](../prompts/ess_release/20_device_communication_commissioning.md),
execution contract and sequence dispositions, then inspected the actual
`7d0092d..6d033ef` implementation and current callers/tests. Only prompt20 was
audited. [Structured evidence](ess_release_20_audit_2026-10-04.json) and
[raw archive](ess_release_20_audit_2026-10-04_evidence.zip) preserve fresh console
traffic, build logs, exact firmware, source snapshots and a SHA-256 manifest.

## Findings and corrections

Three fresh parallel reviewers covered core sequencing/boundaries, application
failure/recovery ownership, and activation/persistence sources plus CLI/Python.
The lead checked findings against code and reran their reproductions/checks.

1. Selecting an already-active host tuple unconditionally discarded current
   confirmation eligibility. After the second confirmation this could strand
   a settled session without a remaining read slot. Preserve confirmation only
   when selection succeeds with an unchanged serial generation. Real changes,
   failures and recovery still invalidate it. The actual application regression
   uses both confirmation slots, selects the unchanged tuple, then finishes with
   no additional UART configuration or wire transaction.
2. The Python checker accepted some internally valid evidence for the wrong
   requested target, host tuple or confirmation candidate. It also accepted
   missing session context, false save/restart requirements and reused successful
   begin IDs. Checks now correlate the action and retained candidate, retain the
   selected candidate through pending inspection, and require a fresh write
   operation for successful begin. Finite procedure polling pins both operation
   and attempt step, stopping on replacement without further commands or replay.
3. Independent review of that fix caught a legitimate refusal case: a new client
   can receive Busy with an unrelated historical session. Such failure diagnostics
   remain valid; they are not evidence that the requested begin succeeded.

No public core API, codec access policy, UART owner, timing guard or write
qualification was expanded. No extra recovery engine or automatic retry was
added. A suspected recovery-quarantine bypass was disproved: the production loop
withholds owner recovery readiness until the late-response guard expires. A new
actual-application test verifies no confirmation admission/TX during that period.

## Requirement and verification matrix

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Typed settings and exact before/requested contexts | `prepareCommunication`, checked FC06/FC03 and supplied `ActionEvent` | Existing core suite plus134136 adversarial classification/value checks PASS | Configuration readback unchanged | Physical communication writes NOT RUN |
| Stored versus responding interface, source rules | Retained candidates, save/restart requirements and `activationUnknown` | Original function pages6/12/26/69 and hardware page11 reviewed; strict requirement validation PASS | Actual custom0/baud0/format0 still responds at node1/1152008N1 | Activation instant, ACK address and baud/format save requirement unresolved |
| Lost/delayed ACK, invalid/stale events, uncertainty | Core write and two explicit confirmation slots | Native failure/deadline/immutable evidence suites PASS | No physical fault injection | No FC06 response-source or electrical qualification inferred |
| Exclusive ownership and known restoration | Owner token lease and application host/recovery callbacks | No-op selection fix; stale result, switch failure, quarantine and producer exclusion PASS | Three unavailable-route begin gates leave traffic unchanged | Alternate active tuple/restoration NOT RUN |
| CLI and finite host procedure | Public console callback, `Console._check_communication`, `communication_campaign` |23 focused +184 existing Python tests PASS; exact target/tuple/session/step and valid refusal cases | Same harness parses plan/inspect/refusal traffic | Begin remains unavailable without qualified route |
| Existing behavior/package/platform | Actual main.cpp fake SDK path and installed library |52/52 CTests, installed C++11 consumer and four firmware builds PASS |37 checked read-only final-image frames PASS | Prompt21 save/activation and prompt22 discovery not executed |

Core adversarial review exercised every exception byte0..255 across all three
settings and qualification combinations, and every unsupported uint16 baud/format
code4..65535 with unchanged output checks. The lead reran the preserved standalone
review program. These are software checks, not drive behavior evidence.
Full strict Release GCC15.1 native verification passes; final Python-only changes
were followed by both affected CTest suites. Version/catalogue generation and
all five offline contrast snapshots pass. Units/probe/polling-load/timer-load
firmware environments pass. Final fixes received a separate independent re-review.

FieldCore-node `6debce7c44af9bc36f00fbcaccdfa47631846cce` owner/backend/module/CLI
was inspected read-only again. Its fixed8N1/eight-byte sensor transactions and
request-prefix echo stripping remain deliberate integration differences. No
FieldCore files or framework types were imported.

## Corrected-image bench

Fresh COM13 identification: USB303A:1001, serial3C:0F:02:CD:6B:98,
MotorControl-RS0.6.0/protocol2. The previous image still had37/37 successful
frames and no faults; nine additional before-image frames passed without clearing
statistics or recovery. Its local/running firmware hash was
`3aa93dcbeb49964691d34c282a929494a3d7d3bf39d3457fd831363a8418d860`.

Uploaded corrected timer image:531216 bytes, SHA-256
`e3baf752499f39c912b9e437dcdf7105feb1eb65f9fa36da4aac07c0eb5bba82`.
Static RAM29384 bytes, linked flash525040 bytes. UART2 TX47/RX48/DE21,
node1/1152008N1, timer20us, starvation85us, response200ms, reply gap304us.
Upload restarted the host board only; no motor write, save or restart occurred.

The final campaign and ten one-attempt/50-ms regression probes passed **37/37
frames,321 RX bytes, zero failures/timeouts/capture faults/UART errors**.
Identity/configuration/state matched before upload and after the campaign.
All three begin gates returned unavailable with no lease/context and identical
before/after traffic counters. Campaign probe durations5440-5570us.

Ending host known/unblocked at1152008N1, owner idle, DE inactive, no recovery
requirement or queued/retained/reserved work. Load/delay/console workload0/0/0;
monitor off; console blocked/short-write/drop counts0. Owner/capture gaps147/56us,
capture high water2, bounded trace overwrites563. Capture section9165706/45086997us
(20.33%) is separate from scheduler estimates0%/7%. Internal free/min/largest
336640/331480/278516 bytes; PSRAM8201772/8201772/8126452; owner and worker stack
headroom3268 bytes each. Raw evidence preserves all measured fields.

## Remaining limitations

Physical address/baud/format changes, activation and restoration remain **NOT
RUN** without actual motor restart control and a qualified route back. Optional
unwired inputs are not the blocker. Independent TX/RX/DE and FC06 source evidence
remain missing; prompt19's malformed mismatch traffic remains unresolved.

The two-confirmation limit is deliberate and remains explicit: after both fail,
or after their evidence is invalidated by actual recovery/reconfiguration, the
CLI retains the lease and evidence for operator/application handoff. It does not
offer a third confirmation or treat an MCU reboot as evidence-preserving recovery.
The no-op selection correction avoids needlessly reaching that limit. Prompt21
must revisit explicit save-dependent address activation with an actual fixture;
this audit neither implements persistence nor claims physical commissioning.
