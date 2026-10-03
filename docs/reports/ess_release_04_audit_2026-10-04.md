# Prompt 04 — fresh independent audit

Baseline: `16b34815c5c0adc8dec93c5275836bce8097bf32`, clean and synchronized
before audit. Final commit is the commit introducing this report. The complete
original prompt, execution contract, prerequisite dispositions, actual
`c1bddc3..16b3481` diff, callers/tests and raw evidence were rechecked.

Disposition: **software and available read-only verification PASS**. One host
qualification defect and one stale documentation statement were corrected.
No firmware defect or reason to replace the single 20-us sampler was found.
Independent electrical timing remains **NOT RUN**; this audit does not advance
any later numbered prompt or authorize a qualification claim for physical writes.

## Findings and corrections

1. **Qualification could pass with missing capture diagnostics or a sticky
   sampling-gap violation.** `check_load_reply()` ignored `timer_callbacks`,
   `sample_gap_limit_us`, `capture_high_water` and `sample_gap_exceeded`.
   Missing fields, negative/non-integer counters and a true gap flag were
   reproduced as accepted inputs. A fault reported after the last successful
   read could therefore leave the campaign summary successful.

   The existing validator now requires those fields, strict integer/boolean
   types, timer callbacks no greater than all samples, and a positive gap limit
   for timer mode versus zero for polling. A load campaign gathers statistics
   and fails on a sticky gap before its first bus read or after any iteration's
   load snapshot. A well-formed fault report remains inspectable through Console;
   recovery remains explicit. No retry, automatic reset or workload cleanup was
   added. Existing helpers and failure summaries are reused.

2. **One guide sentence still described only a single short read.** It now
   identifies measured seven-byte and fixed 37-byte replies and keeps other
   sizes/windows, electrical timing, shared-bus and motion qualification open.

3. **Added failure-cleanup assurance.** A native test now starves capture while
   an independently scheduled eight-byte TX is still shifting. FIFO empty does
   not release DE; the final stop bit and hold must finish. The fault remains
   latched, a subsequent valid reply remains rejected with unchanged output,
   and another TX requires explicit recovery. Recovery does not replay the
   request. This tests existing correct behavior; firmware was not changed.

Two fresh implementation reviewers audited SDK/timing and app/console/harness
paths in parallel. The experiment reviewer recomputed the original archive
counts, CRCs, CPU ratios, memory sizes and hashes independently, then reviewed
the final corrections without authoring them. The lead checked the findings
against actual source and diff and reran the tests. No remaining blocking
finding was identified. No alternate backend, second owner, core API or generic
framework was added; the small shared capture and read paths remain intact.

## Fresh verification and hardware

All **16 CTest suites**, **81 Python cases**, **8 generator cases**, generated
version/ledger checks and five preserved contrast references pass. All three
PlatformIO environments build: probe, polling load and timer load. The final
native/Python suites were rerun after the corrections. Tools: CMake 4.0.1,
native GCC 15.1.0, Python 3.12.10, Xtensa GCC 14.2.0 and PlatformIO 6.1.19.

Firmware remained byte-identical to the original prompt-04 image; no upload
was needed. SHA-256:
`4c55f961f3894ad57ca49e157eed1421ff1c86255d046eb9ffcc570016338001`.
Initial COM13 inspection confirmed USB 303A:1001, serial `3C:0F:02:CD:6B:98`,
protocol 2/version 0.6.0, no recovery requirement and workload disabled. Existing
operation 142 and counters continued from the previous cleanup; they were
preserved rather than reset to make the baseline pass. The same UART2
TX47/RX48/DE21, address 1, 1152008N1, setup/hold 20 us, reply override 304 us,
gaps 750/1750 us, 500-ms request deadline and two-second recovery deadline apply.
`timing_qualified:false` and `cache_off_supported:false` remain visible.

The stricter final harness repeated the original unloaded, moderate, heavy,
10-ms delay, ten-probe cleanup, 20-ms expected failure/recovery and long-read
cache/retention scenarios. **140 new reads passed: 76 model and 64 long replies.**
One expected pre-TX failure produced TX_TIMEOUT, zero TX bytes,
execution_unknown=false and 40965 us duration. Explicit recovery and later
reads passed. Counters increased by 141 starts, 140 frames, one failure/timeout
and 2900 RX bytes, reconciling exactly with the raw terminal records.

Each principal row has 20 reads. Load tuples are work-us / owner-delay-us /
diagnostic payload bytes. Percentages use each campaign's final snapshot.

| Reply | Load | Capture section % of one core | Scheduler core1 % | Owner max us | Sample max us | Read duration us |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 7 bytes | 0/0/0 | 20.840 | 16 | 130 | 49 | 5436–5540 |
| 7 bytes | 2000/5000/128 | 20.234 | 30 | 8023 | 49 | 14545–17599 |
| 7 bytes | 5000/5000/256 | 20.155 | 56 | 10010 | 53 | 20559–23583 |
| 37 bytes | 0/0/0 | 20.982 | 18 | 137 | 49 | 8041–8113 |
| 37 bytes | 2000/5000/128 | 20.239 | 31 | 8033 | 49 | 17146–20223 |
| 37 bytes | 5000/5000/256 | 20.147 | 56 | 10014 | 49 | 25194–26206 |

Three reads of each size at 10-ms injected delay also passed; maximum owner
gaps were 11000/11032 us and durations 24564–25573/27178–28203 us. Long-frame
ring high-water reached 37. No capture/UART errors or sample-gap violations
were reported. Heavy diagnostic ingress drops were 165/174 in the campaign
snapshots; these remain explicit bounded fixture losses. Runner trace overwrite
increased by 4307; retained JSONL is not a full per-sample waveform history.

Capture section cost remains about 20–21% of one core, approximately 4 us per
sample. Timer callback counts are separate from all task/timer samples.
SDK dispatcher/return and critical-section entry/exit are excluded. Scheduler
CPU estimates may charge ISR execution to the interrupted task and must not be
added to the capture percentage. This remains a measured reference selection,
not a qualified production CPU budget or extended soak.

Final internal free/minimum/largest: 336656/331496/278516 bytes; PSRAM:
8358444/8358444/8257524. Observed owner/worker stack headroom: 4804/3268 bytes.
Unchanged storage: App 27136 Xtensa / 27944 native; actual UART 1704 and load
fixture 4816 bytes. Cleanup: DONE, DE released, workload 0/0/0,
pending/retained/reserved zero, no recovery requirement, no input drops or USB
short writes. Original 16-MiB backup hash was rechecked unchanged:
`85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.

## Requirement disposition and remaining gates

| Requirement / failure scenario | Actual production path | Native/build evidence | Hardware evidence | Remaining proof |
| --- | --- | --- | --- | --- |
| Measured capture choice, interrupts/samples, gaps, failures and memory | Esp32S3Uart captureSample; Esp32Load configure; Python campaign | All suites/builds PASS; strict diagnostics tests | Fresh 140 successful reads and reconciled counters PASS | Production CPU budget, broader load and soak |
| Starvation, ambiguity, overflow and missing evidence fail | Same adapter/Runner; explicit recovery | 84/85-us boundary, delayed/masked 37-byte capture, FIFO/ring, new active-TX starvation PASS | No capture violation observed; expected owner-delay failure/recovery PASS | Physical interrupt/cache-off injection NOT RUN |
| Reviewed non-consuming long frame | Fixed FC03 0x0130/16 via existing codec/BusOwner/validator | Frame/error/lifetime/cache and independent wire tests PASS | 64 fresh checked 37-byte replies PASS | Other windows/intermediate sizes/serial tuples not swept |
| Physical TX/DE/RX timing and echo | LL idle checks and bounded intervals retained | Native assumptions/failure tests PASS | **NOT RUN:** no independent aligned TX/RX/DE capture available | Final TX stop, DE hold/release, first RX, final RX stop/FIFO publication, idle watermark and echo measurements |
| Selected SDK cache policy | config restriction and sticky sampling interlock | Installed SDK/map reviewed; fake masking PASS | Cache-off capture unsupported; physical test NOT RUN | Quiesce owner, settle TX/DE and stop capture before cache-off/flash/sleep |
| Write/motion frame qualification | Future typed operations and prerequisites | No new typed writes | **NOT RUN:** no FC06/FC10 or motion traffic | Relevant timing, operations, units/settings/stop evidence |

Pinned SDK remains pioarduino 55.03.311 / Arduino 3.3.11 / IDF libraries
5.5.5+sha.b774170ff46. The reviewer checked the installed sdkconfig and actual
firmware map: callback/sample code is in flash, dispatcher and time accessor
in IRAM, and GPTIMER_ISR_CACHE_SAFE is disabled. An IRAM dispatcher/internal
object is insufficient for cache-off capture. No restriction was weakened.
The original manual p7/p77 and ledger still support the fixed non-consuming
read window without resolving speed encoding or establishing device identity.

Current FieldCore `ffc9a26bacaaf5da6c627d5d0ae2bdf77d5666da` was reinspected
read-only: HardwareSerial batch RX, uart_wait_tx_done, millisecond owner
deadlines and TX-prefix echo stripping remain deliberate differences. No
FieldCore file was edited. Its newer source does not qualify motor integration.

The [JSON report](ess_release_04_audit_2026-10-04.json) records exact counters,
snapshots and artifact hashes; the committed [raw archive](evidence/ess_release_04_audit_2026-10-04.zip)
retains this audit's experiments, scripts and verification logs. The
[original report](ess_release_04_2026-10-04.md) and its evidence remain historical.
Independent 05–07 work may proceed when dispatched; dependent physical actions
and release claims retain the named gates above.
