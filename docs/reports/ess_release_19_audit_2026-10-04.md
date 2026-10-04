# Prompt 19 fresh independent audit

Audited the complete original prompt against source, callers and tests at
`1322cfd` on `main`. The final commit is the commit introducing this report.
The audit fixes configuration reconciliation, faithful timer-error settlement
and Python evidence retention. No device settings, core APIs, vendor snapshots
or generated catalogue changed. Prompts 20 and 22 remain unexecuted.

## Findings and corrections

1. A host tuple change clears observation freshness. Four cross-read comparisons
   incorrectly used that freshness marker to decide whether a retained checked
   settings baseline existed. A later full or grouped configuration read could
   therefore miss a changed scale, direction, encoder/control setting or input
   assignment and preserve an invalid origin/prepared target. Comparisons now use
   retained matching target provenance. Readiness still requires fresh evidence.
   Twelve actual-application cases cover DRIVE, CONTROL_SETTINGS and IO in both
   refresh directions, changed/unchanged settings and historical-result isolation.
2. Timer failure fakes returned before INIT/ENABLE transitions, unlike the pinned
   SDK. Adapter cleanup also tracked only successful return codes. With a valid,
   exclusively owned timer, internal enable/disable failure after transition
   could trap explicit repair at the wrong SDK state. Cleanup now tracks the
   transition; the fake and new repair tests reproduce the real ordering. See
   [ESP-IDF 5.5.5 GPTimer source](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.5/components/esp_driver_gptimer/src/gptimer.c).
   Current SDK `b774170ff46` has PM disabled. Synthetic failure reachability on
   the bench, corrupted handles and other SDK/PM configurations are not qualified.
3. Python host queries/refusals did not retain failure, actual baud and capability
   diagnostics for immutability checks. They now retain/copy those fields and
   reject mutations. Read-attempt accounting now includes lost/interrupted
   admission, without replaying a read.
4. The sixteen-tuple tests lacked completed physical-frame schedules through
   the actual application. Thirty-two scenarios now complete checked 7-byte
   probe and 37-byte capture-read replies through the real Runner/owner and fake
   SDK, with tuple-specific framing and a delayed configuration epoch.

The lead verified the findings against actual diffs and reproduced tests.
Three fresh reviewers covered SDK/framing, configuration ownership and
CLI/correlation. Other-author reviews independently rechecked the integrated
fixes; no remaining concrete software defect was reported. FieldCore-node
`6debce7c44af9bc36f00fbcaccdfa47631846cce` RS485 owner/backend/module/CLI were
reinspected read-only. Serialized ownership and per-request baud vocabulary
remain compatible; its 8N1/eight-byte sensor backend and consuming results are
deliberate differences. No FieldCore files changed.

## Requirement and evidence matrix

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Finite reviewed adapter tuples; reject unsupported before mutation | `HostTuple`, `hostTiming`, `Esp32S3Uart::supports/reconfigure` | Sixteen tuples, invalid/no-mutation and unchanged outputs PASS | All sixteen setups/divider readbacks PASS | Successful alternate-tuple motor communication NOT RUN |
| Queued/active/DE and continuation exclusion | `BusOwner::beginConfiguration/finishConfiguration`, application `hostSerial` | Ordinary/urgent/queued work, recovery, monitoring and operation wait exclusion PASS | Settled changes and no TX during tuple matrix PASS | Hardware contention/failure injection NOT RUN |
| Recompute character/gap/capture/transaction budgets | `HostTiming`, adapter `newEpoch`, `Runner::configureTiming` | 10/11 bits; two-stop-bit guard; default-only 304-us exception; 32 complete replies PASS | Per-tuple timings/readback and timer restart PASS | Independent electrical timing NOT RUN |
| Original/requested/active, failed setup/restoration, explicit repair | `Probe::HostSnapshot`, `configurationBlocked`, configuration lease | SDK error stages, buffered old traffic, post-transition repair and blocking PASS | Sixteen setups and explicit original restorations PASS | Physical SDK failure injection NOT RUN |
| Clock/capture continuity and stale traffic | Same adapter timer lifecycle and monotonic clock | Delayed setup and fresh capture epoch, old TX/RX/watermark rejection PASS | Final sampler starvation flag false | External publication bounds NOT RUN |
| Serial generation separate from endpoint/configuration | `Record::serialTuple/serialGeneration`; retained checked target baselines | Twelve cross-cache cases; unchanged scale/origin retained; actual changes invalidate prepared intent PASS | Historical result tuple unchanged through all sixteen changes; new configuration raw values unchanged PASS | Multi-endpoint scheduling remains later application work |
| One adapter and application CLI/API | `Probe::Host::hostSerial`, `host`, `caps`, `set`, `restore`, passive diagnostics | Actual console/application paths and installed core isolation PASS | Exact command/reply evidence; zero console drops/blocked/short writes PASS | Further load/endurance/platform parity NOT RUN |
| Strict finite Python mismatch and retained failure evidence | `Console._check_host`, `host_check_campaign` | Query/refusal/no-op diagnostics, failed admissions, malformed mismatch without retry PASS | Strict NO_RESPONSE-only campaigns FAIL on malformed mismatch traffic; safely stop and report failed restoration | Physical source of malformed traffic unresolved; see below |
| Explicit mismatch settlement; device unchanged | Actual owner failure/recovery and `host restore` | Recovery and immutable retained terminal tests PASS | Deliberate diagnostic recovery/restoration PASS; exact before/after identity/config/state/driver/I/O/control raw data match | Diagnostic restoration does not turn failed strict campaigns into PASS |
| Short existing regression | Same checked ESS probe path | Full native/package/four builds PASS | Ten restored one-attempt probes PASS | Electrical/echo qualification still separate |

## Hardware discrepancy and deliberate diagnosis

COM13 was identified as USB 303A:1001, serial `3C:0F:02:CD:6B:98`. Preflight
retained the previous firmware/settings/fault counters without reset or recovery.
The corrected timer image uses UART2 TX47/RX48/DE21 active-high, 20-us capture;
load and observation monitoring remain disabled. Only power and RS485 connect
to the bolted ESS23-RS20; its shaft remains uncoupled. No motor write was sent.

The first strict Python `host-check` at 9600 8N1 returned LENGTH with TX8/RX1,
raw `F9`, NOT_CHECKED and unknown execution. Its start bound was 64243 us after
physical TX end. The scenario correctly failed, made no automatic recovery or
read retry, attempted restoration once, and reported the owner's recovery
interlock refusing it. The mismatched tuple remained known rather than being
silently reported as restored.

After inspecting host/driver/statistics/load/memory, explicit diagnostic
recovery/restoration succeeded. A deliberate repetition again produced `F9`
(start 64241 us after TX); the next repetition produced NO_RESPONSE. A separate
strict 19200 8N1 run produced LENGTH/RX1 `E5`, again about 64 ms after TX, and
stopped. All failed runs are preserved in the archive. A later passing silence
case does not resolve the discrepancy.

The final diagnostic campaign inspects actual results instead of claiming
NO_RESPONSE: 9600 8N1 gave NO_RESPONSE; 38400 8E1 gave RX_ERROR. Each admitted
read retained its unknown transport result and was explicitly recovered and
restored once, followed by a successful original-tuple probe. Adapter code
counts UART error indications in both `capture_faults` and `rx_errors`; the
38400 mismatch accounts for one of each. Starvation remained false. The exact
UART error bitmap and external waveform were not retained, so parity versus
other UART error, the malformed-byte source and any drive diagnostic response
cannot be established. Their physical root cause remains **unresolved**; a
bounded raw-status/wire measurement is needed. No timeout, retry, default or
production validation was weakened to hide it.

The diagnostic harness initially asserted zero cumulative UART-error counters
after deliberately mismatching parity. That assertion failed after all checks
and cleanup completed. The log is preserved; offline validation distinguishes
the recorded RX_ERROR from sampler starvation and does not claim the harness
or the strict campaigns passed. Required software failure handling and restored
read-only regression pass independently of this unqualified wire discrepancy.

## Current-image results and resources

Firmware `bench_s3_load_timer`: 519120 bytes, SHA-256
`f22dde5482f9c7090682dac6dd6ee21b35a71b4b8276f3146227257f16a1d369`.
Upload verified. Timer build static RAM 29384/327680, flash 512992/8323072 bytes.
The original 16-MB CO2control backup is preserved with SHA-256
`85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.

All sixteen host setups reported actual baud 9600/19200/38401/115211 at each
format. No transactions started during selection. A retained probe kept its
original tuple/generation; restored presence confidence remained invalid until
a fresh checked read. Identity/config/state and driver/I/O/control before/after
reads match exact raw responses. Model/version 0x4EEA/0x0029, node1/DIP0,
stored tuple codes0/0, direction0, subdivision1000, order0, algorithm3 and
configured encoder4000 remain unchanged. Input functions1/2/3/0, outputs0/0,
polarity/custom0 and stationary raw state remain unchanged. These observations
do not resolve the previous firmware/physical-scale limitations.

Ten final probes PASS: 5464–5532 us, mean5494.8 us. Final cumulative started101,
frames95, failed6 and timeouts2 include all preceding retained failed/diagnostic
runs; the final campaign contributes 53 starts, 51 frames and two intentional
transport failures. RX956 bytes; no cancellation, echo or discarded bytes.
Owner/capture maximum gaps163/56 us; capture high-water2, starvation false.
Capture busy33015163 over elapsed188580976 us (about17.51%, cumulative, including
idle diagnosis time). Trace overwrites1850 are bounded optional ring reuse.

Internal free/min/largest336640/331480/278516; PSRAM
8201772/8201772/8126452 bytes. Owner/work stack free3252/3268 bytes.
Target sizes remain App181024, Record7768, Console18448, adapter1720, Runner320,
BusOwner144, HostTuple8, HostTiming60 and HostSnapshot120 bytes. Large application
records/traces remain PSRAM; required driver/capture storage/stacks stay internal.

The final diagnostic campaign logs159 exact console lines, 32 admissions and
870 events. Every command replied. The ending driver snapshot is at its
command155 (cumulative input_lines333/input_bytes5068), followed by four replied
queries. Drops/blocked output/short writes are zero. Ending pending/retained/
reserved0, output empty, DE released, no recovery required. Final host1152008N1,
actual115211, serial generation64, known/unblocked, failure NONE.

## Verification and handoff

Full CTest47/47, installed C++11 consumer without example includes, all four
firmware environments, generator18 cases, operation inventory21 cases,
version/catalogue checks (221 descriptors/242 words), five preserved serial
contrasts and whitespace checks PASS. Python184/184 includes the new observed
malformed-mismatch regression; the count is recorded in the evidence JSON.
No assertion/warning checks were weakened. See [raw current-image evidence](ess_release_19_audit_2026-10-04.json)
and the [verified archive](ess_release_19_audit_2026-10-04.zip) for preflight,
failed runs, diagnosis, firmware/ELF, source/test diffs and checks.

The [host API/lifetime contract](../host_serial.md) remains the real handoff to
20/22. Software implementation and available host setup/restoration/regression
evidence are complete. Strict NO_RESPONSE campaign variability remains FAIL/
unresolved, successful alternate device tuples and electrical timing NOT RUN.
No alternate-tuple communication, generic motor framing guarantee or physical
SDK-error qualification is inferred from setup or native tests.
