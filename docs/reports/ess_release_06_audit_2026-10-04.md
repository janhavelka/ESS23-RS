# Prompt 06 fresh independent audit

Audited the complete [original prompt](../prompts/ess_release/06_state_feedback_and_health.md), production source, callers, tests and implementation diff `026f391..e4f608d`; baseline `e4f608d`. The final commit is the commit introducing this report. Only prompt 06 and bounded corrections to its existing path were executed. Software: **PASS**. Available stationary read-only COM13 checks: **PASS**. Electrical, physical encoder, motion and stop qualification: **NOT RUN**.

Exact records, configuration and measurements are in [audit data](ess_release_06_audit_2026-10-04.json). The [evidence archive](ess_release_06_audit_2026-10-04_evidence.zip) contains raw logs, image, source snapshots and a SHA-256 member manifest. The [original implementation report](ess_release_06_2026-10-04.md) remains historical evidence, unchanged.

## Findings and corrections

Two confirmed defects were reproduced and corrected:

1. Cached feedback retained its original configuration interpretation after a newer successful configuration read, but exposed no separate confidence disposition. The raw observation and its age were correctly preserved. `ProbeConsole::stateCache` now emits `current_config_operation_id` and `interpretation_current` for feedback. Any newer successful matching configuration observation requires another state read before interpretation becomes current; an unknown algorithm remains unresolved even then. Original values, configuration provenance, target generation and timestamps remain immutable. Native application tests exercise an actual word-order/algorithm change with the same target generation. Hardware exercises unchanged configuration readback with a new observation ID, loss of interpretation confidence and restoration by an explicit state read.
2. Python checked terminal state strictly but accepted contradictory cached state fields, including wrong enabled polarity, unknown masks and fabricated raw encoder counts; a missing alarm flag could raise `KeyError`. A shared bounded state-block checker now validates both paths, including provenance, types, raw/decoded consistency and interpretation confidence. Malformed cache records raise `BenchError` and poison the session without retry or replay. Cached records do not carry word order, so validation accepts either possible unsigned pair order rather than inventing provenance.

Stale README/architecture/profile summaries and the statement that logical input levels were unobserved were corrected. No generated enums, register ledger, vendor bytes, public ABI, cache layout, task, queue or transport policy changed. The public read-event handoff documentation now explicitly requires qualified complete request transmission and response framing. A partial physical TX cannot reach FRAME in the real runner: its pending TX_ERROR is settled before RECEIVE; this was a reviewed precondition, not a production defect.

## Requirement and failure coverage

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Reviewed alarms, flags, logical I/O; released polarity and unknown bits | Installed `ESS_RS::prepareState`, `nextRead`, `advanceRead`, `getStateBlock`; existing checked parser | Original PDF table rechecked; unknown alarm/bit and malformed/exception cases PASS, 14 read groups | Repeated alarm 0, motion 1, input/output 0; stationary log PASS | Logical flags do not measure voltage, wiring or external input effects |
| Raw speed and paired position with explicit unresolved reasons | Copied exact configuration in `ReadContext`; unsigned pair decoder | Both orders, unknown algorithm, absent/mismatched configuration and immutable outputs PASS | Position words 0/0, speed 0, algorithm 3 retained raw PASS | Actual/commanded source, sign and physical units unresolved on this firmware |
| Per-block attempts, validity, generation, success, conservative age; failed refresh retention | Application `StateCache`, request-start lower bound, closure upper bound, separate delivery | Partial refresh, old generation/evidence, delayed timer service, separate ages and passive queries PASS | Repeated queries preserve values/bounds; loaded delayed delivery PASS | Exact drive sample time undocumented; three reads are not an atomic snapshot |
| Configuration interpretation confidence independent of raw freshness | Feedback wrapper in cached `status`/`health` | Same-generation configuration change leaves old feedback immutable but interpretation non-current; fresh state restores it PASS | New config operation 7 invalidates old interpretation; state operation 8 restores it PASS | Known interpretation alone does not resolve sign, scale or readiness |
| Communication, alarm, readiness and operation outcome remain separate | `observeCommunication`, cached Console reports | Identity cannot refresh state; checked exception refreshes communication only; recovery history non-current PASS | Communication fresh, alarm clear, readiness unresolved PASS | Reads cannot resolve an uncertain write or prove motion completion |
| Shared routes, bounded opt-in polling, consuming fields excluded | `read state`, profile state route, `health check`, `serviceMonitor`; existing BusOwner | Disabled monitoring, pressure, cancellation, urgent scheduling, malformed frames PASS; 41 app / 43 load groups | Three finite polls, 3 admitted / 0 rejected / 9 frames; automatic release and monitoring off PASS | No physical stop or unbounded urgent-flood fairness claim |
| Strict host correlation and cached/terminal checking | Shared Python state checker and `state-health` | 103 cases, corruption matrix and failed-session no-replay PASS | 41 fresh cached and 9 foreground typed terminal records independently revalidated PASS | No automatic retry/recovery |
| Framework-neutral bounded API and platform storage | Existing ReadOperation/Reads public headers; application-only cache/console | Installed C++11 consumer without example include path PASS; 19 CTest suites and all four PIO environments PASS | Actual image sizes, RAM/PSRAM and stack measurements retained | Other MCU/platform qualification remains separate |

The source reviewer rechecked original ESS function manual physical pages 68/69/77 and hardware manual physical page 12. Bit 4 set means released, clear means enabled. Alarm values 0/1/2/3/5 have reviewed meanings; raw 4 remains an unnamed error. Unknown masks FF80/FFF0/FFFC are preserved. Open-loop command position and reviewed closed-loop subdivision-equivalent feedback are distinct from raw encoder counts. Configured 4000 counts neither identifies the encoder nor proves physical resolution. Raw model 4EEA and firmware 0029 remain historically observed, unmapped values; they were not newly read in this audit.

The six existing state ledger IDs remain the coverage source. Inventory accounting is unchanged: 25 implemented reads, 176 unimplemented reads, 193 unimplemented writes and 2 unimplemented actions; 221 records, 16 reserved, 2 unresolved access and 135 named choices. No address source was added.

## Verification and bench evidence

The root inspected the integrated diffs and reproduced the focused tests. Three parallel reviewers independently covered source/bitfields/API, application cache/health/polling, and Python/correlation. Reviewers cross-audited the other author's final corrections against actual code and tests; no further confirmed defects remained.

Software checks: full 19-suite CTest PASS with Release C++11 and `-Werror`; final Python 103 cases PASS, generator 8 cases PASS, inventory 11 cases PASS, version/register generation and offline contrast checks PASS. The full-suite log predates the last added Python regression (102 cases); the focused final Python/CTest log records all 103. Fresh installation and exported-header/archive-only consumer PASS. `bench_s3_units`, `bench_s3_probe`, `bench_s3_load_poll` and `bench_s3_load_timer` builds PASS. Source/API layouts were unchanged, so no package/version bump was required.

COM13 USB 303A:1001 / serial `3C:0F:02:CD:6B:98` was checked before flashing. Old-image preflight is retained separately from new-image evidence. Current timer image: 369664 bytes, SHA-256 `a7582f50b32bd1aa289b775aa620739d31d61dbdd6a15887fa68cf0f39b6708c`. Pins TX/RX/DE 47/48/21, active-high DE, node 1, 115200 8N1; timer 20 us, sample-gap limit 85 us. Existing turnaround exception remains bench-specific and electrically unqualified. Original CO2control firmware backup is preserved separately.

The current-image campaign sent **50 FC03 frames / 470 RX bytes**: two configuration operations (10 frames), seven foreground state operations plus three private polls (30 frames), and ten model probes (10 frames). Five repeated state/health checks, confidence invalidation/restoration, finite polling, a loaded state read and ten-probe regression all passed. No settings or motion command was sent. Direction 0, subdivision 1000, word order 0, input polarity 0, assignments 1/2/3/0, algorithm 3 and configured encoder 4000 read back unchanged. Zero position and speed, running false/in-position true/enabled true support the stationary protocol baseline; no independent shaft measurement was performed.

Board operation latency (serviced minus started, excluding host USB delivery) for unloaded foreground state was 21.181–21.279 ms; loaded state operation latency 65.687 ms. Maximum loaded closure-to-delivery delay was 4630 us. The explicit loaded window lasted 325955 us with worker budget 2000 us, owner delay 5000 us and console budget 128: 28 console lines accepted / 4 dropped, worker 64046 us / 32 iterations, capture section 51561 us / 16510 samples / 16297 timer callbacks. This capture-section fraction is not a whole-CPU utilization measurement. Maximum owner gap 8023 us; maximum capture gap 50 us, below the configured 85 us threshold; capture high-water 11. No sample-gap failure or transport/UART error occurred.

Ending state: 50 starts / 50 frames, zero failed/timeouts/cancelled/capture/UART errors; monitoring off, load 0/0/0, DE released, pending/retained/reserved counts zero, no recovery required, zero output blocked/short writes/input drops. Optional bounded trace overwrote 842 old events as designed. No retry, host recovery or statistics reset was used to obtain a passing result.

| Object / resource | Native bytes | Actual Xtensa bytes / bench watermark |
| --- | ---: | ---: |
| App | 65040 | 62200 |
| Frontend record | 928 | 752 |
| Console | 9960 | 9680 |
| ReadContext | 864 | 696 |
| Identity / Configuration | 200 / 992 | 168 / 832 |
| StateCache / StateObservation | 936 / 232 | 840 / 200 |
| Internal free / minimum / largest block | — | 336656 / 331496 / 278516 |
| PSRAM free / minimum / largest block | — | 8323628 / 8323628 / 8257524 |
| Owner / worker stack headroom | — | 3012 / 3268 |

The widest exercised serialized status is 3850/4096 bytes and health 3685/4096; configuration remains 3562/4096. Existing eight retained foreground records and one private finite-poll record remain bounded; polling accepts 100–60000 ms and 1–1000 attempts and defaults off. All owner/cache methods remain in the one cooperative application context; worker tasks do not call them. App/retained storage remains PSRAM and required capture/driver/stack storage internal.

## Integration and handoff

Current FieldCore source was inspected read-only at `47f52e5519725ee16a43da576f4ba52d78b50551`: RS485 task/module/backend and Shzk/VibWire/CLI callers. Compatible vocabulary remains request/work/result, task-owned transport and retained observation. Deliberate differences remain the portable callback/checked ESS validator, absolute deadline and raw retained evidence, application block caches, separate freshness and operation outcomes, no imported firmware module types or measurement retry policy. FieldCore has no motor device to integrate in this step; its sensor-oriented TX/echo conventions are not evidence of ESS compatibility.

The installed public preparation/event types and lifetimes remain those documented in [typed reads](../ess_reads.md): caller-owned bounded state; copied request expectations/configuration and retained response evidence; supplied time/events; yielded work with no I/O. Later consumers of paired feedback must check both raw observation generation/freshness and `interpretation_current`, then resolve source/sign/scale prerequisites. Preserve separate block ages, partial failure history and communication evidence. A successful read cannot establish readiness, physical standstill or completion, and cannot clear an uncertain operation.

Prompt 07–09 prerequisites receive real typed state evidence and explicit limitations. Algorithm 3 interpretation, firmware/model identification, speed units, signed position and physical scale remain unresolved; electrical timing, physical encoder, motion, stop and completion qualification remain **NOT RUN**. No later prompt was started.
