# ESS23-RS20 firmware0x0029: position completion investigation

## Result

The high-subdivision failure is reproduced in **checked drive replies**, not a
USB or application stall. Legal parameter fields do not establish that every
combination works on this firmware. The exact internal cause remains unresolved:
firmware trajectory arithmetic, undocumented parameter interaction and control
behavior cannot be distinguished from the published material. No guessed speed
cap, automatic retrigger, gain change or false completion rule was added.

A separate host-side limitation is fixed: moves that finish between RUNNING
observations can now complete from two fresh, exact stopped endpoint reports.
The actual short-move regression passes15/15;12 use this alternative evidence.
Persistent RUNNING remains a failure and cannot pass this new test.

[Raw evidence, scripts, CSV and verification logs](2026-10-07_position_completion_evidence.zip)
preserve unsuccessful attempts separately. The detailed matrix is also below.
This is finite free-shaft testing, not all combinations or mechanical accuracy
qualification. No independent shaft speed, acceleration or endpoint measurement
was available during these tests.

## Exact device and firmware

User-identified ESS23-RS20, bolted with free shaft, power and RS485 only.
Fresh FC03 identity reads before and after give model0x4EEA, driver version
**0x0029 (41)**, active address1, DIP0. Version register0x0001 is readable;
`read identity` exposes the raw version. The manual does not define its encoding
or release/date mapping. Do not call it v0.41/v2.9 or claim it is current.

Configuration provenance: raw algorithm3 (undocumented in the reviewed table),
configured encoder4000, high-word-first, original subdivision51200. Control read
raw words are `[3,4000,5600,100,40,100,40,200]`. Filter/tracking read is
`[2,5,4000,5,10,512]`; current-loop `[6553,1024,102,409]`; LA
`[2560,256,20,2560,128,100,10,2]`. The archive retains field names, addresses,
raw frames and configuration generations. No algorithm, encoder, current,
gain, input assignment, persistence or device communications setting changed.

Configured encoder counts do not identify its part or prove physical accuracy.
The documented position pair is subdivision-equivalent feedback in closed loop,
not raw encoder counts. The standalone session's labelled coordinate convention
is retained; raw algorithm3 has not been relabelled as a documented algorithm.

## Original pages and update search

Reviewed the actual preserved function-manual physical pages16,68,69,70,77:
relative start0x0027=1, staged window0x0021/5, status bit meanings, feedback
pair/speed, subdivision, parameter ranges and algorithm/version entries. Reviewed
speed0..3000, ramps0..2000 and subdivision400..51200 include these test inputs.
The transmitted frames use that documented window and relative start, with
checked FC10/FC06 acknowledgements. The current register catalogue still guards
undocumented access; no bare-address bypass was used.

On7 October the [official product downloads](https://www.omc-stepperonline.com/ess-series-2-2nm-311-55oz-in-nema-23-integrated-rs485-closed-loop-stepper-servo-motor-24-48vdc-1000ppr-ess23-rs20)
still list the function manualV1.0, hardware manual and RS20 datasheet.
The web reader retrieved the current90-page function manual; no newer ESS
revision or correction for this behavior was found. Direct downloads from the
new `files.omc-stepperonline.com` links returned403, so **remote PDF byte equality
was not verified**. `reference_recheck.json` records those HTTP failures; its
response hashes are HTML error bodies, not new vendor PDF hashes. Preserved
vendor files were not changed. Their existing manifest remains authoritative:

- Function manual SHA256 `0ca2d7f6b69404ead3ba9af982076c54f86eb16b1c24ba237f344aac079b81eb`.
- Hardware manual SHA256 `44a16a4e164e8434702bf6f0e3f14e446c732d997111d189202fb627c961dfa0`.
- RS20 datasheet SHA256 `2542a653ec0521a31a621068a498dc494935ed316602e92fe93d0223896dea02`.

The product links PC debug softwareV1.2.7. The official
[software repository](https://github.com/StepperOnline/Tunning-Software) also
contains Y-series7.4; the [vendor guide](https://help.omc-stepperonline.com/hc/s/articles/how-to-use-the-y-series-closed-loop-driver-software)
describes PC configuration software for other Y-series hardware. This is not
an ESS firmware release. The archivedV1.2.7 ZIP contains a Windows application,
not a separately identified ESS firmware image. No executable was run.

**Motor firmware updateability is not established.** No public ESS23-RS20
firmware image, bootloader/update procedure, rollback instructions, changelog or
0x0029 mapping was found in the checked official sources. We can update the ESP32
application; that is separate from updating the motor. Vendor confirmation is
needed before selecting any image/interface. Nothing was flashed to the motor.

## Controlled parameter matrix

Original ESP32 image: `6b9d6544787c7d2170547cac1ec1726646b188c8a79ea446c955e718fc5555be`.
GPTimer capture,1152008N1 (adapter actual115211), no injected load/debug/polling.
Each case begins with checked standstill. Setup words are asserted against the
case; setup and trigger are not replayed. Fresh state reads sample for up to8s,
then one explicit fast stop and fresh zero-speed/non-running read confirm cleanup.
`cancelled` below means completion remained unconfirmed and the planned stop
interrupted the operation. The successful experiment wrapper means evidence and
cleanup completed; it does **not** relabel those moves as successful.

| Subdivision | Native ramps | RPM field | Native increments | Move outcome | Last position difference before stop | Last motion/speed words |
|---:|---:|---:|---:|---|---:|---|
|1600|100/100|2000|4000|observed|4001|1/0|
|12800|100/100|2000|32000|observed|32001|1/0|
|25600|100/100|600|64000|cancelled|64851|4/600|
|25600|100/100|1200|64000|cancelled|64967|4/1200|
|25600|100/100|2000|64000|cancelled|65094|4/2000|
|51200|100/100|600|12800|observed|12799|1/0|
|51200|100/100|600|128000|cancelled|128947|4/600|
|51200|100/100|800|12800|observed|12788|1/0|
|51200|100/100|800|128000|cancelled|129010|4/800|
|51200|100/100|900|12800|cancelled|13632|4/900|
|51200|100/100|900|128000|observed|128000|1/0|
|51200|100/100|1200|12800|cancelled|13656|4/1200|
|51200|100/100|1200|128000|cancelled|129165|4/1200|
|51200|100/100|2000|12800|observed|12801|1/0|
|51200|100/100|2000|128000|cancelled|129420|4/2000|
|51200|500/500|900|12800|observed|12813|1/0|
|51200|500/500|900|128000|observed|128013|1/0|
|51200|500/500|1200|12800|observed|12813|1/0|
|51200|500/500|1200|128000|cancelled|129267|4/1200|
|51200|500/500|2000|12800|observed|12800|1/0|
|51200|500/500|2000|128000|cancelled|129292|4/2000|
|51200|2000/2000|1200|12800|observed|12801|1/0|
|51200|2000/2000|1200|128000|observed|128051|1/3|
|51200|2000/2000|2000|12800|observed|12800|1/0|
|51200|2000/2000|2000|128000|observed|128013|1/0|

The matrix has14 observed completions and11 persistent-RUNNING cases. No alarm
or RTU error accompanied the11 cases; all25 fast stops confirmed standstill.
At51200,12800 increments is a quarter turn and128000 is2.5 turns under the declared
scale. Lower subdivision1600/2000rpm/4000 and12800/2000rpm/32000 passed the same
2.5-turn command. They are tested alternatives, not universal limits.

Repeated on the new ESP32 completion image:51200/100/900/12800 failed3/3;
51200/100/2000/12800 completed3/3;51200/2000/2000/128000 completed3/3.
Failure is not monotonic with requested speed. This does not rule out an
internal frequency limit: short moves may never attain their requested speed.
The subsequent [manual/FAQ/forum review](../reference/12_ess_rs_motion_web_review.md)
identifies the exact RS hardware manual's unexplained 200 kHz pulse-frequency
maximum. Its application to serial positioning still needs clarification.
Slower configured ramps improve some cases, but we did not change defaults or
claim a universal ramp conversion. A drive speed word is not an independent
tachometer reading.

An earlier retained batch requested100 ramps in host preferences but actually
staged500/500 from the saved native profile. Its raw staging words identify this
script mistake; those results are not100-ramp evidence. At25600/2000/64000 with
actual500/500 the drive remained RUNNING. Normal stop then expired after3s with
52 polls still reporting RUNNING. A separately planned fast stop succeeded on
its first observation. Profile restoration succeeded; original subdivision
restoration was delayed by a pending host handle assertion. The corrected batch
settled handles and restored51200 explicitly. The original failure is retained.
Normal stop therefore remains unreliable in this demonstrated drive state;
extending a timeout is not claimed to fix it.

## Short moves: before/after the host fix

Old image, subdivision400, ramps100/100, speed3000, distances1,2,5,10:
all four reached the exact fresh feedback difference, zero speed, ARRIVED and
not-RUNNING, but the old sequencer exhausted its observation count because it
never saw RUNNING. Distance20 completed through the normal transition.
That is sufficient evidence of the host observation-policy limitation.

The shared public mover now has an explicit, default-off
`positionFeedbackMatchesCommand` prerequisite. Admission requires a correctly
bound stationary native reference and a changed signed-32-bit endpoint. The
ordinary simple API/CLI enables it after fresh preparatory reads under its
existing coordinate convention. It does not infer an encoder mapping in the core.

After checked trigger acknowledgement, two consecutive seven-word observations
must agree with the **exact** endpoint and report ARRIVED, not-RUNNING, zero
speed and no alarm/release/limit. A mismatch resets the first witness. Old flags,
any arbitrary position change, lost acknowledgement and persistent RUNNING do
not pass. Retained evidence records both full replies, source times and the
confirmation method. The window is not documented as an atomic drive sample.

On the new image,1/2/5/10/20 increments at400 and3000rpm repeated three times:
15/15 exact feedback differences,12 endpoint confirmations without RUNNING and
3 ordinary transition completions. No automatic move replay occurred. Exact
matching deliberately does not turn encoder quantization/overshoot into success;
a nonmatching endpoint still needs the ordinary transition or remains uncertain.

One initial regression attempt failed before motion because its test-side saved
profile binding was invalidated by the explicit subdivision change. The fixed
script forgets that old binding before saving the new one; it never weakens the
production parser. A final read-only attempt requested `queue`, which is not in
the Python inventory, and failed locally after reads. Both failures are archived;
the corrected read-only session passed. No failed attempt is hidden by its rerun.

## Final image, bench state and software verification

Arduino image SHA256 `79e3d642b5a3792f568fe365bab128b4466d08be48982c1c04f3692153ba7149`,602848 bytes, Arduino3.3.11/IDF5.5.5.
It uses the existing shared IDF USB console driver. Motor firmware stays0x0029.
Final readback restores subdivision51200 and profile `[30,500,500,60,0,32000]`.
Host next-move preferences are60rpm/100/100; they are not claimed to be already
applied device settings. Raw position95232104, speed0, motion1, alarm0, enabled.
RAM-only zero was refreshed by subdivision restoration. COM13 is closed; queued
requests0, reserved requests0, UART not busy, DE off, no recovery interlock,
monitor disabled, injected load0, debug off. No autonomous motion remains.

The final new-image counters are1373/1373 checked frames,0 transport failures,
timeouts, capture faults or RX errors. These do not erase move-level failures.
Maximum owner gap3004us, capture gap57us; internal minimum341892B, PSRAM free
8173100B, owner stack headroom1396B. Larger19-byte evidence and its bounded
additional position witness stay in the existing caller-owned/PSRAM storage.

Native/fake Arduino and IDF tests cover exact evidence, wrong/stale reports,
CRC failure, word order, signed endpoint, missing reference, old arrival,
nonzero speed, alarm, cancellation and unknown trigger acknowledgement.
Python verifies endpoint proof and rejects altered/stale/contradictory records.
The actual application fixture tests the ordinary short `moveby` path.
The quick verifier passes77 registered tests plus generated/reference/header,
clean source/install consumer and catalogue-link checks. Arduino and native-IDF
images build; IDF's local ROM-GDB-symbol environment warning does not prevent
its firmware build. IDF physical retest was not run in this block. Current
FieldCore owner source was inspected read-only; no firmware-owned types or
second transport path were introduced.

CI for the committed source is checked after push; exact run status belongs to
the commit's GitHub checks. This report does not claim new overnight qualification.

## Questions prepared for StepperOnline (not sent)

1. What exact hardware/build/date is model0x4EEA, firmware0x0029? What does
   algorithm3 mean on ESS23-RS20, and is there a newer applicable firmware?
2. Why does the acknowledged relative setup `[100,100,900,0,12800]`, subdivision51200,
   start speed30, then0x0027=1 remain RUNNING and creep after the expected travel,
   while2000rpm at the same distance completes? Is there an undocumented
   trajectory/acceleration constraint or known firmware arithmetic defect?
3. Why can normal stop0x0100 remain RUNNING in that condition while fast stop0x0200
   establishes standstill? Which drive observation proves normal stop completion?
4. If field updating is supported, supply the exact ESS23-RS20 image/checksum,
   compatible hardware revisions, cable/interface, update and recovery procedure,
   persistence/backup requirements and release notes. PC tuning software alone
   is not enough to select an update.
