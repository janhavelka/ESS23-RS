# Repeated motion setup and timing — 2026-10-05

User-directed follow-up to source `654b9874bc28ca66ede8aa384dacb1329fb30944`.
Full staging remains the default. `MoveSetup` adds `VERIFY_AND_UPDATE` and
explicit `USE_STORED` to `MoveRequest`, native `PositionCommand`, the ESP32
`moveBy`/`moveTo`/`submitMove` functions and ordinary CLI. No second owner or
conversion implementation was introduced. [Usage](../move_example.md#repeating-a-move).

## Implementation and review

One finite sequence reads `0x0021/5`, compares desired raw words and selects:
no write when equal; FC06 for one changed ramp/speed; FC10 `0x0024/2` for target
changes; full `0x0021/5` for mixed changes. Multiple scalar changes use the full
block to avoid extra round trips. Paired halves and arbitrary spans remain
forbidden. Original function PDF pages 16 and 70 (printed 14 and 68) were
visually rechecked: explicit start uses the given parameters, and the five
settings are readable/writable. Existing reviewed access windows stay unchanged.

The native object retains caller intent. The example separately remembers checked
read/acknowledged setup values with target, configuration and serial generations.
Start-only requires a match; known invalidation and uncertain failures prevent
reuse. Neither remembered intent nor read-before-start proves uninterrupted power.
No setting, enable, save or retry is implicit. The application owns state polling;
the observed path still requires a new activity/completion report. Native command
completion remains acknowledgement only. FieldCore `Rs485Task` was inspected
read-only; its owner and firmware types were not imported or changed.

Independent review caught three draft defects before hardware: write-only admission
rejected the first verification read; older responses could stamp current cache
generations; local cancellation after staging could retain a cache despite an
uncertain outcome. Corrections preserve existing write admission, require matching
original bindings and invalidate at terminal settlement. Regressions reproduce
all three, including queue pressure that leaves start unsent.

## Measured comparison

Image `build/repeat/pio/bench_s3_load_timer/firmware.bin`, SHA-256:
`ba9aa2b41c062ced1bedf09233ae02a371b2f2d6756629eb42d48cf9c761bc15`.
Upload/hash verification: `build/repeat/upload.log`. ESP32-S3 Arduino 3.3.11 /
IDF 5.5.5, COM13, ESS23-RS20 raw model `0x4EEA`, firmware `0x0029`, node 1,
115200/8N1 (actual 115211). Saved profile `[30,100,100,60,0,250]`.

Command prepared before execution:

```text
python scripts/bench_repeat.py --count 10 --out build/repeat/evidence/timing_settled --firmware build/repeat/pio/bench_s3_load_timer/firmware.bin
```

Thirty interleaved 100-increment / 60 RPM moves (ten per policy), then three
selective-update cases, each with a three-second operation deadline. Every
terminal must pass strict evidence parsing, show new activity/completion and
the expected write shape. Before each new move, bounded read-only observation
requires non-running and zero speed. Cleanup sends one direct stop, confirms
zero speed, restores the saved profile once and runs ten probes. No write replay,
power operation, persistent write, continuous velocity or endurance loop.

| Path | n | Minimum | Median | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Full setup then start | 10 | 18.328 ms | 18.395 ms | 18.552 ms |
| Read unchanged settings then start | 10 | 18.331 ms | 18.484 ms | 18.766 ms |
| Explicit start-only | 10 | 8.265 ms | 8.507 ms | 8.612 ms |

Times are firmware admission to delivered start acknowledgement, with decoded
diagnostics enabled equally. Initial configuration/profile/state reads, USB host
command submission and post-command status polling are outside that interval.
This is not shaft latency or a worst-case timing guarantee. Read-and-compare
gave no meaningful speed gain here. Start-only saved 9.888 ms / about 54% of the
median command path. Reading then updating speed (FC06) took 27.548 ms; target
pair (FC10/2) 27.525 ms; combined speed/target (FC10/5) 28.563 ms (one case each).

At 115200/8N1, wire bytes alone take 3.733 ms for full setup/start (43 bytes),
3.385 ms for unchanged read/start (39 bytes) and 1.389 ms for start-only
(16 bytes). Those arithmetic lower bounds omit turnaround, drive handling and
owner scheduling. Reading first saves only four bytes when unchanged and adds
a transaction when changes are required; it is a consistency choice rather
than a demonstrated faster default on this fixture.

## Preserved failed run and correction

`build/repeat/evidence/timing.json` and `.jsonl` retain the initial failed
aggregate. Its first two moves completed. Before the third, `read-state` id 31
reported motion word 1 (arrived, not running) and raw speed 4, position 5965.
The new test runner incorrectly required its first post-arrival sample to be
zero. It refused the next move and successfully stopped/restored the profile.
No transaction failed and no uncertain motion was replayed.

The correction uses the existing `observe_stopped` helper: at most ten reads,
50 ms apart, within five seconds. It preserves the zero-speed requirement.
Motion/status and speed are separate non-atomic observations; no claim is made
about which physical settling or drive-internal update mechanism caused the
brief disagreement. Regression replay covers speed 4 followed by 0, budget
exhaustion, uncertain motion, failed stop and lost framing. Primary failures
remain recorded even if cleanup also fails.

The corrected campaign (`timing_settled.json` / `.jsonl`) passes all 33 moves
in 51.203 seconds, including the original full-setup and read/compare cases.
The 518-frame statistics delta has zero failed transactions, timeouts, RX errors
or capture faults; nine final identity/config/state frames follow that snapshot.
All requested terminal results were retained and checked. Passive debug dropped
180 output records under bounded observer pressure; raw terminal transaction
evidence was retained. Debug output is not a lossless wire-analyzer capture.

## Resources, final state and verification

Owner service gap maximum 1133 us; capture gap 58 us below the unchanged 85 us
guard; RX ring high-water nine. Internal free memory before/after 336624 bytes,
minimum 331464; PSRAM free 8177196. Stack high-water free 1828 to 1796 bytes.
Scheduler busy estimates were core 0 = 0%, core 1 = 13%; instrumented capture
section time 18919490 / 93195029 us = 20.30% of one core over its lifetime.
These are different measurements, and a short campaign is not a leak/endurance test.

Final profile `[30,100,100,60,0,250]`, position 9265, speed/alarm zero, enabled,
not running, logical inputs/outputs zero. Load/monitor/debug off, DE released,
owner pending/retained/reserved zero, no recovery and no queued motion.
Independent shaft response/angle, electrical timing and multi-hour endurance
remain unmeasured/deferred.

Full verifier passed: `scripts/verify.py --mode full --build-dir build/repeat/verify
--idf-path C:/pio/packages/framework-espidf
--idf-python build/p25/idf-python/Scripts/python.exe`. It ran all 75 registered
checks, clean source/install consumers and eight Arduino/IDF builds. The final
37-case motion-campaign suite additionally covers the runner correction.
Core tests cover both word orders, every selected write shape, invalid setup,
wrong/stale events, malformed/unconfirmed reads, cancellation, deadlines and no
trigger after failure. CLI/Python tests validate full read CRC and selected shapes.
Package remains unpublished 0.6.0; verified ZIP SHA-256:
`f4bbe531622082a40085f487fc6607c12a53242b56cee8ac0c007641f664ef74`.
