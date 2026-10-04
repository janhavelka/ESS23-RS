# Fresh prompt16/17 completeness audit — 2026-10-04

Baseline: `1139800`; final audited commit is the commit containing this report.
This dispatch rereads both full prompts, their execution contract, sequence index
and requirement coverage map. It executes no later numbered prompt.

## Findings and corrections

Three reviewers independently inspect source/layout/model semantics, actual
API/application ownership and failure/test evidence. The lead verifies findings
against production code and the integrated diff rather than earlier summaries.

1. **Late checked write certainty:** `advanceDriver` promoted a checked echo or
   device exception to ACKNOWLEDGED/REJECTED before applying the write deadline.
   A portable caller supplying a qualified late exception could therefore get
   DEADLINE with uncertainty incorrectly cleared. The real Runner already
   rejected that closure; the public operation and Python validator now require
   closure within the same retained write budget before either promotion.
   Late bytes/parser status/source remain retained, accepted TX remains UNKNOWN,
   effects remain invalidated and no next write is yielded. On-time closure
   delivered after the deadline retains its correct evidence classification.
2. **Optional-host help parity:** `control` was absent from the shared callable
   gate, so help could advertise it without `Host::startDriver`. Help, caps and
   dispatch now use the same host availability boundary, tested with no handler.
3. **Storage documentation:** segment documentation presented prompt16 ABI sizes
   as current after prompt17 enlarged shared contexts. Historical sizes are now
   labelled and current sizes linked; caller-owned storage uses installed types.

The corrections add no second sequencer, queue, conversion path, framework types,
retry, generated schema or universal model/current default. Source PDFs, generated
Types/Registers/access descriptors and board configuration remain unchanged.

## Requirement disposition

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| 16: all16 PT target/scalar records and reserved words | `Segments.h`, `prepareSegmentRead`, generated indexed helpers; `prepareSegmentSettings` validates whole candidate | All indexes/edges, pair orders, unchanged outputs, reserved/access denial | All16 checked reads PASS in this image | PT pair write UNSUPPORTED: no reviewed FC10 window; reserved slot never written |
| 16: all16 PV and shared PT/PV start fields | Same bounded DriverContext, one helper per actual layout | All layouts/index bounds/native ranges | All16 of each layout read PASS; prior PT1/PV16 scalar restoration PASS retained separately | Negative PV/shared-start wire encoding unresolved; shared-start write FAIL retained |
| 16: relative/absolute external choice and PV trigger policy | Existing typed DRIVE settings, `EXTERNAL_POSITION_MODE`/`PV_TRIGGER_MODE` | Enum/mask/precondition tests and same CLI/API path | Read-only current drive settings PASS | No invented serial start or PV timing rule from PT prose |
| 16: capacity versus input selection | Public/CLI capacity16, maximum input selections8 | Formatter/Python strict semantics | Actual record storage reads PASS | External selection/trigger fixtures NOT RUN |
| 16: bounded grouped update, partial/cancel/no-TX | `prepareSegmentSettings`, `nextDriver`, `advanceDriver`, same-axis owner reservation | Exact candidate/trigger inhibition, readback, partial, cancellation, deadlines and pressure tests | No segment write in this audit; prior narrow scalar evidence unchanged | Triggered execution absent wiring/qualified physical-stop envelope |
| 16: storage access independent of fixtures | `startDriver` maps declared wiring separately from assignment/levels | Unwired/unknown/active-trigger admission tests | Current X assignments remain1/2/3/0, levels0, only power/RS485 connected | No automatic input reassignment |
| 17: eight native typed read/write routes | `ControlSettings.h`, `prepareControlRead`, `prepareControlSettings`, `getControl`; shared candidate/event sequencer | Full8field/16step storage, ranges, malformed/unknown/partial evidence | All8 raw reads PASS; delay stored update/restoration PASS | Other app writes require unresolved independent qualifications |
| 17: exact-model limits and dependencies | Copied identity/raw source, model/effects qualifications; whole and every intermediate current tuple validate before TX | Wrong model/version, zero scale, ranges, stale context and unsafe intermediate candidate no TX | Unknown algorithm3/current5600 retained; eight combined no-TX gates PASS | RS20 peak dial rating is not0102 effective-current limit;2042 prose is not an ESS alias |
| 17: effects/reference/prepared invalidation | Actual `startDriver`/`invalidateDriverAssumptions`/cache reconciliation | Encoder source/origin/configuration generations and immutable historical results | Settings/state unchanged after restoration | Configured counts do not identify an encoder or prove physical accuracy |
| 17: settings versus torque/current motion | Public control observation, caps and same parser path | Unsupported capability and optional-host help/dispatch tests | No current/mode/feedback change sent | Common commanded torque/current remains unsupported |
| Both: closure/deadline uncertainty | Shared `advanceDriver`; Python `_check_driver` | Late echo/exception, earlier readiness expiry, partial TX/partial update expiry, on-time delayed exception | Normal path PASS; injected late-response defects verified natively | Electrical response-source qualification remains separate |
| Both: actual CLI ownership/recovery/retention | One BusOwner and `ProbeConsole`, real SDK application paths | Full result pressure, interrupted control and segment generations, repeated recovery and immutable results | Owner empty, DEreleased and recoveryfalse on final bench | Clearing transport cannot replay old grouped work |
| Coverage and public boundaries | Ledger-linked operation inventory, installed core-only public headers | Native/package, generation and inventory checks | Named image/log retained | No edits to FieldCore or later prompts |

The software obligations are implemented with the explicit source guards required
by the prompts. They are **not fully hardware-qualified**. Unsupported wire forms,
contradictory firmware semantics and missing external fixtures stay in coverage;
a compilation result or a generic raw write does not complete them.

## Source and integration recheck

Original physical function-manual pages22–24 and75–78 were visually reinspected:
PT reserved layout, PV/start records, selector capacity, PT-only hold timing,
algorithm1/2, raw encoder range, effective/current percentages and delay bounds.
The current official [ESS23-RS20 product page](https://www.omc-stepperonline.com/ess-series-2-2nm-311-55oz-in-nema-23-integrated-rs485-closed-loop-stepper-servo-motor-24-48vdc-1000ppr-ess23-rs20)
still specifies output **peak** current1–4A and links V1.0 references. No reviewed
firmware0029/algorithm3 mapping, effective-current ceiling or percentage correction
was found. Live PDF downloads reject access; no replacement bytes are claimed.
Preserved original files and source hashes remain intact.

Current FieldCore `Rs485Task.cpp`, `Rs485RuntimeIngress.h`,
`ArduinoRs485Backend.cpp` and RS485 CLI were inspected read-only. Serialized
request admission, bounded completion reservations, cancellation and passive
snapshots are compatible vocabulary. This installed core has no firmware ingress,
measurement payload or product types. The example's copied expectations,
non-consuming result inspection and checked physical settlement/echo uncertainty
remain deliberate differences from that consumer's sensor workflow.

The shared-start failure is neither fixed nor hidden: original index1 native0-to1
received a source-unconfirmed FC06-shaped frame, then confirmed readback0.
The exact previous raw attempt remains retained; this audit only rereads starts.
Another write is not automatically sent to obtain success. Missing independent
response-source/device-acceptance or exact firmware evidence still prevents a
root-cause conclusion.

## Corrected-image COM13 evidence

[Raw snapshots, traffic and exact console lines](ess_release_16_17_audit_2026-10-04.json)
include old-image preflight and corrected-image results. USB303A:1001,
serial3C:0F:02:CD:6B:98; node1,1152008N1; UART2 TX47/RX48/DE21,
20us timer capture,85us gap guard,304us reply gap and200ms response timeout.
Preflight confirms idle/DEreleased/recoveryfalse, load and monitoring off.
Upload verifies the **497776-byte** image SHA256
`25dd6318901e6804f1168e3dbef422063a4ee380b503238e5fc9bf632005a49d`.
Original firmware backup remains intact.

All48 indexed records were read: every PT raw logical field array is
`[120,100,100,0,5000]`, every PV `[100,100,100,0,0]`, every shared-start
`[0,0,0,0,0]` (the last zeros are formatter-unused fields). Actual read spans
exclude PT reserved words. Identity4EEA/0029/node1/DIP0, control
`[3,4000,5600,100,40,100,40,200]`, drive/I/O/config and independent state blocks
remain unchanged afterward. State alarm/motion0/1, input/output0/0 and raw
position/speed0 are drive observations, not an independent shaft measurement.

The justified stopped lock-delay200-to201ms update and explicit restoration200ms
have separate checked FC03 readbacks201/200. Their admission-to-delivery durations
are15272/15213us. FC06 raw frames`0106010700C9F9A1`/`0106010700C83861`
remain ACKfalse/source-unconfirmed/executionUNKNOWN; only stored-word settlement
is PASS. No active lock/torque/persistence inference occurs. Eight no-TX source/
range gates and ten one-attempt existing model probes pass. No uncertain
shared-start write, mode/encoder/current change, input reassignment, clear,
save, external trigger or motion request is replayed.

Totals **114frames/1206RX bytes**, errors/timeouts/capture faults/discards/input
and output drops0. Owner/capture max gaps274/54us, high-water2. Final pending/
retained/reserved0, output queue0, DEreleased/recoveryfalse, load/monitoroff.
The short aggregate capture cost is16.1% over this named24.25s interval; it is
not new electrical or extended performance qualification.

Current ABI bytes: App175768, frontend Record7472, Console18432,
DriverContext3456, DriverRequest104, DriverPrerequisites1192,
SegmentObservation152, ControlObservation256.
Larger contexts/cache/results/trace stay PSRAM; required driver/capture memory
and stacks stay internal. Internal free/min/largest336656/331496/278516;
PSRAM8209964/8209964/8126452; owner/worker stack headroom3428/3268bytes.
These are measured named-image values, not portable storage promises.

## Verification and handoff

Final integrated Release/C++11 native rebuild retains assertions and
warnings-as-errors: **42/42 CTest PASS**. This includes new late-evidence,
full-pressure actual CLI recovery, segment continuation and strict Python
certainty tests. Installed core-only consumer **1/1 PASS**, with no example
include path. Units/probe/poll/timer PlatformIO builds and corrected timer upload
PASS. Generator18, existing Python transport167, inventory failure20 and
five preserved serial-contrast references PASS; version/descriptors and
`git diff --check` PASS. Actual control fixtures remain21, with extra synthetic
timing corruption variants; these do not claim physical late-response injection.

The audit preserves all existing public APIs and console retention/correlation
limits. Prompt18 remains undispatched. Physical trigger/torque/encoder/active-lock
qualification, PT pair permission, negative speed encoding, shared-start acceptance
and exact current semantics remain explicitly open.
