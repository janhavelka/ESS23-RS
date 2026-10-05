# Prompt24 fresh audit — Scenario evidence and failure handling

Baseline `57f2a8c29b9cad16df37c2b66263ca96f160d4a1`; final revision is the
commit containing this report. This independently re-reads prompt24 and its
execution contract, actual Python/console paths, tests and implementation diff.
No prompt25 implementation, motor semantics, generated register data or
firmware changes are included. The fifteen named scenarios and eight earlier
native-family gaps remain visible in the [scenario contract](../bench_scenarios.md)
and [complete coverage inventory](../ess_api_cli_coverage.md).

## Confirmed findings and corrections

1. Ordinary successful probes trusted reported parser/model fields without
   checking the raw frames. Reproductions accepted bad CRC, a CRC-valid reply
   from node2 to a node1 request, and a CRC-valid model0 reply reported as model60.
   `Console._check_probe` now checks bounded request/reply bytes, CRC, address,
   function/count, the model0/one-word window, decoded model and conservative
   identity confidence. Successful probe/capture reads also require zero
   parser detail/frame-error. This reuses the existing wire validator; there is
   no second connection or CLI engine. Positive fixtures now contain actual
   raw evidence, including discovery's post-restoration probe.
2. A serial close failure after successful work could leave summary `ok=true`.
   Session ownership now includes closure. A simultaneous scenario/startup
   failure and close failure previously erased the original error; the summary
   retains primary `error` and secondary `owner_error`, and rethrows the primary
   exception with the secondary cause. This covers construction, startup,
   identification, workload, verification and closure. Existing motor cleanup
   failures retain their existing separate outcomes.
3. An interrupted startup read could leave `Console.synchronized=true` and
   allow cached diagnostics to issue traffic after a partial serial line.
   Startup now latches all interrupted reads consistently with ordinary command
   handling. A real Console/fake-port regression supplies partial JSON followed
   by `KeyboardInterrupt` and proves zero writes, skipped diagnostics, no work
   and a failed retained summary.
4. The sequence-index introduction still described24 as unexecuted despite its
   completed row. It now agrees with the recorded implementation disposition;
   later prompts and physical qualification remain separate.

These are harness evidence/failure corrections. No automatic recovery, retry,
uncertain-write replay, new setting default or relaxed firmware admission was
introduced. Prior failed physical attempts remain in the original
[prompt24 archive](ess_release_24_2026-10-05_evidence.zip).

## Requirement audit

| Requirement / failure scenario | Actual production path | Native/build evidence | Hardware result | Remaining proof |
| --- | --- | --- | --- | --- |
| Finite named families, one port owner | `bench_scenarios`, shared `bench_session`, existing `bench_probe` and `bench_motion` campaigns | Scenario selection/count/range/no-TX tests; shared ownership and fixed tails | PASS quick/load/position/stop/discovery subset | Method execution, unsupported native APIs and autonomous velocity retain their owning gaps |
| Default read-only; explicit changing work | Scenario metadata and strict existing command argument preparation | Wrong-scenario arguments, setters in native-read, plans and fixed motion envelope | PASS read-only quick; only explicitly selected finite motion writes | No all-feature sweep or persistence soak |
| Actual workload, raw/parser/result identity | `Console` acceptance/terminal/inspection/release; `_check_probe`, typed evidence and `_reply_status` | 233 serial/feature tests include corrupt raw probe, missing fields, contradictory parser errors and checked exception/no replay | PASS node1 model4EEA raw `0103024EEA0C6B`; ten probes in each selected quick/load run | One responder does not exclude collisions |
| Freshness, ACK vs completion | Typed state/health and existing finite motion evidence | Separate observations, cached ages, old/partial state, unknown stop, delayed terminal cases | PASS fresh alarm/motion/speed, new running/arrival observations; cached ending does not refresh timestamps | Internal drive sample age and feedback physical scaling unresolved |
| Exclusive bounded evidence and resources | Capped `Evidence`, fixed snapshots,16 campaign/32 motion-command tails | Disk/short-write/capacity interlock; LF byte accounting; failed tail eviction | PASS11 exclusive attempts, exact actual bytes; largest488855bytes/908 maximum records in another file | Filesystem loss cannot guarantee writing an artifact |
| Failure, interrupted startup/close, cleanup | Session exception ownership and synchronization latch; same-session stop/profile restoration | 36 scenario/session and32 motion tests; double failures and zero-write interruption regression | PASS normal stop, retained interrupted move and exact original profile restoration | Unknown communication loss cannot prove physical stop |
| Discovery and host restoration | Existing bounded scan; actual original tuple/selection verification | 25 discovery cases; CTest supplies native-console fixture | PASS explicit address1/current tuple, request/result bounds2/2, COMPLETE and normal probes afterward | Alternate tuples and address-collision qualification open |
| Installed/firmware boundary and scope | Existing ordinary timer firmware and typed API routes | 65/65 CTest, ledger/version/contrast checks; three required firmware builds | PASS unchanged image; diagnostic drops independent of protocol | IDF parity and endurance remain later work |

## Current-image bench evidence

COM13 USB303A:1001 serial`3C:0F:02:CD:6B:98`, ESP32-S3 N16R8,
UART2 TX47/RX48/DE21. MotorControl-RS0.6.0/protocol2 with ten command slots.
The supplied575424-byte ordinary `bench_s3_load_timer` image has SHA256
`4ae880946615c4828992f864e983da36417123df39c15c82a045dd38f30487d3`.
No upload occurred. Artifact hash and console identity are separate evidence,
not flash attestation. The motor remains secured with an uncoupled shaft and
only power/RS485 connected, under the existing bounded functional authorization.

Original/final node1,1152008N1 (actual115211), binding generation5, host serial
generation1. Motor raw model4EEA/version0029, direction0/subdivision1000/
word-order0/algorithm3/encoder4000 remain unchanged. No inferred model mapping,
encoder manufacturer, signed physical feedback scale or autonomous ramp formula.
Input assignments1/2/3/0 remain assigned separately from declared unconnected
wiring; outputs0/0 and actual logical I/O levels zero. Both finite scenarios
restore the exact six-word profile `[30,100,100,60,0,250]`.

| Artifact | Result | Exact selected work / measured evidence |
| --- | --- | --- |
| `quick-baseline` | PASS before strict raw correction |10 probes; retained as baseline, not corrected-parser proof |
| `quick-fixed`, `quick-final`, `quick-confirmed`, `quick-current` | PASS |10 probes each plus typed identity/config/state; final probe latency5466..5571us |
| `load-fixed` | PASS |10 probes, CPU1000us/owner delay0/console64bytes, decoded debug; probe latency5959..7103us |
| `position-fixed` | PASS |100 native relative increments at60rpm/configured ramps; raw feedback1654→1754, new completion, stop and restoration |
| `stop-fixed` | PASS |Finite250-increment move; new RUNNING/speed60, normal stop, interrupted move retained; final raw position1961/speed0 |
| `discovery-fixed` | PASS |Address1..1/1152008N1, request/result limits2/2, one request/finding, COMPLETE and tuple/selection restored |
| `ending`, `ending-current` | PASS passive inspection |No motor read/write; owner/results empty, DE released, load/debug/monitor off |

Normal stop's admitted-to-observed-terminal interval117843us (six polls) is an
application/drive-report interval, not independently measured shaft deceleration.
Finite motion is verified using the accepted functional evidence; independent
shaft/electrical measurements are unmeasured. Final checked state alarm0/motion0/
speed0, enabled standstill, raw position1961. `ending-current` preserves the
last explicit state timestamps; block ages1044246..1058248us at passive status
delivery. Repeated cached queries do not rejuvenate them.

The loaded window was captured before restoring host load:309373us actual work
over309 iterations in3090876us, CPU availability true/core0 busy0%/core1 busy26%,
290 synthetic console lines/18 fixture display drops, owner gap1899us/capture55us,
capture high-water7,64-byte read budget. Debug display counters516→606 observed,
413→490 emitted,103→116 dropped, zero diagnostic capture loss. Display loss did
not consume protocol data or retained operation results.

Counters690→1012 checked frames: **322 new frames**, zero failures/timeouts/
capture faults/RX errors/echo bytes; no reset/recovery concealed errors. Final
driver1912 accepted lines/25899bytes (734 new lines), zero dropped input,
blocked output or short writes; pending/retained/reserved0. Final load-window
owner/capture gaps1838/56us are separate from the loaded experiment's window.
Internal free/min/largest336608/331448/278516bytes, PSRAM8177196/8177196/8126452bytes,
owner stack free2036bytes/worker3268bytes. Timer static RAM29384/flash569028bytes.
Character85..89us/capture20us/RTU750..1750us/bench first reply304us and existing
response200ms/request500ms/TX20ms/recovery500ms budgets are unchanged.

## Final verification and handoff

PASS65/65 CTest after the integrated fixes; explicit233 probe,36 scenario/session,
32 motion and18 generator tests. PASS current version/catalogue221 descriptors/
242 words and five contrast hashes; three required probe/poll/timer builds.
Direct discovery tests pass25 cases with the existing standalone native-fixture
skip; CTest supplies that executable and passes the real-console case.

Two independent reviewers inspected serial/correlation and experiment/evidence
against actual source, callers, tests and archived attempts. Lead verified each
confirmed finding, checked the serial reviewer's raw-parser diff independently,
and the serial reviewer re-audited the lead's session/startup corrections.
Final reviews found no further confirmed scoped defect.

FieldCore current HEAD`d19758865852f4e215ea05c6572d060edc4f0c66` RS485 task,
transaction/backend/CLI and serial harness were inspected read-only. Its bounded
request/result ownership and synchronization vocabulary are compatible; its
eight-byte TX limit, byte-identical echo stripping, sensor retry policy and
framework-specific contracts remain deliberate differences. No FieldCore edit.

Prompts25–26 inherit unchanged named scenarios, strict raw success proof, one
serial owner, fixed evidence capacities, explicit recovery and retained primary/
cleanup errors. Earlier native-family gaps, autonomous velocity, absent homing
fixtures, persistence/restart, collision/alternate-tuple qualification and
endurance remain NOT RUN/unqualified, with no narrowed coverage denominator.
See [machine evidence index](ess_release_24_audit_2026-10-05.json) and
[all fresh raw attempts](ess_release_24_audit_2026-10-05_evidence.zip).
