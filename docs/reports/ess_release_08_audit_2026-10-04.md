# Prompt 08 fresh audit: actions, uncertainty and priority stop

Audited the complete [prompt 08](../prompts/ess_release/08_operations_enable_release_and_stop.md)
against actual production code, callers, tests and the original vendor manual.
Baseline and upstream were `980a4d94f56bd3ddc65a1a046328e8fed66fe67b`.
The final audit commit is the commit introducing this report. No later prompt
was implemented. Software, package, firmware builds and available read-only
hardware verification: **PASS**. Physical actions and electrical qualification:
**NOT RUN**.

The [structured evidence](ess_release_08_audit_2026-10-04.json) and
[archive](ess_release_08_audit_2026-10-04_evidence.zip) retain the campaign,
preflight/ending snapshots, raw JSONL, exact firmware, source snapshots/diff,
verification logs and SHA-256 member manifest.

## Confirmed findings and corrections

1. `advanceAction` classified every checked ESS exception as REJECTED. This
   allowed an undocumented exception such as `E7` to clear the application's
   axis conflict despite lacking documented non-execution evidence. Original
   function-manual physical page 12 defines only `01..07` as request errors.
   Those codes retain REJECTED; all other exception bytes retain UNKNOWN,
   copied raw reply and codec detail. The existing application reservation now
   remains set through result release and transport recovery. A later explicit
   stop can still be admitted. Generic codec/transport evidence is preserved;
   the profile supplies execution interpretation.
2. The Python action validator accepted impossible successful records: final
   observations preceding acknowledgement, closure past the deadline, mismatched
   poll/step counts, successful truncated frames, and populated failure evidence.
   It now cross-checks the retained transaction sequence, lengths, terminal
   evidence, exception interpretation and outcome. An on-time closure delivered
   after the deadline remains valid. Failed truncated/unqualified evidence and
   prior valid observations remain inspectable without replay.
3. Regression coverage lacked explicit stop admission during DE hold/partial TX
   and an interrupted write failing after stop admission. Added those tests to
   the real portable owner and application paths. The latter retains both
   outcomes, blocks traffic for recovery, cancels the unsent stop during explicit
   recovery and requires a new explicit stop. Nothing replays automatically.
4. README and the profile API table still described actions as unimplemented or
   named a planned `Commands.h`. They now describe the actual installed action
   headers and the separate physical qualification gate.

No second scheduler, backend, conversion path or event framework was added.
The production C++ correction is one profile-specific exception classification
guard. Existing bounded evidence and scheduling helpers remain the owners.

## Requirement audit

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Five explicit action choices and typed effects | `ActionOperation.h`, ESS `Actions.h`, named common/native preparations | Literal FC06 commands, policy rejection and common/native parity PASS | Five zero-TX admission rejections PASS | Physical effects NOT RUN |
| Finite caller-owned sequence and released bus during waits | `nextAction`/`advanceAction`, existing `ReadEvent` envelope | Time/poll bounds, delayed evidence, independent reads PASS | No action sequence transmitted | Drive response/effect latency |
| Wrong/stale/duplicate event leaves context unchanged | Core target/generation/operation/step/time validation | Bytewise unchanged-context and terminal duplicate tests PASS | NOT APPLICABLE: supplied core events | Consumer must preserve correlation |
| Correlated bad frame/timeout consumes failure without payload publication | Checked codec and copied `ActionEvidence` | CRC, wrong echo, oversize, partial/full TX and new exception matrix PASS | Read-only CRC/frame checks PASS | FC06 echo-source qualification |
| Shared same-axis reservation across common/native/CLI | `App` active records plus retained conflict bitmap | Unknown exception survives release/recover; unrelated reads/targets PASS | No physical action admitted | Future motion must reuse this reservation |
| Validate policy and reserve urgent/results before cancellation | `startAction`, owner urgent slot, dedicated stop record/correlation | Rejected policy/full capacity preserve active work; blocked console retains both results PASS | Gate rejects before TX/counter change | Physical stop NOT RUN |
| Settle in-flight work, then prioritize stop | Existing BusOwner and runner | Queued/setup/drain/receive/wait/observation cases plus HOLD/partial-TX and late fault PASS | Transport read-only regression PASS | Dynamic stop latency NOT RUN |
| Stop independent of move readiness and unrelated metadata | Known-target stop admission without refresh | Stale feedback, alarm, unknown prior execution, missing origin/scales PASS | No automatic refresh/write on rejected gate | Relevant timing gate remains enforced |
| Serial-only use and explicit queue/deceleration semantics | Profile stop policy; no implicit input/ramp changes | No unrelated input prerequisite; unsupported queue/custom ramp fails before work PASS | Configuration frames identical before/after | Unwired does not prove assignment disabled; actual input effects unqualified |
| Separate TX/acknowledgement/execution/completion | Public `ActionEvent`/context and application evidence mapping | FC06 echo alone insufficient; documented versus unknown exception tests PASS | Drive flags recorded without action-effect claims | Independent current/shaft evidence absent |
| Harness retains trustworthy evidence without retry | `Console._check_action`, existing one-attempt commands | 123 Python tests PASS, including contradictory records and valid delayed/failed records | Raw JSONL retained; no replay/recovery/reset | Physical actions remain NOT RUN |

## Verification and independent review

- Strict native Release build with GCC 15.1.0 and `-Werror`: **PASS**;
  **21/21 CTest suites**, including both actual application builds, capture/masked
  service tests, 123 Python harness cases, generated version/register checks and
  13 operation-inventory negative tests.
- Separate generator negative suite: **8/8 PASS**; contrast snapshot verification:
  **5 references PASS**. Installed C++ consumer builds and runs action preparation,
  acknowledgement/wait/cancellation plus existing typed reads/units: **PASS**.
- `bench_s3_units`, `bench_s3_probe`, `bench_s3_load_poll`,
  `bench_s3_load_timer`: **PASS**. Timer linked flash usage 394328 bytes,
  static RAM 29368 bytes. No public layout/storage change.
- Fresh sequencing/uncertainty and stop/scheduling reviewers inspected code,
  failures and vendor semantics in parallel. A third reviewer audited the
  harness/console. The stop reviewer independently checked the final core/harness
  changes they did not author; the root reviewed the actual combined diff.
  Final focused re-review found no remaining confirmed blocker.

During verification, the new app tests initially attempted result release without
registering delivery through the console. Both application binaries reproduced
the assertion; the fixtures now use the real CLI route. Review also corrected
the harness's exception frame error to the actual `FrameError::EXCEPTION` value
10. The full final suite passes after both corrections.

Host orchestration issues were kept separate from firmware behavior: an initial
CTest filter matched no tests and was corrected; PowerShell treated native
stderr as a command error despite successful builds, so the cached four-build
check captured the actual process exit (0). A relative batch path was corrected
to an absolute path. Upload itself completed with verified data; printing its
Unicode output through the host's CP1252 stream then failed. The raw upload log
retains SUCCESS/hash verification. No extra flash or motor transaction was
performed to hide these host issues.

## Final-image bench evidence

COM13 was reidentified as USB `303A:1001`, serial `3C:0F:02:CD:6B:98`, running
MotorControl-RS 0.6.0 protocol 2 with ten correlations. Preflight preserved the
previous image's 56 frames/576 RX bytes and zero failures, load off and empty
owner; no reset/recover command cleared them. The upload installed the audited
20-us GPTimer image, 400064 bytes, SHA-256:

`291bad796db9014f2d475720dd0af412340978060c9d9defa26a1ac302c0e889`

UART configuration remains address 1, 115200 8N1, TX47/RX48/DE21, 200000-us
response timeout, 304-us reviewed bench reply gap and 85-us sample-gap guard.
Original 16-MiB CO2control backup was rehashed and preserved:
`85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.

The campaign completed 28 checked FC03 frames/288 RX bytes: one identity read,
configuration and state before/after, a 37-byte capture reply, and ten model
probes. All five action choices rejected `timing_unqualified`, operation ID 0,
with identical transport counters before/after. Ten additional one-attempt
probes at 50-ms intervals passed. Final totals: **38 frames/358 RX bytes, zero
failures/timeouts/cancellations/capture faults/UART errors**. Raw configuration
and state frames match before/after: model `4EEA`, firmware `0029`, alarm 0,
motion word 1, inputs/outputs 0, position/speed 0. Those flags are drive reports,
not physical action or standstill proof. No motor write or move was sent.

Campaign probes took 5.419–5.577 ms; final configuration 36.058 ms, state
21.225 ms, long capture 8.122 ms. Maximum active-owner gap 136 us, capture gap
55 us, capture high-water 2; no gap guard exceeded. Optional trace overwrites
610 are retained diagnostics, not lost frame evidence. Ending capture section
time 9968916 us / 64101947 us elapsed is 15.55% of one core; the separate
scheduler snapshot reports 0%/6% busy. This short unloaded snapshot is not a
comparative cost experiment or an extension of prompt 04's operating envelope.
The observed cost difference from the older image is not attributed to this
exception fix or claimed as a qualified optimization.

Internal free/minimum/largest: 336656/331496/278516 bytes; PSRAM:
8318508/8318508/8257524 bytes; owner/worker stack headroom: 2132/3268 bytes.
Ending load/monitor off, DE released, no pending/retained/reserved work, axis
conflict, transport recovery need, dropped input or blocked/short console output.

## Remaining gates and handoff

Independent TX completion, DE hold/release, first RX, final stop-bit publication,
idle watermark and FC06 local-echo exclusion remain **NOT RUN**: no independent
TX/RX/DE capture is available. Stopped-state normal/direct stop, deliberate
enable/release and alarm-clear physical checks remain **NOT RUN** behind that
gate. Dynamic stopping, motion, travel and winding-current proof were not tested.
Unsupported custom ramps and device queue guarantees still reject before writes.

Current FieldCore owner/backend/module/CLI were inspected read-only at
`47f52e5519725ee16a43da576f4ba52d78b50551`. Its sensor retry workflow, equal-TX
prefix stripping, eight-byte TX capacity and measurement types remain deliberate
differences requiring a separately authorized motor integration. No FieldCore
file changed. This core keeps framework-neutral supplied events; the application
owns transaction scheduling, echo qualification and operation reservations.

Prompt 09 remains separately dispatched. It receives the existing real action
and usable software stop path, preserved uncertainty/reservation semantics and
the unchanged requirement to establish physical stop/timing evidence before a
live move. Missing equipment permits independent software progress, not a
physical qualification claim.
