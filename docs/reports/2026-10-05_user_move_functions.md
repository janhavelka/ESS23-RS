# Unit-aware ESP32 move functions — 2026-10-05

Follow-up to `1b7e6728db081d1b244a600c4aef94c69da17652`. The existing
Arduino/native-IDF application now exposes `moveBy`, `moveTo`, advanced
`submitMove`, copied `moveProgress` and explicit `releaseMove`. See the
[C++ example](../move_example.md). These application functions use the existing
core conversion, move sequence, same-axis reservation and single bus owner.
No new wire commands, device settings, retries or startup motion were added.

Host scales can be supplied through `ApplicationOptions::positionUnits`.
Initial checked configuration/state/profile reads remain necessary; automatically
performing that initial setup is a separate convenience. Age-only expiry remains
disabled by default. Absolute engineering coordinates still require an origin.
The application checks its existing small native bench envelope after the shared
unit conversion; the core does not inherit that bench limit. FieldCore's current
`Rs485Task` submit/result/cancel boundary was inspected read-only. Its types and
owner remain outside this example and the reusable core.

## Verification

`scripts/verify.py --mode full --build-dir build/user_move/verify-final
--idf-path C:/pio/packages/framework-espidf
--idf-python build/p25/idf-python/Scripts/python.exe` passes. The retained
`summary.json` records all 75 registered native checks, clean source/install
consumers and eight Arduino/IDF builds. Package version remains unpublished
0.6.0; the locally verified ZIP SHA-256 is
`0967bf2cae2a1f9038a6d7ec89b5a2cd59eb3f0fae83c247feebd624266a8798`.

New Arduino/IDF fake-application regressions exercise six unit forms yielding
the same 100-increment staging frame, explicit radians approximation, absolute
versus relative triggers, absent scales/origin, stale configuration generations,
busy admission with unchanged output ID, stop interruption, blocked console,
polling completion and retained result release. C++ calls retain the requested
unit and use the same preparation as the CLI.

The first full run (`build/user_move/verify`) exposed an existing readiness-test
expectation: native negative requests must fail at the early application guard.
Removing the blanket native-unit restriction had also removed that early sign
guard. It was restored for native step requests; converted requests retain core
encoding/range validation. The focused three tests and final full verifier pass.
The failed log remains separate; no hardware write came from that failed test.

## Short hardware regression

Uploaded Arduino timer image:
`build/user_move/pio/bench_s3_load_timer/firmware.bin`, SHA-256
`d510d7e1a15ffeef3965f1e493a6e240fac7def00d5b895c2e4c686604c69192`.
Upload/hash verification is retained in `build/user_move/upload.log`.
Platform: ESP32-S3, Arduino 3.3.11 / IDF 5.5.5, COM13, node 1,
115200/8N1 (actual 115211), ESS model 0x4EEA / firmware 0x0029.

Prepared finite procedure: `build/user_move/run_bench.py`; raw records:
`build/user_move/evidence/units.json` and `.jsonl`. The procedure fixes the
60 RPM/250-increment envelope, three-second operation deadlines, explicit stop,
zero-speed checks, exact profile restoration and ten final probes before running.

- A 36-degree motor-frame request with explicit assumed scale 1000 increments
  per turn staged 100 native increments and reported new activity/completion.
- A 250-increment request was interrupted by normal stop. Both the interrupted
  outcome and observed stop were retained, with no replay.
- Each phase restored `[30,100,100,60,0,250]`. The temporary host scale was
  restored to unknown. No motor communication, persistence or I/O setting changed.
- The 4.859-second campaign counted 96 checked frames; final identity/config/state
  reads added nine. No failed transactions, timeouts, RX or capture faults.
- Maximum owner gap 1834 us, capture gap 58 us, ring high-water nine.
  Internal free memory changed 336656 to 336624 bytes; minimum 331464 bytes,
  PSRAM free 8177196 bytes, stack free 1908 bytes. This short first-use sample
  does not establish a memory trend or endurance qualification.

Final checked state: position 5764, alarm zero, enabled, not running, speed zero,
inputs/outputs zero; saved profile restored. Load/monitor/debug off, DE released,
pending/retained/reserved owner storage empty, no recovery required and no queued
motion. Hardware exercised the ordinary CLI entry into the shared backend;
the new C++ entry itself was tested with native Arduino/IDF application fixtures.
No absolute hardware move was attempted outside the existing 0..250 bench window.
Independent shaft angle, linear travel and electrical timing remain unmeasured;
this report does not close the prior release qualification gaps.
