# Optional evidence age and native position commands — 2026-10-05

User-directed follow-up to the unpublished prompt30 candidate. Starting source:
`e01b80c`. This changes elapsed-age policy and adds native command preparation;
it does not publish a release or close the nine native-family qualification gaps.

## Contract and implementation

- Core `maxAgeUs`/`maximumAgeUs=0` now means no elapsed-age expiry. Positive
  values retain their existing boundary semantics. Future timestamps, absent
  required observations, target/configuration generations and invalidation
  remain checked. Saturating expiry is shared instead of duplicated.
- The ordinary Arduino/IDF application defaults
  `ApplicationOptions::observationMaxAgeMs` to zero; callers can opt into a
  positive age budget at `beginApplication`. Cached ages remain visible.
  Operation deadlines, post-command evidence and no-replay policy remain.
- `ESS_RS::PositionCommand` remembers requested speed, raw ramp words, endpoint
  and word order. `prepareRelative/prepareAbsolute` copy exact target bits and
  settings into the existing caller-owned `MoveContext`. The application runs
  the existing `nextMove/advanceMove` path through its one owner/reservation.
  Each operation stages `0x0021/5`, checks its acknowledgement, then sends start.
- This native path requires no prior motor-state cache or host origin. Its
  result is `ACKNOWLEDGED` with completion `NOT_OBSERVED`. It does not infer
  completion from movement that could predate a noninterrupting start, establish
  starting-speed compatibility, enable the drive, change I/O, save or retry.
  Existing observation-aware moves retain coordinate/limit checks and observed
  activity/completion. Both API levels share one sequence implementation.
- `buildStartPosition` provides checked start-only frames alongside existing
  raw FC03/06/10 codecs and position-profile builders. Native remembered intent
  and start-only are API-only; ordinary CLI moves still use observed admission.
- Re-sending parameters is not evidence of uninterrupted power. A reset between
  setup and start, an alarm, a disabled drive or an ignored busy start can still
  prevent the requested movement. Transport acknowledgement cannot promise it.

Independent review found and corrected a draft native-path completion claim:
without a prior stopped observation, RUNNING/ARRIVED could belong to an older
move. The final native path ends at confirmed start acknowledgement and has no
unused polling options. Reviewers then rechecked the correction, defaults and
Python validation. FieldCore `Rs485Task` remains a read-only reference: its
submit/result/cancel owner boundary is preserved; no framework types, scheduler
or second bus owner were introduced.

## Verification and retained failure

The native suite covers old evidence with expiry disabled, opt-in expiry,
missing/future/wrong-generation evidence, unchanged outputs, operation deadlines,
both word orders and raw target bits, independent copies of desired settings,
partial/full TX failure without start/replay, unconfirmed trigger replies and
on-time closure delivered late versus genuinely late closure. Arduino/IDF fake
application tests admit and complete an ordinary move after31seconds while
rejecting missing I/O and invalidated configuration. Installed consumers compile
and link the new API through package exports.

The first COM13 campaign failed in the Python checker, not firmware admission.
Firmware operation7 accepted31.113-second-old evidence with age0, reported
checked staging/start acknowledgement and RUNNING then ARRIVED. The old Python
checker required positive age, rejected that terminal, and correctly stopped
further session writes. A read-only reconciliation also exposed the same old
assumption in cached-health validation. Both failed JSON/JSONL logs remain:

- `build/motion_api/evidence/ordinary.*`
- `build/motion_api/evidence/reconcile.*`

An explicit new checked session confirmed stopped/zero speed, sent one direct
stop, restored `[30,100,100,60,0,250]`, released retained operation7 and probed
successfully. Raw feedback changed4881→4981; this is drive feedback, not independent
shaft observation. Evidence: `build/motion_api/evidence/explicit_cleanup.*`.
No original write was replayed. The corrected host checker supports age0 for
move/home deadlines and cached state/health while retaining provenance and
mandatory deadlines; original recorded responses are replayed only in software.

## Final measurements

The corrected finite campaign passes. The prepared script is
`build/motion_api/run_bench.py`: three finite
moves (100/250/250native increments at60RPM), a31second pre-move wait, normal/direct
stop interruptions, exact profile restoration after each phase and10final probes.
Thresholds are fixed in the script before execution: accounted terminal results,
observed first completion, retained interrupted outcomes, existing3second move
deadline, checked stopped state and exact restoration. No persistence, power
cycling, unbounded velocity, uncertain replay or endurance loop is involved.

Wire-byte lower bound at115200baud/8N1 is43bytes ×10bits /115200 =3.733ms for
19-byte setup +8-byte reply +8-byte start +8-byte reply. This excludes bus gaps,
drive processing, owner service and queueing. Start-only has16bytes =1.389ms of
wire bytes but relies on the already stored parameters. Neither figure is a
physical reaction-time guarantee.

### Recorded final image and results

Arduino timer image `build/motion_api/pio/bench_s3_load_timer/firmware.bin` SHA256:
`feb34fb50c2096c3898be0fbff3b25540b50371197589b47505a4c45bfb13989`.
Upload and hash verification passed (`build/motion_api/upload.log`). Pinned
Arduino3.3.11/IDF5.5.5, S3, node1 at115200/8N1 (actual115211baud), model4EEA,
firmware0029. No motor communication setting was changed.

Corrected evidence: `build/motion_api/evidence/ordinary_corrected.json` and
`.jsonl`; duration36.953seconds, decoded debug enabled during the campaign.

| Phase | Setup acknowledgement after admission | Start acknowledgement after admission | Result |
| --- | ---: | ---: | --- |
| 100 increments, readiness31.110seconds old | 9.586ms | 18.522ms | New activity/completion observed |
| 250 increments, normal stop | 10.973ms | 24.050ms | Interrupted outcome retained; stop observed |
| 250 increments, direct stop | 11.200ms | 24.053ms | Interrupted outcome retained; stop observed |

These are software delivery timestamps, not shaft latency. Start-ACK frame
closure upper bounds were18.236/23.727/23.643ms; median delivery24.050ms (n=3).
Stop phases also issue compatible state reads, so their bus schedules differ.
The finite sample is not a worst-case latency guarantee.

- 130 checked frames between stats snapshots, plus nine checked final
  identity/configuration/state frames; ten ordinary probes included. Transport
  failures/timeouts/RX errors/capture faults0. Both interrupted and stop results
  remained accounted; no automatic replay or transport recovery.
- Final raw position5457, enabled, raw alarm0, not running, raw speed0. Original
  profile `[30,100,100,60,0,250]` restored after every phase. Debug/load/monitor
  off, DE released, owner/results empty, no recovery requirement.
- Internal free336624bytes and PSRAM free8177196bytes unchanged across campaign;
  task stack watermark1956bytes, load task3268bytes. Maximum owner gap1868us,
  capture gap62us (85us limit), ring high-water9. Capture-section delta20.42%
  of one core; scheduler estimate CPU0/CPU1 0%/7% is a different diagnostic.
- Decoded observer display drops increased176 under console pressure; protocol
  capture drops0 and retained operation evidence complete. Passive observation
  remains bounded/lossy without consuming transaction bytes.

The full verifier passed75 registered checks, clean source/installed consumers,
four Arduino builds and four native-IDF builds in `build/motion_api/verify`.
After the Python-only correction, all five affected Python suites passed
(237+36+32+22+23cases); final clean native/package verification is recorded in
`build/motion_api/verify-final`. No runtime source changed after the full builds
or flashed image. Original failed logs remain separate from the passing run.

Commands: `python scripts/verify.py --mode full --build-dir build/motion_api/verify`
with the installed IDF5.5.5 path/Python environment supplied; then
`python scripts/verify.py --mode quick --build-dir build/motion_api/verify-final`.
The final quick run passes all75 checks and both clean package consumers.
Core ZIP `build/motion_api/verify-final/package-stage/MotorControl-RS-0.6.0.zip`
SHA256: `670304a607183d7e95cf508c1b8d59fc8efe0055ecec55ef896b62989f2faca2`.
Hardware: `python build/motion_api/run_bench.py` after the recorded timer-image
upload. Failed attempts use separate original prefixes; none were overwritten.

Independent electrical/shaft timing, physical power/disconnect faults and
multi-hour endurance remain NOT RUN. The new native-intent route has direct
native/installed-consumer tests; hardware exercises the shared staging/start
engine through the ordinary observation-aware API, not a separate native CLI.
