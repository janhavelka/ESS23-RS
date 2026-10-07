# ESS23-RS20 firmware 0x0029: pulse-rate boundary experiment

## Result

**Four hours and eight seconds of active practical-envelope testing completed:**
4,580 cases, comprising **4,145 completed finite moves and 435 planned stop
interruptions**, with no new motor/transport/cleanup failure in that envelope.
This is accumulated active experiment time (motion, observations, setup and
checks), not four hours of continuous shaft rotation. One host progress-file
failure split it into two sessions; it remains a failed session, not a PASS.

**The proposed 200 kHz limit is not a sufficient fix.** Four separate reset-like
failures were reproduced during broader tests, including two at only
180,000 increments/s. Eight persistent-RUNNING results, including a pilot
repeat, occurred at higher rates. The internal causes remain unresolved.
No universal library speed/subdivision cap was added. The passing envelope is
specific to this device, image, ramp matrix and fixture; it is not a new vendor
rating or proof of every combination below 200 kHz.

[Raw/structured evidence, scripts, matrix CSV and verification logs](2026-10-07_rate_boundary_evidence.zip)
retain every failed attempt. The archive's `manifest.json` hashes each member.
Archive: 49,880,721 bytes, 202 members, SHA256
`1145c04b46c2c4f9b620938e3b9d6f00bcb78d2b1158722d279ecc4fe2f0e18a`.

**Subsequent user-requested policy:** [the boundary follow-up](2026-10-07_position_limits.md)
adds conservative combined RPM/rate/ramp defaults. The no-cap disposition above
records this earlier study; neither report establishes a universal vendor limit.

## Scope and evidence rules

This study tests the hypothesis suggested by the exact ESS23-RS hardware
manual's 200 kHz pulse-frequency row. The manual does not say whether that row
limits internally generated serial positioning. Command-equivalent rate is
`subdivision * requested_rpm / 60`; it is not RS485 traffic rate, measured shaft
speed or a documented internal clock. The function manual's short-move diagram
also means a short move may never reach its requested speed. The original
[RS20 full datasheet](../vendor/ESS23-RS20_Full_Datasheet.pdf#page=1), drawing
A4573 revision 0 dated 26 May 2025, was also visually checked: it explicitly
labels 200 kHz **MAX. INPUT FREQUENCY**. Neither that label nor the hardware
manual identifies a corresponding limit on internal RS485 position generation.
The drawing specifies 24-48 V power and 1.0-4.0 A peak current; the actual
supply voltage/current rating and transient voltage remain unmeasured here.

Device: user-identified ESS23-RS20, raw model `0x4EEA`, readable firmware word
`0x0029`, raw algorithm `3`, configured encoder `4000`, high-word-first,
address 1, 115200 8N1. Only power and RS485 are connected, motor secured and shaft
free. This tests one device/firmware/fixture. There is no independent shaft,
voltage, electrical timing or thermal measurement. No gain, current, encoder,
I/O assignment, device communication, save or factory-restore write is used.

ESP32 image SHA256:
`79e3d642b5a3792f568fe365bab128b4466d08be48982c1c04f3692153ba7149`.
Arduino 3.3.11 / IDF 5.5.5, GPTimer capture and the shared USB console driver.
The host-supplied image hash is provenance, not an on-device hash attestation.
The source baseline is `77b0052f24270b10bd298c7d7ee8306aa1e5c9d0`.

All motion uses the regular application/public operation path, checked FC10
position staging at 0x0021/5 and relative start FC06 0x0027=1. Successful case records require checked staged words, sampled raw
state/feedback, an explicit normal or fast stop and checked standstill. Failed
transport cases retain unknown cleanup until a separate explicit recovery and
stop establishes it; those cases cannot count as successes. Completed moves have additional
natural-settlement observations **before** cleanup, so cleanup cannot turn a
still-running move into a successful natural completion. Results and settings
remain correlated with their operation/configuration context. No uncertain
motion is automatically replayed and no transport recovery is implicit.

One-increment cases deliberately probe observation limits. At 51,200 command
increments/revolution and a configured 4,000-count encoder scale, one command
increment is smaller than one feedback count. The explicit native-relative
cases use the existing transition completion policy; they do not opt into the
simple workflow's exact-endpoint witness. Unobserved RUNNING and unresolved
position change remain uncertain. A separately confirmed stop permits the next
**new** case; it does not certify the previous displacement. These cases must
not be counted as successful moves or as evidence of a pulse-frequency fault.

## Reproduced reset-like failure

The initial mixed-rate campaign stopped at case 210:

| Parameter | Value |
| --- | ---: |
| Subdivision | 6,400 |
| Requested speed | 2,812 rpm |
| Acceleration/deceleration native words | 100 / 100 |
| Relative target | 32,000 increments (5 turns) |
| Command-equivalent rate | 299,946.7 increments/s |

Both setup and trigger were acknowledged. Fresh reads saw RUNNING, followed by
one state query with no reply. The owner entered its recovery interlock; cleanup
stop requests were refused before transmission. The host/USB remained responsive,
with no new capture/RX fault or ESP32 reboot. After explicit host recovery and a
separately issued fast stop, readback showed position zero, subdivision changed
from 6,400 to 1,000, and profile changed to `[30,100,100,60,0,5000]`.

One deliberately authorized reproduction from confirmed standstill produced the
same missing reply and loss of volatile values. In that repeat, feedback went
from 1,600 to 1,611 to 32,118 before communication failed. No cleanup stop had
been sent at the failure. Later readback again showed zero position and default
settings, while the ESP32 uptime continued.

This establishes a reproducible motor-controller reset or equivalent loss of
volatile state. It does **not** establish brownout, regenerative overvoltage,
firmware watchdog, arithmetic error or a 200 kHz cause. Supply voltage at the
failure and the motor's internal reset reason are unavailable. A zero alarm
after restart cannot rule out a transient fault. The two reset-like events and
the interrupted campaign remain failures; later successes do not erase them.

The first explicit recovery after the reproduction also delivered the old
move's cancellation terminal record into a new host session. Strict correlation
rejected that unrelated ID. That failed attempt is retained. A new explicit
session drained old output and confirmed fast stop/readback; no move was replayed.

## Counterexample below 200 kHz and ramp controls

The first below-boundary sweep stopped at case 352: subdivision **4,000**,
**2,700 rpm**, target **20,000 increments / 5 turns**, ramps **100/100**.
The equivalent rate is **180,000 increments/s**, below the proposed ceiling.
The same missing response, zero position and default subdivision/profile were
observed after explicit recovery. The ESP32 again stayed alive.

After confirmed stop and configuration readback, a controlled sequence changed
only the two ramp words for that same target, speed and subdivision:

| Acceleration word | Deceleration word | Result |
| ---: | ---: | --- |
| 100 | 500 | Natural standstill, exact 20,000-increment feedback difference |
| 500 | 500 | Natural standstill, exact 20,000-increment feedback difference |
| 500 | 100 | Natural standstill, exact 20,000-increment feedback difference |
| 100 | 100 | Missing state reply and reset-like loss of volatile settings again |

The repeated 100/100 failure is not caused by the cleanup stop: no stop had
been transmitted when communication failed. Either slower ramp makes this
particular finite move succeed. That does not isolate acceleration versus
regeneration: with different ramp words a short move can have a different peak
speed and trajectory. No physical acceleration formula is inferred.

**A 200 kHz cap alone is insufficient.** A below-cap case reproducibly fails;
passing requested rates above the cap were also observed in the mixed sweep.
No universal library rate limit is justified by these results. This reset is
a separate failure mode from persistent RUNNING. It does not rule out an
internal rate constraint contributing to that original symptom. The exact
manufacturer meaning of the row and the internal reset cause remain open.

One recovery attempt was refused because the earlier recovered result 4379 was
still retained after a deliberately strict new-session correlation failure.
Inspecting/releasing that reviewed terminal recovery result freed its one
recovery-result slot. Explicit recovery, fast stop and readback then succeeded.
Releasing evidence storage was not treated as a motor stop. All failed sessions
remain in the evidence, including this test-session bookkeeping issue.

## Four-hour endurance envelope

After those failures were deliberately diagnosed, a separate four-hour phase
uses rates at or below 200,000 increments/s **and requested speeds at or below
2,000 rpm**. Ramp pairs are 500/500, 100/100, 2000/2000, 100/500 and 500/100.
Subdivision values are 400, 1600, 4000, 6400, 8000, 10000, 12800, 20000,
25600 and 51200. Per group, speeds include 60 rpm and approximately 25%, 50%,
90%, 99% and 100% of the smaller RPM/rate ceiling. Targets are 0.25, 2.5 and
5 turns, plus a 20-turn upper-speed case. Separate ten-turn commands exercise
planned normal/fast interruption. All are finite positive moves; negative
encoding and external fixtures are not qualified here.

The largest requested speeds in this practical endurance phase were:

| Subdivision | Maximum tested requested rpm | Equivalent increments/s |
| ---: | ---: | ---: |
| 51,200 | 234 | 199,680 |
| 25,600 | 468 | 199,680 |
| 20,000 | 600 | 200,000 |
| 12,800 | 937 | 199,893.3 |
| 10,000 | 1,200 | 200,000 |
| 8,000 | 1,500 | 200,000 |
| 6,400 | 1,875 | 200,000 |
| 4,000 | 2,000 | 133,333.3 |
| 1,600 | 2,000 | 53,333.3 |
| 400 | 2,000 | 13,333.3 |

These are **tested session bounds**, not automatically enforced public-library
limits or proof of sustained shaft speed. Use the same recorded ramp/travel
conditions when comparing results.

The excluded high-RPM/short-ramp cases remain failures. This narrower envelope
cannot validate every combination below 200 kHz. One-increment observation cases
are retained from the earlier sweep and are not repeated as if they proved
successful physical motion. The new endurance phase uses resolvable travel.

## Completed sessions and failed attempts

| Session | Completed case records | Disposition |
| --- | ---: | --- |
| Initial mixed-rate pilot | 28 | 27 ordinary completions; one persistent-RUNNING result. Deliberately ended after review to add pre-cleanup natural-settlement checks. |
| Mixed-rate matrix | 209, then failed case 210 | 190 ordinary completions, 12 planned stops, seven persistent-RUNNING cases; case 210 lost its state reply and volatile settings. Raw case-210 commands/results are retained even though the early script did not emit its compact trial row. |
| Exact 6,400 / 2,812-rpm reproduction | 1 attempted | Same missing reply and default settings; failed before compact trial emission. |
| Below-200-kHz boundary sweep | 352 | 214 ordinary completions, 34 planned stops, 103 one-increment observation-limit outcomes; case 352 reset-like failure at 180,000/s. |
| Ramp controls | 4 | Three ordinary completions and repeated 100/100 reset-like failure at 180,000/s. |
| Endurance session A | 944 | 855 ordinary completions and 89 planned stops; host progress-file replacement failed before case 945 was issued. Confirmed stop/settings restoration; session remains FAIL. |
| Endurance session B | 3,636 | 3,290 ordinary completions and 346 planned stops; session PASS with original session settings restored. |

Session A active time: **2,996.891 s**; session B: **11,411.062 s**;
combined **14,407.953 s**. Session A evidence runs from 09:49:51 to 10:39:49 UTC;
B from 10:41:34 to 13:51:47 UTC on 7 October (Prague is UTC+2).
The stopped interval and earlier diagnostic experiments are not counted toward
four hours. Case boundaries permit a small duration overrun so the current
finite case and cleanup can finish. Session B covers all 50 subdivision/ramp
pairs, with repeated passes; the exact per-case values are in `trials.csv`.

The host failure was Windows `ERROR_ACCESS_DENIED` while replacing the watched
`status.json`. Motor processing was healthy and cleanup completed. The experiment
was corrected to append progress records, like its existing evidence log, with
no file replacement or write replay. The first attempt's failure is retained.
The early diagnostic script could mask a failed read with a cleanup refusal;
the subsequent harness preserves the primary error, records the partial trial,
and reports cleanup separately without bypassing the recovery interlock.
These are experiment-script corrections, not motor firmware fixes.

Persistent RUNNING occurred at subdivision 51,200, 100/100 ramps: 900 rpm at
0.25 and 5 turns; 1,200 rpm at 0.25, 2.5 and 5 turns; 2,000 rpm at 2.5 and
5 turns. The pilot repeats the 900-rpm quarter-turn case. All these had checked
setup/trigger acknowledgements and continuing raw state replies; a separate
fast stop established standstill. The mixed matrix also has passing cases
above 200 kHz. Its measured position finite differences sometimes exceed
200,000 increments/s, but are encoder-derived averages between bounded host
observations, not independent shaft-speed measurements or proof of an internal
pulse clock. They cannot identify a universal sharp hardware threshold.

## Health, timing and resource evidence

Across the two endurance sessions the checked-frame counter increased from
40,159 to 298,408: **258,249 additional frames**, including setup, reads and
cleanup. Failed/timeouts stayed **4 -> 4** (the four earlier reset events),
capture faults **0 -> 0**, RX errors **0 -> 0**. No new recovery interlock,
ESP32 reboot, console input drop or blocked output occurred. Diagnostic trace
overwrite counts increased as expected for bounded rolling storage; this is not
protocol-byte loss. USB short-write count stayed 97,773 throughout endurance;
that historical counter had increased while failed sessions closed their port
with terminal output pending. It is not a count of motor communication errors.

| Measurement | Endurance observation |
| --- | ---: |
| Internal free / recorded minimum | 347,048 / 341,892 bytes; unchanged |
| PSRAM free / minimum | 8,173,100 bytes; unchanged |
| Owner / worker stack free watermark | 1,396 / 3,268 bytes; unchanged |
| Maximum owner service gap | 4,410 us |
| Maximum capture gap | 64 us (configured bound 85 us; no exceedance) |
| Normal stop whole operation duration | 125,104-1,923,242 us |
| Fast stop whole operation duration | 24,960-259,273 us |
| Largest ordinary-move feedback endpoint difference | 12 command increments at subdivision 51,200 |

Stop durations above are `serviced_us - started_us`, including host operation
admission, write and observation; they are **not measured shaft braking times**.
The 12-increment feedback difference is about one configured encoder count
(51,200/4,000 = 12.8 command increments/count), not proof of shaft accuracy.

CPU scheduler estimates were approximately 0-1% / 29% for the two cores when
valid. The existing `Esp32Load` estimator intentionally marks CPU percentages
unavailable after its 32-bit accounting window (~71.6 minutes); later values
are null, not zero or evidence of a crash. Capture section time remains measured
separately (about 20.6% of one core). No CPU estimate after that window is claimed.
Memory and timing counters remain valid and continued to be sampled.

## Final bench state and reproducibility

Final explicit restoration PASS: subdivision **51,200**, profile
**`[30,500,500,60,0,32000]`**, matching the pre-test backup. Because reset-like
failures had destroyed the device's volatile values, restoration was explicitly
reconstructed through ordinary checked commands: subdivision 51,200,
speed 60, accel/decel 500, then one finite `moveby 225 deg` to stage the original
32,000-increment profile. Fast stop and exact profile readback followed. No
persistent save, counter clear, algorithm, encoder, gain or I/O change occurred.

Final raw position **216,080,428**, speed **0**, motion **1**, alarm **0**; enabled,
not running. Host next-move preferences are 60 rpm / 100 / 100, distinct from the
restored drive settings. Monitor/debug/injected load are off, UART idle, DE off,
queued/reserved requests zero, recovery interlock false. Two historical retained
records remain inspectable; they are not active work. RAM origin was refreshed
by explicit subdivision changes; the pre-test coordinate origin is not preserved
across device resets. COM13 is closed/free. Motor firmware remains `0x0029`;
ESP32 image is unchanged. No autonomous motion is left running.

The archive contains the exact scripts and command records. Extracting it under
`build/rate_qualification_20261007/` at the source baseline permits inspection.
`python build/rate_qualification_20261007/analyze.py` regenerates the offline
summary/CSV. The motion scripts reserve fresh evidence paths and own COM13
exclusively; they are recorded experiments, not an instruction to replay a
known failing case. All motor writes and explicit recoveries are in their logs.

Native/fake suites: **77/77 PASS** via
`ctest --test-dir build/position_followup_final/native -C Release --output-on-failure`.
`python scripts/check_repository.py`: references/docs/metadata PASS.
No shipped runtime code/settings changed in this block. CI for the exact pushed
commit is checked after push; the report does not substitute older green CI.

## Remaining questions and library disposition

- 200 kHz is labelled input frequency in the RS20 drawing; internal RS485
  applicability is still undocumented. No blanket cap or silent clamping added.
- Persistent RUNNING and reset-like loss of volatile settings remain distinct,
  unresolved drive behaviors. Do not automatically replay either failed move.
- A 2,000-rpm ceiling plus the rate/ramp envelope passed this experiment; it is
  practical fixture evidence, not a universal motor maximum or all-values proof.
- Actual supply voltage at the reset, internal reset reason, firmware version
  mapping and supported update procedure are unavailable. No motor update was
  attempted. Vendor analysis should include both failing staged profiles, raw
  firmware `0x0029`, algorithm `3`, successful ramp controls and lost settings.
- Negative-direction encoding, loaded mechanics, electrical/thermal measurements,
  restart/persistence, continuous velocity and external I/O remain unqualified
  by this experiment. No external fixture or independent shaft reading is invented.
