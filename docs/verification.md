# Software verification

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
