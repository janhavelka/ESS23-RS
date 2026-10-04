# Regular APIs and passive RS485 observation

Baseline `8260081`; final revision is the commit introducing this report.
The user's direction removes the separate functional-test mode and assigns
correct wiring/electrical installation to the user, while firmware owns command
semantics, sequencing and software timing. This block also implements passive
raw/translated observation during ordinary traffic. It does not execute a new
numbered prompt or the deferred several-hour soak.

[Structured evidence](regular_api_sniff_2026-10-04.json) and
[raw archive](regular_api_sniff_2026-10-04_evidence.zip) retain the exact image,
source snapshots, native/build logs, preflight and timestamped console traffic.

## Changes and audit

- Removed `MOTORCONTROLRS_FUNCTIONAL_BENCH`, its firmware environment and both
  public experiment switches. Ordinary actions and finite positioning can
  observe state after a valid fully transmitted FC06 whose source is unconfirmed,
  retaining UNKNOWN execution. Invalid/partial/late transactions still fail;
  confirmed FC10 staging is still required before the trigger.
- Native absolute targets use `preparePosition`'s actual metadata dependencies.
  No current-position reference is required merely to encode an absolute native
  target. Endpoint and displacement knowledge remain separate; provided
  references still have freshness/stationary checks, and conversion, path and
  limit requirements remain intact. The example preserves its small envelope
  and supports ordinary absolute targets0..250, not a special zero experiment.
- Added typed `ESS_RS::PositionProfile` read/parse/staging helpers. Ordinary
  `motion-profile read|inspect|restore` retains original values, endpoint binding
  and exact readback. No duplicate arbitrary-register path or hidden trigger.
- The board declares combined DE/~RE with RX disabled during transmit. The
  normal application maps a successful checked owner response with physical TX
  completion and qualified closure to confirmed response provenance under that
  declared wiring contract. Runner/adapter checks reject pre-release, early or
  ambiguous RX. No analyzer flag blocks admission; acknowledgement still does
  not prove completion. Alternate transports can retain unconfirmed provenance.
- Installed `TrafficCapture` provides fixed caller-owned copies, independent
  non-consuming cursors, overwrite/drop counters, accepted-TX/RX bytes and
  software TX-end/direction/end events. Runner hooks use the existing read/write
  path; no second reader, UART call, allocation or retry is introduced.
- `ESS_RS::decodeTraffic` checks requests and correlated replies through existing
  codecs. Raw malformed/partial/unmatched data remains visible. The console adds
  function/register names and values with `sniff raw|decoded`; `sniff off` and a
  status query do not change bus work. Python logs asynchronous traffic separately
  from command/operation correlation. Display pressure drops diagnostic copies,
  preserving ordinary replies and retained results.

Three parallel agents covered regular API/source integration, observer/decoder
correctness and console/Python integration. Independent review checked actual
code and final hardware artifacts. Review hardened oversized input loss, RX
length/sequence correlation, copy-output aliasing, capture disable during partial
RX and the ordinary absolute target path. Tests corrected stale expectations
that changing a host tuple erases an unchanged wiring declaration. An initial
new test used the wrong RequestId member; it was fixed before valid verification.
No hardware test in this block failed or caused a write replay.

Original function PDF pages16/26/69/70/71 and existing source policy were reused:
position staging/trigger, auxiliary commands, feedback and stop distinctions.
No motor register, sign encoding or physical scale was guessed. Current FieldCore
`41aeaf9c` was inspected read-only: its fixed8N1/eight-byte requests and prefix
echo handling are deliberate differences; its consuming diagnostic ring is not
imported. This library keeps copied frames and independent cursors with no
FieldCore types or application scheduler in the core.

## Verification

| Check | Result |
| --- | --- |
| Strict native C++11 build and complete CTest suite |55/55 PASS |
| Python protocol/console harness |196 cases PASS |
| Bounded motion campaign orchestration |18 cases PASS |
| Generator failures and generated version/catalogue |18 cases and current outputs PASS |
| Offline contrast snapshots |5 references verified |
| Installed external consumers |C++11/C++17 PASS, directly using TrafficCapture/ESS decoder/PositionProfile |
| Regular firmware environments |units, probe, polling/load, timer/load all PASS |
| Observer invariance |On/off/overflow produce identical transport reads/writes/results; malformed and recovery bytes retained |
| Console pressure |Owner buffers/queued request/retained result unchanged; ordinary pending reply preserved |

The public APIs have no UART, framework, clock, heap or logger dependency.
Basic codec linking remains independent of descriptive catalogue strings.

## Standard-firmware hardware

COM13 preflight freshly identified the existing ESP32-S3 and retained its310
successful prior-image frames with zero failures; configuration/state/probe
reads succeeded before upload. The original CO2control backup was preserved.
Uploaded **ordinary `bench_s3_load_timer`**,543024 bytes, SHA-256
`680e65f8a06c7de70a42ca049da83444e0c505d44ab777c45db20775628b07fd`.
No mode-specific firmware or startup motor traffic exists.

Same node1/1152008N1, TX47/RX48/DE21,20us capture,85us starvation guard,
304us response gap, unloaded CPU/owner/console fixtures. Original native motion
words `[30,100,100,60,0,5000]`, configuration and logical inputs were retained.
The motion procedure prints complete command/stop/restoration plans before
opening the port. Each new phase stores its own JSON/JSONL evidence.

| Ordinary operation with sniffing active | Drive-reported result |
| --- | --- |
| Profile/identity/config/state reads, raw display |PASS, original profile saved |
| Stopped normal/direct stop, release then enable, decoded |PASS; released flag changes, no alarm |
| Relative100, raw; absolute0 return, decoded |Position0 ->100 ->0, activity/arrival and eventual speed0 |
| Normal stop during relative250, decoded |New RUNNING before stop; interrupted result retained; position206, eventual speed0; return0 PASS |
| Direct stop during relative250, raw |New RUNNING before stop; interrupted result retained; position110, eventual speed0; return0 PASS |
| Ordinary absolute100 without coordinate reference, decoded |Position0 ->100; endpoint100 known, displacement unknown; return0 PASS |
| Explicit profile restoration |Original six-word readback exactly matches; configuration unchanged |
| Ordinary probes |Ten one-attempt probes plus restoration/final probes PASS |

Final **371/371 frames**,3613 RX bytes,0 transport failures/timeouts/capture
faults/UART errors/discards. No automatic replay, save, motor restart or input
reassignment. The normal wiring contract yields ACKNOWLEDGED write results,
separate from observed completion; the generic unconfirmed case remains tested.
Final state: enabled, alarm0, raw motion1, raw speed0, raw position0; owner/DE
settled, no pending/retained/reserved work or recovery requirement. Sniff is off.

Observer:2118 records processed,1902 display lines emitted,216 display drops
explicitly reported,0 capture-input drops,2102 overwritten old history records,
16 retained. These counters describe diagnostic copies, not lost protocol bytes.
This is a bounded best-effort display, not a lossless bus recording. Raw/decoded
lines include exact bytes, request correlation and software timing; the archive
does not pretend omitted display copies were captured externally.

Software maxima: owner gap2172us, capture gap57us, capture high-water9. Capture
section32900207/161408028us (20.38% of one core), separately reported scheduler
CPU0%/9%. Normal replies had0 console input drops/short writes. Internal free/
min/largest336608/331448/278516 bytes; PSRAM8193580/8193580/8126452; owner/worker
stack headroom2036/3268 bytes. Diagnostic formatting adds service work, so these
stop observations are not electrical or independent physical latency claims.

Independent electrical/shaft measurement, algorithm3 interpretation, calibrated
units and unresolved negative encoding remain separate evidence limitations.
Unimplemented persistence/discovery and operations needing an actual motor
restart or missing semantic prerequisites are not silently enabled. These are
specific operation requirements, not a general functional-test admission mode.
