# Simple motion console — 2026-10-05

User-directed follow-up to `98674c9fc8a0e00a8dc64dd14718d12019583406`.
The [console guide](../console.md) now starts with `speed 60`, `accel 100`,
`decel 100`, `moveby 100 steps`. `moveto`, degrees, turns, configured millimetres,
explicit `stepsperturn`, and optional `motion stored` use the ordinary typed move
path. Advanced commands and core APIs remain available.

## Implementation and review

One finite application session obtains missing checked configuration/profile
information and refreshes state. It reuses valid configuration/profile data on
repeats. ARRIVED with residual speed causes bounded read-only state settlement;
an unsuccessful read terminates preparation. The five-second preparation bound
also caps the yielded move deadline. No move write is retried.

Desired speed/native ramps and explicit scale are separate from historical
operation results. Native ramp words are not labelled physical acceleration.
Explicit axis-scale changes replace/clear future intent; selecting another target
clears the scale declaration. Missing units/origins, unsupported encodings and
existing example bounds remain errors. There is no hidden enable, position clear,
I/O change, calibration, persistence or restart.

The wrapper owns its typed-read/move child results, reserves the same axis during
preparation, and yields all traffic through the existing owner. Priority stop
interrupts preparation only after successful urgent admission. Successful delivered
sessions may be reclaimed by the next simple move; failure and uncertainty require
explicit review/release. Output pressure cannot silently reclaim a pending terminal.
No extra transport, queue or core scheduler was introduced. FieldCore's current
`include/TunnelMonitor/rs485/Rs485Task.h` was inspected read-only; no FieldCore file
or firmware-owned type was imported.

An independent reviewer checked the preparation, owner/result lifetime, cancellation,
stop and parser integration. Review fixed a false `no_motion_sent` claim while a
trigger was in flight. Failed output links the retained child evidence and gives
the correct wrapper release command. Normal output reports drive-observed running,
arrival, alarms and uncertainty in a few lines. `@ID result CHILD` retains full
wire evidence; debug traffic remains a copied observer of the production path.

## Failed run and correction

`build/simple_cli/evidence/human_motion.json` and `.jsonl` preserve the first
campaign. Its one finite 100-increment move completed with acknowledged setup/start
and new running/arrival reports. The host evidence checker then rejected its
internal child's `command_id: 0`. This was an integration defect, not a failed move.

The children now retain the parent user's command ID. A native regression checks
the correlation. `first_run_cleanup.json` / `.jsonl` retain a separately opened,
explicit cleanup session: identity/configuration/state inspection, direct stop,
zero-speed observation, wrapper release, original-profile restore and probe. No
move was replayed. Failed evidence was preserved before the final image was uploaded.

## Final board scenario

Image: `build/simple_cli/pio/bench_s3_load_timer/firmware.bin`, 613072 bytes.
SHA-256: `715277c4b5015e40310189f54b95602ea2e6e2d27ec34f03dc46fe4922db3442`.
`build/simple_cli/upload-corrected.log` records flash hash verification. ESP32-S3,
Arduino 3.3.11 / IDF 5.5.5, COM13, node 1, 115200/8N1 (actual 115211).
ESS23-RS20 bench: raw model `0x4EEA`, firmware `0x0029`. The actual saved profile
in this session was `[30,100,100,60,0,100]`; historical reports are not substituted
for this current readback.

The finite runner was prepared before execution:

```text
python build/simple_cli/check_console.py build/simple_cli/evidence/human_motion_corrected
```

Plan: four moves of 100 command increments at 60 RPM, seven-second host command
bound, every terminal accounted, new running/arrival feedback, compact output
under 500 characters, a bounds rejection without motion, then explicit direct stop,
zero speed, saved-profile restoration, host-scale clear and three ordinary probes.
No persistent writes, power operation, indefinite velocity or endurance loop.

| Case | Result |
| --- | --- |
| Cold `moveby 100 steps` | PASS: automatic checked preparation; full setup/start. |
| Repeat `moveby 100` | PASS: no manual reads/releases; cached configuration/profile reused. |
| Explicit `stepsperturn 1000`, then `moveby 36 deg` | PASS: same checked conversion yields 100 increments. Scale is a host declaration, not an angle calibration. |
| `motion stored`, then `moveby 36 deg` | PASS: no setup write; acknowledged start and observed completion. |
| `moveto 100 steps` from the current high raw position | PASS rejection: precise example-bounds explanation and no motion command. |
| Direct stop, profile/host-scale restoration, three probes | PASS. |

The final campaign records 74 frames / 699 RX bytes, no timeouts, capture faults,
RX errors, input drops or blocked output. Maximum owner service gap 222 us; capture
gap 55 us below the unchanged 85 us guard. Internal free memory 336656 before /
336624 after (32-byte decrease); PSRAM free remains 8177196 bytes. Stack high-water
free 2084 bytes. These short-run observations do not establish a memory trend or
endurance qualification. Scheduler estimates were core 0 = 0%, core 1 = 8%; measured
capture-section work was 10693429 / 52397017 us (20.41% of one core), a separate metric.

Final position feedback 9866, speed/alarm zero, enabled/non-running, logical I/O
zero. Profile restored exactly to `[30,100,100,60,0,100]`; host scale returned to
unknown. Load/monitor/debug off, DE released, owner pending/retained/reserved zero,
no recovery requirement or queued motion.

## Verification and limits

Native fixtures cover cold preparation, exact parser failures, valid absolute
staging/start/completion, step/degree parity, stored repeat, transient nonzero speed,
explicit scale changes/clears, target changes, failed preparatory reads, local
cancellation, stop admission, blocked output, child ownership and uncertain writes.
The same application scenarios run through Arduino and native-IDF USB adapters.

The final full-verifier command and results are recorded in
`build/simple_cli/verify-corrected/summary.json`:

```text
python scripts/verify.py --mode full --build-dir build/simple_cli/verify-corrected --idf-path C:/pio/packages/framework-espidf --idf-python build/p25/idf-python/Scripts/python.exe
```

PASS: all 77 registered checks, clean source/install consumers and all eight
Arduino/IDF firmware builds. [Retained evidence archive](2026-10-05_simple_motion_console_evidence.zip)
contains both hardware campaigns, the separate cleanup, exact runner, verified
upload log and full-verifier summary. The failed hardware run is not replaced by
the corrected pass. Independent review rechecked the correlation, scale invalidation
and in-flight diagnostic fixes after correction.

Core package SHA-256:
`2ad1e52eed8b2f66418fd13e21ed722b12571e471161b7e766ee0ccd4a8b3af8`.
Evidence archive SHA-256:
`d36520d828406c50033bb0d6786691de39370b1e20e758e8a38569cfc9cfda76`.

Physical successful absolute motion was NOT RUN in this follow-up because the
existing example requires absolute starting feedback/target in 0–250 and the
current feedback is outside that range. Native fixtures exercise its successful
wire path; the test did not reset the device position to manufacture admission.
Independent shaft/angle and electrical measurements remain unmeasured. Negative
encoding, configured machine travel and other existing release gaps remain open.
The console simplification does not expand the bench envelope or claim full ESS
qualification. The package remains unpublished 0.6.0.
