# Short unattended functional motion campaign

Baseline `c7b53d4`; final revision is the commit introducing this report. The user
explicitly removed person-at-bench/analyzer admission requirements for short
functional tests and excluded the several-hour soak. No numbered prompt21 work
was performed. [Procedure/API](../functional_bench.md),
[structured evidence](functional_motion_2026-10-04.json) and
[raw archive](functional_motion_2026-10-04_evidence.zip) accompany this report.

## Implementation and review

Updated the shared execution contract, prompts04/08/09, sequence index and bench
authorization. Functional acceptance uses drive reports and firmware evidence;
independent shaft motion and electrical timing stay unmeasured. The explicit
`bench_s3_functional` image opts selected actions/finite positioning into
read-only observation after a valid but source-unconfirmed FC06 frame. Execution
remains UNKNOWN, original source evidence remains false, and staging/CRC/timing/
deadline/overflow guards remain unchanged. No write is automatically replayed.

`motion-bench read|inspect|restore` retains original motion words, exact endpoint/
configuration binding and separate write/readback provenance. The finite Python
procedure prints the planned phase before opening COM13, reserves new evidence
files, sends at most one planned cleanup stop, and retains failures. Native-zero
experimentation uses the explicit ESS prerequisite `nativeZeroEnvelopeVerified`,
only for literal native absolute0 without references, origins or host limits.
It keeps displacement unknown and creates no false `AxisReference`. Ordinary
absolute moves remain strict. No speculative capture backend or application
scheduler was added to the core.

Three parallel reviewers covered original PDF semantics, application admission/
restoration, and core sequencing/evidence plus host orchestration. The lead
checked their findings against actual code and live traffic. Current FieldCore
was inspected read-only at `6debce7c`; its fixed8N1/eight-byte request and FC06 echo
handling remain deliberate integration differences, not imported framework code.

## Sources and first failure

The preserved function manual physical pages16/25/26/68-71 establish FC10
position staging, relative/absolute triggers, stops and auxiliary actions.
Pages77/78 document algorithms1/2 and arrival-error/time parameters. Actual
algorithm3 remains unresolved. Page69 distinguishes commanded position from
subdivision-equivalent encoder feedback by algorithm; this run does not resolve
which interpretation applies. The malformed printed FC10 example is not copied;
all traffic uses the checked builder. No negative two's-complement target,
direction-setting change, input reassignment, save or motor restart was sent.

The first100-increment move obtained RUNNING then ARRIVED after232841us. The
next read reported position99 and raw speed23, so the original immediate-zero
host assertion failed. Its single planned direct stop then obtained non-running,
zero speed and position99. A subsequent read retained99. All raw failure evidence
is preserved. The first return gate rejected before TX because it incorrectly
demanded feedback exactly100 as a prerequisite; profile restoration/readback and
ten probes succeeded before firmware replacement.

The manual does not promise ARRIVED implies exact target equality or simultaneous
zero speed. The host now observes up to ten additional read-only samples50ms
apart, retaining every sample. The final forward test reproduced ARRIVED with
nonzero raw speed12, followed by zero on the next sample. Return and moving-stop
tests also preserved transient nonzero readings. This corrects the host's
conflation of distinct reports; it does **not** establish whether the physical
cause is settling, feedback publication delay or something else. The99/100 and
1/0 differences remain observations, not a calibrated scale or proven tolerance
mechanism. The source-unconfirmed write outcomes remain UNKNOWN throughout.

## Final-image hardware results

COM13: ESP32-S3 USB303A:1001/MAC3c:0f:02:cd:6b:98, TX47/RX48/DE21, node1,
1152008N1, unloaded20us capture,85us starvation guard,304us reply gap. Final
functional firmware536832 bytes, SHA-256
`f48dd55115a78e96eb135d0c900615254321765d6f8955d3de0e083a9ee7d9e2`.
The upload encoding failure and its corrected UTF-8 upload log are retained;
only host firmware was restarted. The original CO2control backup is preserved.

Saved/restored `0x0020..0x0025`: `[30,100,100,60,0,5000]`. Motion used unchanged
raw ramps100/100 and native speed60. Subdivision1000, word order0, algorithm3,
soft-limit enable0, input functions1/2/3/0 and inactive logical inputs remained
unchanged. Physical speed/ramp/angle calibration is not claimed.

| Final-image case | Retained result |
| --- | --- |
| Stopped normal/direct stop | Checked subsequent non-running and zero-speed reports |
| Release then enable | Motion word1 ->17 ->1, alarm0; explicit stop reconciles each uncertain action |
| Initial absolute0 experiment | Position report99 ->1, RUNNING then ARRIVED, eventual raw speed0 |
| Positive relative100 | Report1 ->100, RUNNING then ARRIVED, speed12 ->0;232082us operation |
| Absolute0 return | Report100 ->1, RUNNING then ARRIVED, eventual speed0 |
| Normal stop during relative250 | New RUNNING observed before stop; interrupted operation retained; report1 ->101, speed36 ->0 |
| Return after normal stop | Report101 ->1, activity/completion and eventual speed0 |
| Direct stop during relative250 | New RUNNING observed before stop; interrupted operation retained; report1 ->51, speed20 ->0 |
| Return after direct stop | Report51 ->1, activity/completion and eventual speed0 |
| Restore and regression | Exact six-word readback, unchanged configuration, ordinary probe plus ten one-attempt probes PASS |

These are drive-reported positive/reverse activity and functional stops, not
independent physical observation or stop-latency measurements. No several-hour
test, continuous velocity or communication-loss-during-motion test was run.

Final totals: **310/310 checked frames**,3017 RX bytes,0 failures/timeouts/
capture faults/UART errors/discards. Capture/owner maximum gaps57/1753us,
capture high-water4; capture section12165071/59163978us (20.56% of one core),
separate scheduler estimates0%/11%. Trace ring overwrites5989 are retained as
bounded diagnostic loss; the serial evidence files preserve the full campaign.
Internal free/min/largest336608/331448/278516 bytes; PSRAM8201772/8201772/8126452;
owner/worker stack headroom2020/3268 bytes. Final owner/DE settled, no queued/
retained/reserved work, no recovery requirement or console drops/short writes.
Final state: enabled, alarm0, raw motion1, raw speed0, raw position1. The firmware
leaves the explicit functional image installed and sends no autonomous traffic.

## Verification and remaining limits

54/54 native CTests,191 Python protocol-harness cases,17 campaign cases,18
generator cases, generated artifact checks, five offline contrast references,
installed C++11/C++17 consumers and all five firmware environments PASS. Tests
cover strict defaults, uncertain FC06, malformed/stale evidence, literal-zero
policy boundaries, unknown displacement, exact restoration, delayed zero-speed
observation, bounded exhaustion, cleanup and no replay.

Independent TX/RX/DE timing, shaft displacement/standstill, calibrated units,
negative target encoding, raw algorithm3 semantics, feedback publication/arrival
precision and endurance remain open. There is still no remote motor power
control; communication-setting activation/restart cases remain NOT RUN under
prompt20. These limits do not undo the functional activity/stop evidence above.
