# Prompt 14 - Fresh independent homing audit

Baseline `9726347`, fetched and synchronized, initially clean. Final commit is
the commit containing this report. This audit re-read the complete original
prompt and execution contract and inspected the actual `031100d..9726347`
implementation diff, current APIs, application callers and tests. Only prompt14
was audited; prompt15 remains separately dispatched.

Three fresh reviewers inspected diagrams/prerequisites, state/reference
lifecycle, and CLI/Python coverage in parallel. The source reviewer visually
checked original physical PDF19/20,32-67,72/73 and independently re-reviewed the
integrated fixes. Root verified findings against source and reproductions.
[Original handoff](ess_release_14_2026-10-04.md), [API](../ess_homing.md),
[all35 method dispositions](../ess_homing_methods.md),
[exact current-image/raw evidence](ess_release_14_audit_2026-10-04.json).

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Documented methods/prerequisites | `homeMethod`/`homeMethodAt`, original method diagrams and ledger | All35 descriptors audited;33/34/35 executable; optional terminals not mandatory | Both method-report routes PASS |27 switch methods UNIMPLEMENTED;18 and4 collision methods UNRESOLVED |
| Whole preparation, auxiliary and offset | Installed `prepareHome`, exact method/native qualifiers, reviewed0x0031/6 staging | Bad target/generation/parameters/index/input/auxiliary/readiness, unchanged output/no TX and offset extremes PASS | Six method gates and nonzero-offset syntax reject with no TX | Active auxiliary7, exact physical rates/ramp and nonzero offset unresolved |
| Fresh search/completion/reference | `advanceHome` and `getHomeReference`, bounded supplied events and copied evidence | Old-high/missing activity/low/high/zero, unexpected RUNNING35, alarms, stop and lost replies PASS | Stationary raw baseline unchanged; no reference claimed | Physical search/return/independent shaft-zero NOT RUN |
| First post-home feedback | `advanceReads`, existing cache `current` and qualified native-zero witness | Five actual SDK-fake application scenarios: historical123 to fresh0 preserves; immediate/later nonzero invalidates; unresolved word order covered | Updated-image state regression PASS | Qualified homing execution still simulated |
| Cancellation and ownership | Same application owner, axis reservation, immutable deadlines | Full existing cancel/stop/recovery/generation/storage suites PASS | Final pending/retained/reserved0, DE released, no recovery | No physical stop qualification added |
| Checked rejection versus malformed reply | Existing checked ESS parsers and `HomeContext` execution classification | Correct FC06 exception emits REJECTED; wrong-function exception emits FRAME_ERROR/UNKNOWN;15 actual console terminals PASS | Read-only checked FC03 regression PASS | Physical FC06 source/echo evidence remains open |
| Strict host evidence and finite cleanup | Python `_check_home`, existing finite method35 scenario |167 host tests; forbidden readiness flags and forged failure outcomes reject; both delayed-evidence cases PASS |96 exact lines, seven zero-TX gates PASS | Physical scenario remains gated; no retry/replay |
| Public/package/platform boundaries | Existing installed Homing API, no core changes/framework dependency |33/33 CTest, installed1/1 and all4 firmware environments PASS; generators/inventory current | RAM/PSRAM/stacks and service/capture measurements below | No switched fixtures or full qualified execution stack measurement |

## Confirmed findings and fixes

1. The first post-home feedback compared new zero against invalidated pre-home
   feedback (reproduction: raw123, new reference/gen2, refresh0 erased it/gen3).
   Historical feedback is now usable as a delta baseline only when current and
   not older than the reference. A fresh nonzero raw pair still invalidates a
   qualified native-zero witness immediately, including unresolved word order.
   Zero/nonzero requires no sign/order conversion. No extra ownership flag or
   queue was added. Existing target-isolation tests now use coherent nonzero
   reference/history50 and new51, keeping their original independent purpose.
2. Python accepted terminal records with admission RUNNING/release/alarm/limit
   flags that `prepareHome` rejects. The checker now applies the same documented
   forbidden mask, while preserving unknown bits without inventing meaning.
3. Python accepted a successful zero FRAME relabeled as cancellation or transport
   failure. Outcome-specific checks now require corresponding local/event/status
   or checked frame evidence. On-time final closure delivered after the deadline
   can still succeed; on-time intermediate closure delivered too late to issue
   its required zero read remains a valid next-step deadline failure.
4. The console fixture named exception used0x90 during an FC06 trigger, testing
   wrong-function rejection rather than a checked exception. It now uses0x86;
   parity asserts EXCEPTION/REJECTED and separately retains the malformed case.
   Five additional emitted failure/timing cases now exercise the host checker.

No new source-semantic, optional-I/O, implicit setting/retry, public boundary or
unbounded ownership defect remained after independent final re-review.
FieldCore was reinspected read-only: `Rs485Task` cancellation releases active TX,
RX processing strips a request-identical prefix, and its current8-byte TX
capacity cannot hold21-byte staging. This reference continues settling physical
TX and preserving checked echo/source uncertainty, with portable supplied events
and an application-owned axis reservation. No FieldCore source was changed.

## Final verification

Native **33/33 PASS**; Python host **167 PASS**; actual core/console/Python parity
**15 PASS**; generator **18 PASS**; inventory negative cases **16 PASS**; installed
consumer **1/1 PASS** with no example include path. Version/register checks and
all5 preserved serial-reference snapshots PASS. All four PlatformIO environments
PASS: units, probe, polling load and timer load. `git diff --check` PASS.
Logs/images remain under `build/bench/prompt14_audit_*`. Core/public/generated/
ledger/vendor bytes are unchanged. Storage sizes remain HomeContext856,
prerequisites64, request24, yielded work72 and shared evidence80 bytes (Xtensa14.2).
No extra app buffer/task/queue; larger records remain PSRAM, required driver/
capture state and stacks internal. Physical qualified-operation stack use is
still NOT RUN; simulated tests are not physical measurements.

## Corrected-image COM13 regression

Fresh USB303A:1001, serial3C:0F:02:CD:6B:98, passive prior-image preflight PASS:
1152008N1, node1, load/owner-delay/console injection0, DE released, empty owner,
no recovery fault. Final timer image456416 bytes, SHA-256
`dee8aa9eca945d93d12d7c66004b1a840948d1e12670021e32a1007e54da38fd`, uploaded
with data hash verified. UART2 TX47/RX48/DE21, response200ms, reply gap304us,
GPTimer20us and85us gap guard. The original handoff/image remains historical
evidence; this report contains the corrected image's independent regression.

38 checked FC03 frames/364 RX bytes PASS, including typed driver/identity/config/
state reads and ten one-attempt probes. Before/after assignments/settings match:
raw model/version4EEA/0029, node1/DIP0, subdivision1000, encoder setting4000,
algorithm3 unknown, input assignments1/2/3/0; raw alarm/motion0/1, I/O0/0,
position/speed0 and homedfalse. Seven zero-TX rejection checks PASS. No home,
motion, auxiliary/input/settings/save/restore, reset or recovery write was sent.

First driver delivery spans29.584ms; model probes5455..5509us, mean5483.4us.
Owner/capture max gaps157/54us, capture high-water2 and no gap fault.
Capture8452871/41584007us =20.33% of one core. All transport/capture/RX errors,
timeouts/cancellations/discards/echo bytes0. Optional trace overwrites616.
All96 sent lines are retained; driver snapshot atline92 reports input drops,
output blocks/short writes0, followed by four local reports. Load-generated
lines/drops0/0. Internal free/min/largest336656/331496/278516; PSRAM
8261164/8261164/8257524; owner/worker stack headroom3476/3268 bytes. Final DE
released, owner empty, recoveryfalse, monitoring/load off. No uncertain motor
write or hardware failure was replayed.

Physical homing/reference/search/return remains **NOT RUN**: electrical/source,
exact native rate/ramp, active auxiliary7, method/index/mechanics and command
coordinate correspondence remain unqualified. Switch-dependent edges/return
remain **UNIMPLEMENTED**, with prompt15 admission/fixture handoff intact.
Method18 conflict, collision encodings/parameter mappings and nonzero offset
semantics remain **UNRESOLVED**. No missing switch/load or independent shaft
observation was fabricated; host origin and device clear remain separate actions.
