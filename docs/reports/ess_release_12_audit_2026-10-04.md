# Prompt 12 fresh audit

Baseline `234bc45357059d225b404d26e0056c91703df6d5`; final revision is the
commit introducing this report. Re-read the entire prompt and execution
contract, reviewed the actual implementation diff and callers, and assigned
fresh independent PDF/access-policy and wire-fixture reviewers. This audit
executes no later prompt. [Structured results](ess_release_12_audit_2026-10-04.json)
and [raw evidence](ess_release_12_audit_2026-10-04_evidence.zip) preserve the
regression reproduction, test/build logs, console traffic and SHA-256 manifest.

## Finding and correction

One latent generator consistency bug was confirmed. An ordinary register
address spelled `0024` passed hexadecimal validation and generated access
permission at `0x0024`, but public constants and descriptive catalogue entries
emitted `0024` verbatim: C++ interpreted that as octal `0x0014`. This was an
existing generator defect adjacent to prompt 12's canonical FC10-window emitter.
The current ledger already uses canonical addresses, so its generated artifacts
and firmware were unaffected.

Both remaining C++ emitter sites now format the parsed address as `0x%04X`.
No new abstraction, register permission or API was needed. The regression strips
prefixes from every ordinary address and indexed base, validates the ledger,
then compares complete public header, catalogue and access-policy outputs.
Running that actual test against the committed generator fails; the corrected
generator passes. A reviewer who did not author the correction independently
confirmed it and reran all 18 generator tests and the generated-artifact check.

## Requirement review

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Same ledger, consistent generated addresses | `registers_header`, `source_file`, `access_header` | Red/green regression; unchanged generated artifacts PASS | Existing image unchanged | None for address spelling |
| Exact windows, no paired FC06 bypass | Generated `Access.h`; all Codec validate/build/parse paths | Exhaustive start/count oracle, paired halves, forbidden neighbors/gaps, CRC/echo/capacity and unchanged outputs PASS | No write attempted | No additional admissible window established |
| All writable pairs and source semantics | Ledger constraints, derived inventory and [pair guide](../ess_pair_writes.md) | 20 writable dispositions, both-order arithmetic, inventory checks PASS | Physical write/readback/restore NOT RUN | Home-offset order, negative encoding/scale, limits/stored wire forms and partial application |
| Catalogue independence and actual callers | Codec-only target, Position preparation, application request validator | Isolated C++11 codec link and installed consumer PASS | Ten read-only probes PASS | Raw arithmetic does not establish device interpretation |
| Narrow experiment and downstream scope | Existing pair experiment; prompts 13/14/16 restrictions | No speculative helper, raw CLI bypass or extra policy found | Experiment NOT RUN | Qualified timing/source, stopped/input state and interpretation prerequisites |

The PDF reviewer freshly rendered original physical pages 8, 16, 18–20, 22,
29–30, 70, 73, 75–76. The root independently checked pages 30 and 73. Original
PDF SHA-256 remains `0ca2d7f6b69404ead3ba9af982076c54f86eb16b1c24ba237f344aac079b81eb`.
The homing offset is absent from page 30's configurable-order list; page 20's
zero-valued example cannot disambiguate its order. No source supports expanding
beyond `0x0024/2`, `0x0021/5`, `0x001D/3`, `0x0031/6`. The current unavailable
typed writes and per-pair proof gaps are therefore retained. Neither reviewer
found another confirmed in-scope defect.

Actual FieldCore source was reinspected read-only at
`7c356aff59132ab7ab7be6e3ec26a599ede6b99e`. Its owner still has eight-byte TX
storage and its task strips a matching request prefix. Those integration
differences remain relevant to FC10 capacity and FC06 source attribution;
no framework types or sensor replay policy were imported.

## Final verification and bench disposition

Strict GCC 15.1.0 Release build with `-Werror`: **27/27 CTests PASS**, including
18 generator tests, 14 inventory tests, 155 Python harness tests, isolated codec,
application/sequence tests and delayed/masked capture tests. Installed C++11
consumer **1/1 PASS**; generated version/catalogue and five offline contrast
references PASS. All four PlatformIO environments PASS. The first build command
used nonexistent `units_preview`; it failed before building, then the corrected
`bench_s3_units` command passed alongside probe, polling-load and timer-load.

No flash was needed: current generated output and the rebuilt timer image are
unchanged, 429888 bytes, SHA-256
`43e3a3abf7d8dea7e5f5f473a149786bfa4da1e697ec0f5762d9455904cb4270`.
COM13 identity was freshly checked: USB 303A:1001, serial 3C:0F:02:CD:6B:98,
MotorControl-RS 0.6.0/protocol 2. Existing wiring TX47/RX48/DE21 and node 1 at
115200 8N1 remain the bench envelope; response timeout 200 ms, reply gap 304 us,
20-us timer capture and 85-us sample-gap guard. Timing qualification remains false.

Ten one-attempt FC03 model probes at 50-ms intervals passed, durations
5461–5609 us, model `0x4EEA`. Cumulative counters advanced from 38 to 48 frames
and 358 to 428 RX bytes; errors/timeouts/capture faults remained zero. No reset,
recovery, parameter write or motion was sent. This short model regression does
not requalify long frames, refresh configuration/state or establish standstill.

Ending load/delay/console workload is 0/0/0, DE released, no pending/retained/
reserved work or recovery requirement. Cumulative owner/capture gaps 143/54 us;
capture high water 2, bounded trace overwrites 780. Capture section time is
151058328/743942934 us (20.30%); scheduler CPU estimates are separately 0%/5%.
Internal free/min/largest bytes 336656/331496/278516; PSRAM 8291884/8291884/8257524;
owner/worker stack headroom 3476/3268 bytes. These are cumulative observations
on the existing image, not a fresh load qualification.

Physical pair writes/readback/restore remain **NOT RUN**: independent TX/RX/DE
and FC06 echo-source evidence, field interpretation and stopped-state/external
input prerequisites are missing. Software audit is complete; no physical family
pass is implied. Prompt 13 may proceed only when dispatched, retaining all
[per-pair restrictions](../ess_pair_writes.md).
