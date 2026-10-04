# Prompt 11: fresh independent velocity audit

Executed only the prompt 11 audit on 2026-10-04 under the execution contract.
Baseline: `86c6d9af96312d3ad2eb3a71154f0a4ad9958f22`; final revision is the commit
containing this report, with its actual hash and push status in the final handoff.
The working tree and upstream were clean before work. The full original prompt,
current contracts, original manual pages and actual implementation diff were
reinspected. Previous reports informed the prerequisites; they did not substitute
for inspecting code. See [API/lifetime/service contract](../ess_velocity.md),
[exact evidence](ess_release_11_audit_2026-10-04.json) and the
[retained archive](ess_release_11_audit_2026-10-04_evidence.zip).

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Exact signed velocity and minimal conversion dependencies | `prepareVelocityTarget` in existing Axis helpers, installed common request/target types | Independent 5,000-case Python Fraction comparison across units, frames, time, polarity, rounding and ranges PASS; unchanged rejection outputs; actual radians checked | Identity/configuration/state remain unchanged | Firmware RPM factor and negative wire encoding unqualified |
| Native ramp snapshots, unsupported acceleration/JOG/features | ESS `prepareVelocity`, reviewed three-word staging and START_SPEED; existing explicit stop | Original PDF pages 17/18/19/25/70 reviewed; existing native/actual application tests PASS | Four velocity gates and finite Python rejection PASS with zero motor writes | Conflicting defaults do not resolve starting speed or physical acceleration formula; no fabricated mapping |
| Bounded lifecycle, uncertainty, deadline and late evidence | Existing `VelocityContext`, `nextVelocity`, `advanceVelocity`, `serviceVelocity`, nested `ActionContext` stop and application owner | Independent lost stop ACK and closure-at-deadline delayed delivery probes PASS; existing actual SDK/Runner cancellation, urgent pressure, recovery and conflict tests PASS | Owner empty, DE released, faults/errors zero | Host eligibility is not a watchdog or guaranteed physical stop latency |
| Ctrl+C with a known admitted request | Existing `Console.wait` and shared finite `move_campaign`/`velocity_campaign` cleanup | Idle interruption sends one explicit stop, routes the original terminal, retains cleanup and releases results; interrupted serial I/O remains unknown/no further commands | Finite rejected/unadmitted scenario sends no cleanup write | Interrupted I/O or broken communication cannot establish stop; no automatic replay/resynchronization |
| Strict evidence and actual API parity | Python `_check_velocity`, actual C++ console formatter and public operation sequencing | Reject impossible precision/tokens/uncertainty/stop obligations and fabricated failure; ten actual C++ operation records pass registered Python parity suite | Current-image gates preserve correlation; physical terminals simulated only | Independent shaft/RPM/acceleration comparison NOT RUN |
| Help/dispatch parity | Existing velocity command entry and public rate units | `counts/s` is now listed and tested; maximum velocity record remains 4442/4608 bytes | Actual `help velocity` includes `counts/s` | No encoder source or motion qualification inferred from reachability |
| Package/platform/source boundary | Installed public headers, one application UART/task/owner, generated ledger unchanged | 26 native suites, 155 Python tests, installed-only C++11 consumer, generator 8/inventory 13, all four firmware environments PASS | Final firmware uploaded with flash hash verified; 19 checked read-only frames PASS | Physical deep velocity stack and electrical qualification remain open |

## Findings, corrections and independent review

Three parallel reviewers inspected units/manual, lifecycle/stop, and
console/Python correlation. They checked actual source and tests and the original
implementation diff rather than accepting the previous summary. The root verified
the findings and integrated the corrections. Independent final reviews found no
remaining confirmed defect in scope.

- Every exception in `Console.wait` previously invalidated framing. Ctrl+C
  during an idle sleep after a complete read now preserves known framing and
  permits the existing finite cleanup to request one explicit stop. Interruption
  during read/write/record consumption remains unknown and ends the command
  stream. Original terminals received while cleanup runs are retained in the
  campaign summary. No command is retried and uncertain execution is not relabelled.
- Python accepted impossible exact/approximate provenance, activity/latest/stop
  tokens, changed evidence under the same token, and failures without a retained
  transaction or legitimate local-time/activity/stop reason. It also accepted
  uncertainty and stop obligations contradicting staging/trigger evidence.
  These records now fail validation; captured late failure evidence stays inspectable.
- Re-review of those fixes caught incorrect enum literals and a missing case:
  real readiness expiry, absence of activity, and finite duration expiry after
  staging but before trigger admission must remain valid terminal records.
  Checks now match actual enum values and phase/time bounds. To prevent synthetic
  fixtures hiding such errors, `probe_console_test --velocity-fixtures` advances
  real public contexts and formats ten real outcomes; a new registered Python
  suite validates those records. No operation result fields are handwritten there.
- The help entry omitted implemented identified-encoder `counts/s`; it now matches
  the parser/API/Python unit vocabulary. No new mode, conversion or device write
  was added.

The core numerical and sequence implementations required no change. Exact rate
preparation still reuses Axis factor cancellation, quantization and approximation;
stop still reuses `ActionContext`. There is no second numeric path, queue, task,
UART, protocol, retry engine or speculative acceleration formula.

Original PDF pages 17/18 describe signed RPM and three native words, pages 18/19
separate external JOG inputs, and page 25 retains pre-start stop ramp settings.
The appendix/default contradictions on page 70 still block exact firmware mapping.
User-confirmed ESS23-RS20 geometry/nominal encoder identity remains recorded;
configured encoder resolution does not prove physical feedback or speed semantics.

Current FieldCore owner, task and CLI were inspected read-only. Its
RequestTransaction/WaitUntil and accepted/retained-result vocabulary are useful;
its eight-byte TX capacity cannot carry fifteen-byte velocity staging, completion
milliseconds are not closure bounds, and sensor prefix-echo/retry behavior is
unsuitable for response-identical FC06 motor ACKs. No FieldCore, generated ledger
or vendor bytes changed. Exact reference hashes are in the JSON evidence.

## Verification and current-image bench evidence

Full verification: **26/26 CTests PASS**, **155 Python tests PASS**, generator
8/inventory 13 PASS, generated version/register and offline contrast checks PASS.
Installed-only C++11 consumer PASS without example includes. All four environments
(units, probe, load-poll, load-timer) compile with the existing warning policy.
The independent exact comparison ran 5,000 cases; ten actual core/console/Python
outcomes include before-TX and after-staging readiness expiry, duration expiry,
absence of activity, service miss, success/radians, stop failure, late trigger
ACK and stop deadline after ACK. Native tests are simulated evidence.

COM13 was inspected as USB 303A:1001, serial `3C:0F:02:CD:6B:98`, UART2
TX47/RX48/DE21, node 1, 115200 8N1, timer capture 20 us/bound 85 us. Preflight
used local queries without reset/recovery and preserved prior statistics: 38
frames, zero errors, load off, DE settled, no retained/pending work. The corrected
host then passed 19 new read-only frames on the existing image before upload.

The help correction was uploaded and flash hashes verified. Final timer image:
429888 bytes, SHA256
`5cf09b507597e1036900aa03fe4e383511b235a55282ec98bdfc573f6eaf5d7c`.
The original 16 MiB backup remains SHA256
`85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.
Original implementation evidence and the previous image remain preserved.

On the final image, identity/configuration/state (nine FC03 transactions),
four zero-TX velocity gates, finite velocity rejection and actual help passed,
followed by the required ten one-attempt model probes: **19 frames/167 RX bytes**.
No timeouts, errors, cancellations, capture faults, discarded or echo bytes.
Identity remains model `0x4EEA`, firmware `0x0029`, address 1/DIP 0; subdivision
1000, configured encoder 4000 and unresolved algorithm 3. Raw alarm/motion
0/1, I/O 0/0, position 0/0 and speed 0 match the prior image. Exact raw read
payloads match across the help upload; no settings or motor writes occurred.

Ending owner/capture gaps: 142/52 us; capture high-water 2, no sample-gap fault.
Internal free/minimum/largest: 336656/331496/278516 bytes; PSRAM
free/minimum/largest: 8291884/8291884/8257524 bytes. Owner/worker stack headroom:
3508/3268 bytes. App/record/console storage and public type sizes remain as
recorded in the implementation handoff; this audit changes no layouts. Runtime
stack measurements cover read-only and gated paths, not qualified velocity.

Ending load 0/0/0, monitoring off, DE released, owner pending/retained/reserved 0,
no axis conflict or recovery requirement, console drops 0. Read-only cleanup
PASS. Physical velocity, acceleration, dynamic stop and independent shaft
comparison remain **NOT RUN**: independent timing/response-source evidence and
exact firmware speed/ramp/input/stop prerequisites remain unresolved. Serial I/O
interruption/link loss retains unknown stop; deliberate live link-loss testing
still needs an independent stop fixture.

Prompt 12 receives the same actual installed velocity APIs, caller-owned lifetime,
immutable deadlines/ramp snapshots and retained uncertainty. No new paired-write
permission or physical qualification follows from this audit.
