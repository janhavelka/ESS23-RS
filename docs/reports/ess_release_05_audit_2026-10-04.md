# Prompt 05 fresh independent audit

Baseline `7ad80bf`, clean and synchronized with the configured upstream after
fetch. Final commit is the commit introducing this audit report. Only prompt 05
was audited; prompt 06 remains unexecuted. Local date 2026-10-04, Europe/Prague;
raw evidence uses UTC 2026-10-03. Production code, generated descriptors and
firmware were unchanged by this audit.

The manual/coverage, decoder/API and application/console reviewers independently
read the actual source, baseline diff, callers and failure tests. The lead checked
their findings against the files, original PDF pages and real bench records.
No confirmed implementation bug or missing prompt-05 behavior was found.

Two contract documentation gaps were corrected: the profile surface omitted the
implemented `ReadOperation.h`, `Reads.h` and `Reads.cpp`; discovery prose used an
obsolete identity name, presented planned common discovery APIs as implemented,
and understated existing hardware evidence. The root/documentation introductions
also now distinguish implemented typed reads from future setting/action commands.

| Requirement / failure scenario | Production path and actual API | Fresh native/build evidence | Hardware disposition / artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Coverage linked to ledger IDs; source/model/read/write/action separate | `ess_rs_operations.json`, `check_ess_operations.py`, existing catalogue expansion | Inventory and 11 negative tests PASS; 221 descriptors/242 words unchanged | Original archive integrity and all ten typed terminals rechecked | 182 read, 193 write and two action-register obligations remain unimplemented; 16 reserved/two unresolved entries and 135 named choices accounted |
| Typed identity/version/node/DIP | `prepareIdentity`, `nextRead`, `advanceRead`, `getIdentity` | Independent literal frames, malformed/exception/unknown-value tests PASS | One fresh identity observation PASS | Exact model, firmware and DIP mapping unresolved |
| Gap-safe bounded configuration and pair-order prerequisite | `prepareConfig`, five FC03 windows, `getConfig` | All windows, gap rejection and unknown enums PASS | One fresh five-window configuration PASS | No paired payload read here; later pair decoding requires known order |
| Wiring separate from assignments and levels; units conservative | `InputWiring`, `ConfigObservation`, existing `UnitConfig` | Copied declarations, unknown codes/bits, zero encoder and conversion refusal PASS | Fresh raw assignment/polarity/encoder data identical | Wiring UNKNOWN; levels NOT READ; no physical encoder/command-scale inference |
| Stored serial separate from active tuple | `ActiveSerialTuple`, raw stored codes | Deliberately differing stored/active tuple test PASS | Host115200/8N1/node1; stored codes0/0 and custom node0 | Pending/save/application state cannot be inferred from readback |
| Portable public headers and no I/O | Installed `ReadOperation.h`, `Reads.h`, `Reads.cpp` | Fresh C++11/-Werror core install and consumer PASS without example include path; four PIO environments PASS | Unchanged timer image | No MCU/RTU/console dependency enters the installed API |
| Target/generation/correlation, deadline and immutable previous observations | Core event envelope and atomic getters, real owner mapping | Wrong tokens, transient buffers, every partial-failure position, original deadline and delayed closure PASS | Retained inspection/release PASS; no automatic retry/recovery | RTU has no wire request ID; indistinguishable delayed replies remain possible |
| Real common/profile CLI routes and retained operation ownership | Existing Console, `typedRead`, `admitStep`, `advanceReads` | Actual SDK app/console paths, pressure, cancel/recover, no replay, help parity PASS | Fresh common routes PASS; previous profile/loaded evidence independently revalidated | This audit does not requalify electrical timing or advertise physical stop |

## Verification repeated

- Release C++11 build with `-Werror`; all 19 CTest suites PASS.
- Explicit Python checks: 91 bench transport tests, eight generator tests,
  11 inventory tests, inventory expansion, version/register generation and five
  preserved serial contrasts PASS.
- Fresh clean core install and exported-package consumer configure/build/run
  PASS. An independent reviewer also compiled the read tests and installed
  consumer with only installed headers/archive.
- `bench_s3_units`, `bench_s3_probe`, `bench_s3_load_poll` and
  `bench_s3_load_timer` PASS; current image hash is unchanged.
- Independent evidence audit verified all 35 original archive entries, nine
  raw logs, archive/image hashes and all five identity/five configuration
  terminals against the current strict Python validator.
- Final integrated documentation/evidence review and whitespace check PASS.

The current read-only FieldCore reference was reinspected at
`6730003f001b0c5f953ea73b5d430d7e971307df`: `Rs485OwnerTransaction.h`,
`Rs485Task.cpp`, Arduino backend, Shzk/VibWire modules and CLI submission path.
Copied eight-byte requests, owner result reservations, checked expected-target
parsers and staged publication are compatible conventions. Its framework types,
measurement payloads, sensor retry policy, borrowed RX and millisecond completion
time are deliberate differences from this portable core and retained qualified
closure evidence. FieldCore was not edited.

## Fresh COM13 verification

USB `303A:1001`, serial `3C:0F:02:CD:6B:98`; protocol2 MotorControl-RS0.6.0 was
inspected before traffic. The existing `bench_s3_load_timer` image remains:
SHA-256 `33d46cb038f13162ca458532af38fa3cf49ba856aae17107197cd4c8af4d5b2a`,
360816 bytes. No upload, reset, recovery or motor write was needed. Settings
remain UART2 TX47/RX48/DE21 active-high, node1,1152008N1, timer20us, sample-gap
limit85us, setup/hold20us, reply-start304us, final gap1750us, response200ms and
absolute operation500ms. Timing qualification and cache-off support remain false.

Commands: local preflight `version/config/load/drv/memory/stats`, one
`typed-read --kind both` campaign with immutable retained inspections and explicit
release, ten model probes (`stress --count 10 --interval 0.05`), then local
cleanup inspection. Load was already0/0/0 and remained disabled. All16 fresh
transactions/138 RX bytes passed; cumulative counters advanced from44/468 to60/606
with zero failures, timeouts, capture faults or RX errors.

| Read | Exact TX | Exact RX | Raw evidence |
| --- | --- | --- | --- |
| Identity | `0103000000044409` | `0103084EEA00290001000057A3` | model4EEA, version0029, active node1, DIP0 |
| Direction/subdivision | `010300100002C5CE` | `010304000003E8FA8D` | 0,1000 |
| Stored serial | `010300130003F40E` | `0103060000000000002175` | custom node0, baud0, format0 |
| Limits/order | `010300170003B5CF` | `0103060000000000002175` | stop0, soft-limit0, word-order0 |
| Input configuration | `010300400005841D` | `01030A00000001000200030000BDB6` | polarity0; functions1,2,3,0 |
| Algorithm/encoder | `010301000002C5F7` | `01030400030FA00FBB` | algorithm3 UNKNOWN; configured encoder4000 |

Identity/configuration application start-to-service was7507/34900us; host command
completion15/94ms. Probe transaction durations5433..5582us, mean5469.6us.
Capture gap maximum53us and active owner gap143us are cumulative diagnostics;
the capture-gap flag remained false. Internal free/min/largest
336656/331496/278516 bytes; PSRAM8325676/8325676/8257524; owner stack minimum4340
and worker3268 bytes. Command input drops remained0. No isolated load/CPU or
electrical qualification claim is made from this unloaded audit.

Ending state: workload0/0/0, DE released, pending/retained/reserved0, no bus fault
or recovery requirement. The immutable previous observation and cancellation/
failure paths are simulated native evidence; these hardware reads establish only
successful non-changing observations under the recorded configuration.

The [audit JSON](ess_release_05_audit_2026-10-04.json) retains complete fresh
terminal records, before/after diagnostics and SHA-256 manifests. The
[audit evidence archive](ess_release_05_audit_2026-10-04_evidence.zip) retains
four raw JSONL runs and the fresh Python/install logs:16036 bytes, SHA-256
`9f55214a4aba1a4f635f1dbb903392ebb29caaaca952b59937fd46973502ec88`.
The original implementation archive and vendor files are preserved.

## Handoff

The actual installed APIs, lifetimes and sequencing remain those in
[the read contract](../ess_reads.md) and the original prompt-05 report.
Prompt06 can consume target/generation-tagged identity/configuration provenance;
07-09 still need reviewed state, units, limits, enable/stop and motion contracts.
Model4EEA/version0029 and algorithm3 remain unmapped. A configured encoder4000
does not identify its manufacturer or prove physical resolution. Missing physical
label/firmware identification, subdivision interpretation, input levels/wiring,
specific input arbitration and independent electrical qualification remain open.
