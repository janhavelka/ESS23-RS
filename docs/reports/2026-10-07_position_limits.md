# Default position operating boundary, 7 October 2026

## Policy and its meaning

User requested an explicit default after the [four-hour study](2026-10-07_rate_boundary.md).
The shared ESS position preparations now enforce **all three** conditions:
requested speed <=2,000 rpm, command-equivalent rate <=200,000 increments/s,
and each native ramp word 100..2,000 ms. Integer maximum speed is
`min(2000, floor(12000000 / active_subdivision))`; no floating-point threshold
or silent clamping is involved. The upper RPM bound matters: 2,700 rpm /
4,000 subdivision / 100-100 ramps previously caused a reset-like failure at
only 180,000 increments/s.

This is a **conservative policy boundary**, not the exact physical failure
threshold, a universal ESS rating, a cure for motor firmware, or proof of
absolute reliability. The original input-frequency specification does not
establish an internal serial-motion rate. The prior failures remain failures.

## Implementation and API changes

- `PositionLimits`, `positionSpeedLimit` and `checkPositionLimits` live in the
  installed ESS position API. Native `PositionCommand::prepareRelative/Absolute`
  and common/profile `prepareMoveRelative/Absolute/Angle` share the validator.
- Supply active `subdivision` in `PositionCommand` or `MovePrerequisites`.
  Zero/unknown rejects. Host command scale, origin, encoder counts and gearing
  cannot substitute for this drive setting. Applications still own freshness.
- Default preparation rejects before publishing work, preserving output.
  The admitted context retains subdivision and limits. Explicit caller policies
  can narrow/expand the envelope under caller qualification; manual bounds remain.
- Raw register/profile/start builders retain their low-level contracts. They do
  not establish readiness or enforce an operating policy. `USE_STORED` relies
  on the supplied settings matching the actual drive; it is not an implicit read.
- Arduino and IDF use the same application/readback and core check. `settings`
  reports the subdivision ceiling; simple move errors preserve the actual reason
  and do not retain an axis reservation. Host intent setters remain local:
  `speed 3000` stores intent, but a move rejects before writes.
- No trigger/completion, stop, retry, recovery, UART or capture rule was relaxed.
  No motor firmware, save, current, encoder, algorithm or I/O setting changed.

Current FieldCore `src/rs485/Rs485Task.cpp` was inspected read-only at
`9d4b58c55983e1f223a9cd4064dc15bdaf27c5ff`. Its existing transport ownership and
other framing modes remain outside this pure profile policy; no FieldCore edit,
new owner or framework type was introduced.

## Physical matrix

Device: user-identified ESS23-RS20, model word `0x4EEA`, firmware `0x0029`,
raw algorithm 3, configured encoder 4000, high-word-first, node 1 / 115200 8N1.
Motor secured with free shaft, power and RS485 only. Supply transients, shaft
speed/displacement, temperature and mechanical loads are not independently measured.

The new Arduino image uses Arduino 3.3.11 / IDF 5.5.5 and ordinary GPTimer capture:
SHA256 `5e9dc8a7d3d7c463482e511c573d6908dbe4f218f39143cca97881129645e025`.
Previous working image remains preserved under `build/position_followup/pio/`.

The finite matrix uses subdivisions 400, 800, 1000, 1600, 3200, 4000, 5999,
6000, 6001, 6400, 8000, 10000, 12800, 20000, 25600, 51199 and 51200.
For each subdivision, native ramp pairs are 100/100, 100/2000, 2000/100 and
2000/2000. Each group includes:

1. Approximately one quarter turn at the integer ceiling minus one rpm.
2. Approximately one quarter turn, five turns and twenty turns at the ceiling.
3. Separate ten-turn commands interrupted by normal and fast stop, at the lesser
   of 600 rpm and the ceiling.
4. A ceiling-plus-one-rpm request, required to reject with no increase in the
   owner's started-transaction counter. This is a software rejection test,
   not physical operation above the limit.

Non-divisible subdivisions use floor(subdivision/4) exact command increments.
Each group also uses an ordinary 90-degree/60-rpm preparation move; these setup
moves are logged separately from the matrix count. No uncertain motion is replayed.
Every ordinary case needs successful correlated completion and natural zero-speed,
nonrunning reports before cleanup; stops cannot manufacture a successful endpoint.
Each stop case requires a separately confirmed stop. Profiles are read/snapshotted,
restored between configuration changes and checked again during final cleanup.
Raw/parser evidence and periodic status/health/resource records are incremental.

### Final outcome

**PASS: 408/408 matrix cases**, comprising **272 completed finite moves** and
**136 planned normal/fast stop interruptions**, over **1,065.141 seconds** of
active matrix time (17m45s, including observations/setup). All **68** requests
at ceiling+1 rpm rejected without a new motor transaction. There were also 68
logged preparation moves, not included in the 408 count.

The before/after counters cover **21,553 checked frames** in the matrix session.
Failed/timeouts/capture faults/RX errors remained zero. No persistent RUNNING,
reset-like setting loss, drive alarm or cleanup failure occurred. Natural
settlement was checked before each ordinary case's cleanup stop. Maximum
feedback endpoint difference was **12 command increments**, at subdivision
51200 (12.8 command increments per configured 4000-count encoder count).
That is not a measured physical-position accuracy. RPM is requested speed;
short trajectories and long ramps may not reach it.

The separate ordinary-console regression passed: `settings` displayed 234 rpm
at subdivision51200; speed235 and each 99 ms ramp were rejected without a motion
command or profile change; then `moveby 90 deg` and `moveto 360 deg` completed at
234 rpm /100/100, with no manual release/recovery. Repeating `moveto 360 deg`
reported a stopped no-op. This session added **84 checked frames**, with no errors.

| Software/resource observation | Measured value |
| --- | ---: |
| Maximum owner service gap | 4,100 us |
| Maximum capture gap / configured bound | 69 / 85 us |
| Final internal free / minimum | 347,048 / 341,892 bytes |
| PSRAM free / minimum | 8,173,100 / 8,173,100 bytes |
| Owner / worker stack minimum headroom | 1,348 / 3,268 bytes |
| Final CPU estimates, cores0/1 | 0% / 25% |
| Capture callback time fraction | about20.7% of one core |
| Console blocked / short writes / dropped input | 0 / 0 / 0 |

Internal free memory moved from347400 to347048 bytes during initial activity,
then periodic observations remained flat; PSRAM remained flat throughout. These
are measured workload watermarks, not a proof of every lifetime/allocation case.
Per-case host time, including checks/cleanup, ranged0.593..8.703s, mean2.237s;
it is not physical acceleration or stopping time. Trace overwrites are the
bounded rolling diagnostic history, not lost protocol frames.

### Retained evidence and final bench state

[Evidence archive](2026-10-07_position_limits_evidence.zip): **4,123,584 bytes**,
**85 members**, SHA256
`c270c6202189341dda152e91ee144044a8b46ba0181ee75a94ce19548acffeec`.
It contains raw JSONL, structured summaries, matrix CSV, scripts, firmware image,
source diff against `ad5681a5fd195b8f3d3aa165cce77f3d6e4ca816`, verification logs,
exported core and a per-member SHA256 manifest. Extract under
`build/position_limits_hil/`; `python build/position_limits_hil/analyze.py`
recreates the offline matrix/summary. The live scripts own COM13 exclusively
and reserve new evidence paths; do not overwrite/replay an old run blindly.

One initial host attempt failed from a missing `timeout_s` argument after a
confirmed stop and before any move or setting write. Its `boundary.json/jsonl`
and console log remain FAIL; the corrected `boundary-run` is separate.
Intermediate build errors/missing new fixture metadata and an early CTest run
before all binaries existed are retained; final complete verifiers pass.
No physical failure was hidden or converted into PASS by cleanup.

Final motor settings equal the original: subdivision **51200**, profile
**[30,500,500,60,0,32000]**, raw firmware **0x0029**, algorithm3, encoder4000.
Fresh raw position **240971966**, speed**0**, motion**1**, alarm**0**; enabled,
not running. Host next-move intent is60rpm/100/100. UART idle, DE off,
pending/reserved/retained operations0, recovery interlock false, monitor/debug/
injected load off. COM13 closed/free. New RAM session coordinates were established
through explicit subdivision operations; no persistent coordinates or device
counter clear. The new Arduino image remains installed; motor firmware unchanged.

## Software verification

- Every integer subdivision 400..51200: native preparation accepts the integer
  ceiling and rejects ceiling+1 with unchanged output. Invalid/missing subdivision,
  custom-policy overflow boundaries, both ramp boundaries and all setup modes tested.
- Common preparation and actual Arduino/IDF application fakes test the same
  limits, independent of the host coordinate scale; no stage/start on rejection,
  followed by a successful ordinary move. No new origin/gear/lead dependency.
- 77/77 native/Python suites PASS. Clean source/install consumers, isolated
  C++11 public headers, C++17 consumers without RTTI, codec-only catalogue
  exclusion, generated metadata and offline reference/doc checks PASS.
- Full Arduino verifier PASS for all four environments. Full IDF verifier PASS
  for native S3 firmware, clean S3/S2 core consumers and portable S2 application.
  S2 is compile-only; no new IDF/S2 physical motion qualification is claimed.
- Exact pushed-commit CI is checked after push; an earlier CI run is not used
  as evidence for this change.

## Limits that remain

The four-hour baseline covered different interior ramp/speed points; the new
matrix exercises policy edges. Neither tests every trajectory or all continuous
ramp values. Long moves still require adequate caller deadlines. Very small
moves may be below feedback resolution or miss RUNNING; the existing strict
completion rules remain. Negative encoding, loaded mechanics, physical speed,
electrical/thermal behavior, continuous velocity, homing and external I/O gain
no qualification from this study. Reset cause, the vendor's internal rate
semantics and a supported motor-firmware update route remain unresolved.
