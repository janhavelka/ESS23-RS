# Software verification

## E2 adapter and read-only probe, version 0.5.0

Checks on 2026-10-03:

| Check | Result |
| --- | --- |
| CMake Release and CTest | 11/11 passed, including actual adapter code built against SDK fakes, console and Python harness |
| Runner | 20 groups, including interval ambiguity, incomplete capture, explicit reply turnaround and prior 1000-transaction fake soak |
| E2 adapter fakes | Six groups cover ownership/init errors, physical TX versus FIFO empty, interval capture, faults/recovery/storage and full runner probe |
| Console | Eight groups cover input/correlation bounds, no side effects on invalid input, cached health and retained raw/timing evidence |
| Python harness | 24 fake serial tests, including framing/deadlines/reset detection, explicit repeated probes and passive watch |
| Arduino headers | Fixed DEFAULT/DISABLED macro collisions; native macro regression and Arduino-first include build pass |
| ESP32-S3 | Probe firmware and existing offline units preview build with the pinned platform |
| Bench | 100 consecutive parsed probes, later 20-probe run, explicit timeout/recovery, cached watch/staleness and local counter reset |
| Memory | Interval runner: 320 bytes native / 256 ESP32-S3; trace entry 32 bytes. App storage actually allocated in PSRAM; runtime metrics retained |

See the [bench report](reports/2026-10-03_e2_probe.md), its metrics and fault
transcript. Strict native warnings pass. These results do not establish exact
motor identity, external timing bounds, motion, native ESP-IDF firmware or
FieldCore integration. COM13 was deliberately flashed after firmware backup;
historical sections below describe earlier blocks in which it was untouched.

## Native standalone runner, version 0.4.0

Checks on 2026-10-03:

| Check | Result |
| --- | --- |
| CMake Release and CTest | Passed, 8/8 including the new runner suite and all existing core/generated-file checks |
| Runner scenarios | 17 test groups covering fake wire timing, exceptions, codec handoff, fragmentation, overflow, drain, cancellation, echo, faults, recovery, storage and diagnostics |
| Repeated use | 1000 deterministic transactions on the same runner/buffers; normal/exception mix, varied gaps and polling, clock above UINT32_MAX, exact counters, trace overwrite and no replay |
| Deadline/framing regressions | Physical TX timestamp distinct from service time; final t3.5 included in response budget; lagging capture cannot become a drive timeout; busy wire blocks TX admission/enqueue |
| Fault regressions | Short enqueue is never resumed; cancellation drains accepted bytes; failed recovery remains interlocked; TX hold respects physical idle despite deadline failure; a later clock error preserves the completed transaction |
| Bounded service | Tests enforce at most 64 read callbacks in one poll, including DRAIN-to-HOLD transitions with buffered echo |
| Native warnings | Runner/tests compile and run with C++11, `-Wall -Wextra -Wpedantic -Werror` |
| ESP32-S3 compile | Runner translation unit compiles with installed Xtensa GCC, C++11, `-Os -fno-exceptions -fno-rtti` and strict warnings; this is not a UART adapter or full firmware test |
| Memory | Runner object: 304 bytes native, 240 bytes ESP32-S3; trace entry: 24 bytes on both. Caller buffers/rings measured separately in the runner guide |

The runner and test adapter perform no hardware I/O. COM13 was not opened or
flashed. Actual UART error/timing capture, DE polarity/latency, PSRAM placement
and motion behavior remain unqualified. The existing offline Arduino preview
does not run the new helper. No new package/API is imposed on FieldCore or
the core library; this implementation lives in the repository's example layer.

See [the runner guide](runner.md) for callback obligations, retained evidence,
recovery preconditions and the memory budget. The
[platform review](reference/10_runner_platform_review.md) records FieldCore
reuse and the future ESP32/Python work.

## Rename and full audit, version 0.3.0

Checks on 2026-10-03:

| Check | Result |
| --- | --- |
| CMake Release build and CTest | Passed, 7/7: units, catalogue, codec, preview, two generated-file checks and gap-ledger rejection tests |
| Unit regressions | Motor-side conversions without gearing; linear encoder conversion without lead; missing crossed scales; both polarities and directions; small-value scale/time conversion |
| Binary64 arithmetic | Units pass strict C++11 builds with normal MinGW `long double` and `-mlong-double-64`, exercising the intermediate-underflow fix |
| FC10 windows | Independent position, speed and homing request/echo fixtures added alongside the original pair; output capacities and rejected neighbouring/subset windows checked |
| Manual defect | P16's extra-byte request has a valid CRC but invalid payload length; corrected five-word request has its own literal fixture |
| Register access | All 65536 addresses checked against descriptors; every start/count read window through 0x013F cross-checked against the compact access map |
| Gap ledger | Complete 242-word map plus 78 undocumented words across 15 gaps; negative tests reject missing/overlapping gaps and incorrect reserved/access categories |
| Public headers | All 8 renamed headers compile independently with C++11, `-Wall -Wextra -Wpedantic -Werror` |
| Source package and installed consumer | MotorControl-RS archive contains both private headers, excludes references and old include paths; standalone build/install and `find_package(MotorControlRS)` consumer pass |
| Codec footprint boundary | Installed codec consumer links the compact access map; symbol inspection confirms descriptive catalogue and lookup are absent. This is not a target-MCU size measurement |
| E2 ESP32-S3 preview | Renamed 0.3.0 Arduino build passes; 325980 flash bytes and 22800 static RAM bytes for this offline units/catalogue preview |
| Source integrity | All 9 original reference hashes and all 5 contrasting manufacturer hashes match their manifests; local documentation links checked |
| Independent review | Final codec, access-map and generator review found no further correctness defect; original FC10 page images and complete appendix audited |

The [timing audit](reference/09_timing_and_gap_audit.md) records document facts
and calculated times separately from missing firmware guarantees. No numeric
response/save/startup bound or undocumented register meaning was invented.
COM13 was not opened or flashed. This block does not qualify hardware behavior,
native ESP-IDF firmware, ESP32-S2 firmware or motion workflows.

## ESS codec block, version 0.2.0

Checks on 2026-10-03:

| Check | Result |
| --- | --- |
| CMake Release build and CTest | Passed, 6/6: units, catalogue, codec, preview and both generated-file checks |
| Independent codec fixtures | Literal ESS manual frames plus a forward-polynomial CRC oracle independent of the production reflected-polynomial implementation |
| Frame validation | Expected address/function/count, complete normal/exception lengths, all truncations, overlong/impossible lengths, byte counts, CRCs, both write echoes and error precedence |
| Raw exception handling | All 256 raw exception-byte values preserved; malformed/wrong-function/bad-CRC exceptions rejected |
| Request policy and outputs | All 256 node-byte values, reviewed map access, gaps/reserved/unknown access, paired-write rejection, FC10 window, byte/word capacities, nulls, overlap and unchanged error outputs |
| Probe and word conversion | Exact minimal probe frame, unknown raw model retention, both word orders, invalid order and signed/unsigned 32-bit boundaries |
| Public headers | All 8 compile independently with C++11, `-Wall -Wextra -Wpedantic -Werror` |
| Independent source review | Codec/tests reviewed against original ESS PDF page images; no blocking issue found |
| Clean source package | PlatformIO archive includes the private RTU helper and excludes vendor/reference downloads; standalone CMake build/install passed |
| External package consumer | Separate installed-package program built and ran probe construction and synthetic reply parsing |
| E2 ESP32-S3 build | Offline Arduino preview built; the new codec translation unit compiled with the pinned Xtensa toolchain |
| Reference integrity | All 9 original hashes unchanged; all 5 new manufacturer PDFs match the separate source/hash manifest |

The embedded preview still exercises units/catalogue only; it does not call
the codec on real data or initialize the motor UART. The native codec tests
and external consumer use fixtures. COM13 was not opened or flashed. No drive
timing, state, movement or persistent effects have been qualified. Native
ESP-IDF firmware and ESP32-S2 builds remain pending.

Run the current native suite with the CMake/CTest commands below. Verify the
additional manufacturer snapshots offline with
`python scripts/prepare_serial_contrasts.py --check`.

## Initial foundation, version 0.1.0

Software checks on 2026-10-02 for version 0.1.0:

| Check | Result |
| --- | --- |
| CMake Release build, MinGW-w64 G++ 15.1.0 | Passed; library and tests use C++11 |
| CTest | 5/5: units, catalogue, preview, version generation, catalogue generation |
| Every public header included alone | 7 headers passed C++11 with warnings treated as errors |
| Native unit behaviour | Steps/angles/travel, gear ratios, encoder bases/polarities, rpm/s and squared time units, missing/invalid configuration, precision bounds, output preservation, exact integer boundaries |
| Catalogue behaviour | All 65536 possible register addresses checked against independently transcribed intervals; pairs, gaps, reserved words, indexed limits and key native choices checked |
| CMake installed consumer | Separate `find_package` application compiled, linked and ran unit conversion plus register lookup |
| Source archive | Core-only PlatformIO package contains no vendor/standards downloads; standalone CMake build checked |
| E2 ESP32-S3 preview | Compiled with pioarduino 55.03.311, Arduino 3.3.11 and Xtensa GCC 14.2.0 using the N16R8 board recipe |
| Reference preservation | All 9 original source files match the recorded SHA-256 hashes |

Run repository checks with:

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

On Windows the example build uses `.\scripts\pio.cmd run -e e2_s3_units`.
The preview exercises units and catalogue metadata only. It does not initialize
RS485 or move a motor. Those initial build/package checks did not establish
frame-codec correctness or resolve the original manual's conflicting fields.

COM13 was not opened or flashed. Motor identity, encoder/subdivision settings,
direction timing, communication and physical motion remain untested. Native
ESP-IDF firmware and ESP32-S2 consumer builds remain pending; the framework-free
core and IDF component registration do not substitute for those builds.
