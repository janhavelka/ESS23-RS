# Repeatable verification and core packages

Run from the repository root with Python 3.10+, CMake/CTest 3.26+, Ninja and
GCC or Clang with GNU/LLVM `nm` available to CMake. The verifier is qualified
on Windows MinGW GCC and hosted Linux GCC/Clang; MSVC verification remains
unqualified. Tests require unoptimized Python: do not set `PYTHONOPTIMIZE`.
Install the offline PDF checker dependencies once:

```sh
python -m pip install -r scripts/requirements-verification.txt
python scripts/verify.py --mode quick
python scripts/verify.py --mode full
```

The core build still supports CMake 3.16. The verifier uses CTest's
[fail-on-zero-tests option](https://cmake.org/cmake/help/latest/manual/ctest.1.html#cmdoption-ctest-no-tests),
which requires 3.26.

Quick runs all registered Release native/Python suites, generated register and
version checks, operation inventory, offline reference hashes and local Markdown
links. It then exports the reviewed core-only package, checks every ZIP member
and its bytes, extracts that archive, builds/installs it, and
builds/runs independent source and exact-version `find_package` consumers.
Every public header compiles alone in strict C++11 and C++17; C++17 consumers
disable RTTI. Both codec-only consumers must omit descriptive catalogue strings
and symbols. The static CMake target exports the configured install include path.
No examples, Python, vendor files or sibling repository are package dependencies.

Full additionally builds the four pinned Arduino environments and native IDF
S3 firmware, core-only S3/S2 consumers and the portable S2 application fixture.
Export the pinned ESP-IDF 5.5.5 SDK/tool environment first. `--idf-path PATH`
and `--idf-python PATH` select an existing SDK and its Python environment.
PlatformIO must already be installed; Windows falls back to the repository's
managed-Core wrapper. No SDK or tool is installed silently.

CI separates the same full checks with `--firmware arduino` and `--firmware idf`;
each explicitly selected run also executes quick checks. Hosted Ubuntu GCC and
Clang jobs cover compiler differences. Firmware jobs use the pinned PlatformIO
platform and IDF container. They compile firmware and do not access COM13,
flash hardware, run motion or assert hardware qualification. Local Windows GCC
verification is recorded separately from hosted CI execution.

Every run uses a new `build/verify/<timestamp>` directory, or a new directory
chosen with `--build-dir PATH`. Existing output is rejected. Logs and
`summary.json` retain exact commands, durations, failure status and package
SHA-256. The ZIP uses stable entry order, timestamps and permissions. A missing
tool/input, unregistered suite, empty/skipped test or failing command is a
failure. Registration checks require the actual interpreter, script, native
executable and check arguments; per-test Python optimization overrides fail.
CTest properties that turn a failing exit into PASS are also rejected.
Reference checks are offline; original vendor bytes remain separately
licensed. Local links are checked in maintained documents, excluding historical
reports/PDF extracts; external URLs, anchors and explicitly named sibling
reference repositories are not availability claims.

For a clean check, use a fresh checkout or a staging copy of tracked source
plus the reviewed changes, with no `.pio` or build outputs. Tool download caches
may remain external. A verifier run never deletes existing builds, evidence or
flash backups.

The exported ZIP contains only `include`, `src`, root CMake and metadata,
license, README and changelog. It can be unpacked anywhere and consumed without
repository-private helpers:

```sh
cmake -S MotorControlRS -B core-build -G Ninja -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/absolute/core-install
cmake --build core-build
cmake --install core-build
cmake -S consumer -B consumer-build -G Ninja -DCMAKE_PREFIX_PATH=/absolute/core-install
cmake --build consumer-build
```

An independent consumer needs only its own CMake project/main and
`find_package(MotorControlRS 0.6.0 EXACT CONFIG REQUIRED)` followed by linking
`MotorControlRS::MotorControlRS`. The checked consumer fixture also supports
`MOTORCONTROLRS_SOURCE_DIR` and `MOTORCONTROLRS_HEADERS_DIR` for direct source
and individual-header checks. Hardware and endurance dispositions remain in
their owning reports; a green verifier is software/build evidence.

## Earlier verification records (historical)


## Prompt15 optional digital I/O

[Implementation/review and current-image evidence](reports/ess_release_15_2026-10-04.md)
passes37/37 CTest checks,173 Python host/IO units,16 actual I/O console parity
fixtures, the installed C++11 consumer and allfour firmware environments.
A190-frame COM13 campaign performs13 explicit passive/unloaded settings updates
with checked readbacks and restores every original word. No external switch/load
physical function, electrical output state or new motion qualification is claimed.
The first missing-accepted-line failure and its native/hardware correction are
retained; uncertain motor writes are not automatically replayed.

## Prompt08 bounded actions and stop

[Action verification](reports/ess_release_08_2026-10-04.md): 21/21 CTest suites
pass under strict C++11 Release with warnings as errors, including 118 Python
harness cases and 13 inventory cases. Installed consumer and all four firmware
builds pass. Final COM13 campaign passes 28 read-only frames and five zero-TX
action gates. Physical action/stop and electrical qualification remain NOT RUN.

## Fresh prompt07 audit

[Fresh independent review](reports/ess_release_07_audit_2026-10-04.md) corrects
relative displacement limits, zero-radian exactness, cancellation onto zero and
strict fractional parsing. All 20 native suites, 109 Python cases, installed
C++11 consumer and four firmware builds pass, plus 45,000 independent rational
oracle cases. The corrected image passes 23 read-only COM13 frames with no
transport errors and unchanged drive settings; motion and travel remain unqualified.

## Prompt07 exact target preparation

[Preparation verification](reports/ess_release_07_2026-10-04.md) passes 20 native
suites, 107 Python cases, the installed C++11 consumer and all four firmware
builds. Independent integer oracles pass 13,776 rounding/origin and 3,600
large-rational allowance cases in both extended and binary64 arithmetic modes.
COM13 host API checks and 23 checked read-only frames pass with no device settings
change. Origin/source/physical travel and motor execution remain unqualified.

## Fresh prompt06 audit

[Fresh independent review](reports/ess_release_06_audit_2026-10-04.md) corrects
superseded feedback interpretation confidence and strict cached JSON validation.
19 native suites,103 Python cases, installed C++11 consumer and all four firmware
builds pass. Current-image COM13 state/poll/configuration/probe checks pass50
frames with zero transport errors; physical units/source and motion remain unresolved.

## Prompt06 state and health

[Typed state/cache verification](reports/ess_release_06_2026-10-04.md) passes19
CTest suites, the installed C++11 consumer, all four firmware builds and stationary
COM13 state/health/polling checks. Per-block age, failed refresh retention,
generation mismatch, urgent scheduling and cancellation have native regressions.
Actual feedback units/source and physical motion completion remain unresolved.

## Fresh prompt 03 audit

The [fresh independent audit](reports/ess_release_03_audit_2026-10-04.md) fixes
result-view reuse, expired adapter recovery, deadline diagnostics, output/cache
coupling, valid cache retention and strict Python retained-result inspection.
16/16 CTest suites, 8 generator cases, 70 Python cases and all three firmware
environments pass. Current-image COM13 campaigns pass 37 checked reads plus
the expected pre-TX 20-ms-delay failure, retained inspection and explicit recovery.
Electrical, physical USB saturation/disconnect, cache-off, motion/stop and
extended soak remain unqualified.

## Release prompt 03 owner/console integration

Strict C++11 Release build with assertions and warnings as errors: 16/16 CTest
suites, 8 generator cases and 66 Python cases pass. All three probe/load firmware
environments build. Final timer-image COM13 campaigns passed 35 checked reads,
including active console queries, competing load, retained results, delayed
observation ages and explicit recovery. The deliberate upper 20-ms setup-delay
failure is retained and reproduced; no timeout or retry policy was weakened.
See [prompt 03 evidence](reports/ess_release_03_2026-10-03.md). Electrical,
cache-off, motion/stop and extended soak qualification remain open.

## Platform boundary and optional I/O audit

The current standalone environments are `bench_s3_units`, `bench_s3_probe`,
`bench_s3_load_poll` and `bench_s3_load_timer`. The adapter is `Esp32S3Uart`,
with application-supplied pins and polarity. Earlier entries retain historical
names and results. See the [scope audit](reports/2026-10-03_platform_scope_audit.md)
for the cleanup, independent review, build and new-image bench evidence.

## Repository rename preparation

On 2026-10-03, a clean copy named `MotorControl-RS` passed all 13 CTest suites
and built `e2_s3_units` and `e2_s3_load_timer`. PlatformIO's local dependency
links and regenerated VS Code paths pointed to that copy. No hardware was
flashed for this metadata/documentation change. See the
[rename guide](repository_rename.md) for the tested move/cache procedure.

## Capture and load audit, version 0.6.0

Checks on 2026-10-03: Release build with warnings as errors and **13/13 CTest
suites**, including 44 Python harness cases, passed. Native SDK fixtures compile
the real E2 adapter and runner and independently schedule wire arrival, capture
and owner service. Tests cover timer lifecycle failures, delayed DE observations,
FIFO/ring overflow, late replies and missing/contradictory timing evidence.

E2 polling and timer load builds compile. Read-only COM13 campaigns, memory/CPU
measurements, audit fixes and remaining hardware limits are recorded in the
[capture/load report](reports/2026-10-03_capture_load.md). These checks do not
establish external timing, cache-off tolerance or motion qualification.

## Implementation audit, version 0.5.1

Checks on 2026-10-03: clean Release build with warnings as errors and **12/12
CTest suites passed**, including eight adapter groups, five actual-application
groups and 28 Python fake serial tests. Both ESP32-S3 examples built. COM13 ran
20/20 checked probes, cached monitoring with unchanged bus counters, and explicit
timeout/interlock/recovery/reset/alias checks. All 14 retained source hashes match.

See the [audit report](reports/2026-10-03_audit.md), measured metrics and fault
transcript for defects, fixes and limits. External timing qualification, exact
motor identity and motion remain open. Historical results below retain the
versions and scope in which they were obtained.

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
