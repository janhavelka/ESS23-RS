# Overnight HIL campaign - 5 to 6 October 2026

Status at dispatch: **RUNNING, not a completed endurance PASS**. The user
explicitly requested overnight tests including motion, ending at 10:00 next
morning. This supersedes the earlier direction to defer a several-hour test.
Existing no-replay, finite-movement and fixture limitations still apply.

## Actual session and fixed cutoff

The single-port Python owner started at **2026-10-05 22:00:35 Europe/Prague**.
After measuring recording rate, its first segment ended cleanly after 35 cycles
and six motion cases. The active continuation started at **22:06:52**, retaining
the same fixed cutoff. There was no uncertain motion replay.
The fixed cutoff is **2026-10-06 10:00 CEST / 08:00 UTC**. Both UTC and an
initial monotonic budget constrain the run; a backward clock correction cannot
extend it. Five minutes are reserved for the final explicit stop, fresh
nonrunning/zero-speed observations and exact original motion-profile restoration.
No new work is admitted once that reserve begins. An error ends new workload
immediately. Broken framing or a failed prior stop leaves an unknown cleanup
outcome, with no automatic recovery or uncertain write replay.

The host requests temporary Windows sleep prevention for the process lifetime,
without changing persistent power settings. COM13 must remain exclusively owned
by this process. Host shutdown, power loss or forced process termination cannot
provide verified cleanup. The finite motor requests do not require an endless
velocity loop to continue testing.

Baseline source: `b361a18e21512c5808d811013f37090f237ce1d7`, console protocol 3,
library 0.6.0. No firmware/core/FieldCore source was changed for this experiment.
The current FieldCore `include/TunnelMonitor/rs485/Rs485Task.h` was inspected
read-only; the experiment reuses this repository's application owner.

Recorded uploaded Arduino/GPTimer image:
`build/fast_cli/pio/bench_s3_load_timer/firmware.bin`, SHA-256
`bfa1d01cdfafcea2d72b0ffedcfeaac5dec62eff5d3f625275866f66e1339ea1`.
Its earlier upload verified flash bytes; the console itself does not attest the
image hash. ESP32-S3, Arduino 3.3.11 / IDF 5.5.5, ESS23-RS20 label, model
`0x4EEA`, firmware `0x0029`, address 1, 115200/8N1. Only power and RS485 are
connected; motor secured to table, free uncoupled shaft. Original profile:
`[30,100,100,60,0,100]`. Before this session the drive reported stationary,
alarm zero, raw speed zero; one pre-existing retained terminal result is
preserved rather than silently consumed.

## Workload and guards

The retained experiment runner composes `bench_probe.Console`, `run_recorded`,
`state_health_campaign`, `probe_campaign`, `run_phase`, `observe_stopped` and
the existing profile read/restore helpers. There is one serial connection and
one motor workflow, with strict IDs, CRC/parser evidence and explicit releases
of this campaign's admitted results.

| Work | Selection |
| --- | --- |
| State/health, passive status, memory, stacks, CPU, owner and errors | Approximately every 10 seconds; separate observation ages checked, not rejuvenated by cached queries |
| Presence and reviewed 16-word capture read | One of each per iteration, checked raw/parser outcomes |
| Finite position | One small positive 100-command-increment move per minute at 60 rpm with configured native ramps |
| Dynamic stop | Every ten minutes, alternate normal/fast during finite 250-increment move; retain interrupted outcome |
| Motion cleanup | Explicit implemented stop, bounded fresh zero-speed/nonrunning read, exact saved-profile restore after every case |
| Native configuration coverage | Driver, optional I/O, algorithm/encoder/current/lock, four tuning groups, position/speed records 1 and 16, capabilities and local profile inventory every ten minutes; compare raw settings to the initial backup |
| Competing-task/console load | Ten checked read-only probes every twenty minutes at worker 2000 us / owner delay 5000 us / console 128 bytes; restore load to zero before motion |

Every iteration rejects new transport failures/timeouts/capture faults/RX errors,
discarded bytes, command-input drops, recovery interlocks, unsettled owner/DE,
retained-result growth, stale/invalid/mismatched health or failed refreshes.
Capture gap must remain below the unchanged 85-us guard; owner gap below 15 ms,
owner stack free at least 1024 bytes, worker stack at least 2048 bytes. Compared
with the warmed baseline, internal free memory decline above 8192 bytes or PSRAM
decline above 1024 bytes stops the run. Exact values stream for later trend
analysis; these thresholds do not prove leak freedom.

Raw JSONL evidence streams incrementally and flushes after each record. Capacity
is finite: 3,000,000 records / 2 GiB; exhaustion stops work. The live heartbeat
contains only the latest fixed-size diagnostics and counters. Final JSON contains
bounded last-case evidence, before/after settings and cleanup. Diagnostic display
loss is distinct from protocol/results loss; overwritten trace history remains
visible. No physical shaft angle, temperature or electrical timing is fabricated.

External-switch homing/segment execution, unresolved native families, arbitrary
negative encoding, unbounded velocity, power/restart activation, persistence
cycling, gain sweeps and intentional moving link-loss are **NOT RUN**. The fixture
and existing evidence do not permit an all-native physical PASS. Ordinary serial
motion is exercised without inventing external I/O prerequisites.

## Preflight results and retained failures

| Case | Result |
| --- | --- |
| Initial three-probe/identity/config/state regression | PASS |
| First experiment attempt | FAIL before motion: runner used the wrong nesting for configuration raw values; corrected against actual console record |
| Second experiment attempt | FAIL in final assertion after two successful finite motion cases: pre-existing retained count 1 was incorrectly expected to be zero; stop and profile restoration succeeded, original failure preserved |
| Corrected cutoff/cleanup pilot | PASS: 8 observation cycles, 2 motion cases including dynamic normal stop, 76.109 s, 236 checked frames, zero failures/timeouts/RX/capture errors, profile restored; raw final position 11420 |
| Loaded ten-probe pilot, restored to unloaded state | PASS |
| Dynamic fast-stop pilot with profile restoration | PASS |
| Injected experiment guards | PASS: six tests cover existing-result preservation, failure counters, owner/stack/memory/gap guards, stale health and forward/backward clock changes |
| Existing Python correlation/scenario/motion suites | PASS: 239 / 36 / 37 cases |
| Quick release verifier | PASS: all 15 groups, 77 registered tests, clean source/install consumers and generated/offline/package checks |

The runner corrections changed experiment assertions only. No failed motion
command was replayed to obtain a passing outcome. Failed pilot records remain
separate from the corrected pilot and the ongoing overnight aggregate.

[Preflight evidence, exact experiment source and image](2026-10-05_overnight_hil_preflight.zip)
include a SHA-256 member manifest. This archive precedes the overnight result;
its presence is not evidence that twelve hours completed.
Archive SHA-256:
`dd182da58a3970e1b5c904ec2ae341128edff8754f0edfdbf9090ae80c36858c`.

[First segment and revised experiment source](2026-10-05_overnight_hil_segment1.zip)
retain the clean early stop and exact larger evidence bound. The original
512-MiB estimate was too small at the measured roughly 1 MiB/minute recording
rate; it was corrected before exhaustion. Memory remains bounded and records
still stream directly to disk. Segment 1 had zero transport/capture errors and
verified standstill/profile restoration; it is a finite segment PASS, not a
completed overnight result. Archive SHA-256:
`998deed57d0eda567d5d0d9b7f753b451f0e7cd1d8f480e6ca53416eef37dd02`.

## Live evidence and operation

```text
python build/overnight_20261005/runner.py --until 2026-10-06T08:00:00+00:00 --out build/overnight_20261005/night2 --firmware build/fast_cli/pio/bench_s3_load_timer/firmware.bin
```

`build/overnight_20261005/night2.pid` records the active background PID (36772).
`night2_heartbeat.json` reports running/finished/failed, last update, counters and
diagnostics. `night2.jsonl` preserves exact commands/responses/events;
`night2_stdout.log` records periodic summaries, `night2_stderr.log` exceptions.
`night2.json` is finalized when the owned session closes, including stop and
restoration evidence. Do not treat its reserved empty file as a finished result.

For an explicit early orderly stop, create
`build/overnight_20261005/night2.stop`. The runner finishes already admitted bounded
work, performs its planned cleanup and records `operator_stop_file`; this is an
early-ended run rather than completion of the requested overnight duration.
Do not kill the process or open a competing COM13 connection for inspection.

The hardware aggregate, final bench state and endurance disposition remain open
until the final result is inspected. Publication and broader release/native-family
qualification are unchanged. Hosted CI remains a separate software check; the
baseline IDF job was cancelled without executed steps and has been explicitly
rerun. Exact launch-document commit synchronization/CI is checked at handoff.
At 22:09 CEST, [GitHub's Actions incident](https://www.githubstatus.com/incidents/3q1yb5m7ltvb)
reports delayed hosted-runner assignment. The cancelled baseline job had
`runner_id=0`, no runner name and no steps; this is distinct from a failing
repository test. Launch-document jobs are queued/in progress, not claimed PASS;
checks are retained unchanged. The hardware campaign continues independently.
