# First implementation verification

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
RS485 or move a motor. The build/package checks do not establish correctness of
unimplemented frame codecs or the original manual's conflicting fields.

COM13 was not opened or flashed. Motor identity, encoder/subdivision settings,
direction timing, communication and physical motion remain untested. Native
ESP-IDF firmware and ESP32-S2 consumer builds remain pending; the framework-free
core and IDF component registration do not substitute for those builds.
