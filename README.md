# MotorControl-RS

Framework-independent C++11 serial motion library with exact units/target
preparation, checked ESS Modbus RTU codecs and bounded typed operations.
Applications own UART/DE, clocks, scheduling, memory, caches and recovery.
The core performs no I/O, allocation or automatic retry and requires no
Arduino, ESP-IDF or FieldCore header. Namespace/include/CMake identity is
`MotorControlRS`; package name is `MotorControl-RS`.

**0.6.0 partial candidate, unpublished.** This development version is retained;
identify the candidate by its source commit and artifact hash, not version alone.
Typed native coverage is incomplete. Functional qualification covers the secured
free-shaft ESS23-RS20 at node1/1152008N1, raw model `0x4EEA`, version `0x0029`,
on Arduino/native ESP-IDF ESP32-S3. Neither raw code has a resolved universal
model/firmware mapping. ESS23-RS10 is documented but not bench-qualified; other
manufacturers are unimplemented. S2 is core/portable compile-only evidence with
no qualified S2 UART adapter. No industrial certification is claimed.

The recorded subset includes positive finite native positioning, normal/direct
stop, enable/release, reads and selected reversible stored settings. It does not
qualify every motion mode, calibrated shaft/travel units, electrical timing,
communication-loss stopping, persistence/restart survival or endurance.
Cache-off capture is unsupported; capture costs about20-21% of one S3 core.
A write echo is not completion; local cancellation is not a motor stop. Lost
write acknowledgements can leave unknown execution and must not be replayed
implicitly. External wiring, drive assignments and observed levels are distinct.

Observation age expiry is off by default: core `maxAgeUs`/`maximumAgeUs=0` and
standalone `ApplicationOptions::observationMaxAgeMs=0`. Nonzero values opt in to
expiry. Missing evidence, invalidated generations, transaction deadlines and
new-event completion requirements still apply. Retained observations do not
prove uninterrupted motor power or an unchanged physical state.

[Candidate scope/gates](docs/release_candidate.md),
[getting started/troubleshooting](docs/getting_started.md) and
[API/CLI inventory](docs/ess_api_cli_coverage.md) describe current behavior.
[FieldCore handoff](docs/fieldcore_handoff.md) proposes later integration only.

## Core ZIP consumption

The ZIP contains include/src, root CMake, metadata, MIT license, README and
changelog. The documentation links, firmware, examples, tests, scripts and
vendor references on this page belong to the full checkout and are absent from
the ZIP. They are not needed to build or consume the core. Public headers
contain the API signatures, Doxygen lifetime/units/prerequisite/error contracts.

With CMake3.16+ and a C++11 compiler, unpack to `MotorControl-RS`:

```sh
cmake -S MotorControl-RS -B core-build -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/absolute/core-install
cmake --build core-build
cmake --install core-build
```

Create `consumer/CMakeLists.txt` with no repository-private helpers:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MotorConsumer LANGUAGES CXX)
find_package(MotorControlRS 0.6.0 EXACT CONFIG REQUIRED)
add_executable(motor_consumer main.cpp)
target_link_libraries(motor_consumer PRIVATE MotorControlRS::MotorControlRS)
set_target_properties(motor_consumer PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
```

Create `consumer/main.cpp`:

```cpp
#include <MotorControlRS/profiles/ess_rs/Codec.h>
int main() {
    uint8_t bytes[8] = {};
    return MotorControlRS::ESS_RS::buildProbe(1, bytes, sizeof(bytes)) == 8 ? 0 : 1;
}
```

```sh
cmake -S consumer -B consumer-build -DCMAKE_PREFIX_PATH=/absolute/core-install
cmake --build consumer-build
```

Run the resulting `motor_consumer` executable. It constructs bytes and sends
nothing. A source consumer may use `add_subdirectory` and the same target.
Root CMake also supports native IDF component consumption; that alone is not
standalone firmware or board-adapter support.

## Units API

Position, velocity and acceleration preferences are independent. Supported
spatial units are command steps, motor full steps, identified encoder counts,
turns, degrees, radians and configured millimetres. Time denominators can be
seconds, minutes or milliseconds, including rpm/s.

```cpp
#include <MotorControlRS/MotorControlRS.h>
#include <MotorControlRS/profiles/ess_rs/Defaults.h>

using namespace MotorControlRS;
UnitConfig config = ESS_RS::makeBenchUnitConfig();
config.settings.position = PositionUnit::DEGREES;
config.settings.velocity = VelocityUnit(PositionUnit::TURNS, TimeUnit::MINUTE);
config.settings.acceleration = AccelerationUnit(PositionUnit::RADIANS);

UnitConversion result;
Status status = convertAcceleration(
    6.283185307179586, config.settings.acceleration,
    AccelerationUnit(PositionUnit::STEPS), config, result);
// On success: approximately 1000 command steps/s^2 with this bench configuration.
```

`convertDisplacement`, `convertVelocity` and `convertAcceleration` use explicit
input/output units and caller-owned rational scales. They check invalid scales,
missing conversion dependencies, finite values, magnitude and arithmetic error;
outputs stay unchanged on error. Exact native integer range checks avoid
floating-point conversion. The shared [target preparation](docs/axis_preparation.md)
adds origins, wrapped paths and quantization. Device-specific physical ramp
encoding remains unresolved.

Gearing and linear lead are required only when crossing the corresponding
motor/load/travel boundaries. Motor steps-to-full-steps conversion needs no
load gearing; linear encoder counts-to-millimetres needs no screw lead.

`makeBenchUnitConfig()` assumes 1000 command steps per motor turn, 4000 decoded
encoder counts per motor turn and direct 1:1 coupling. Both model datasheets
specify 1.8° full steps, giving 200 full steps per turn. Linear travel has no
default lead. These settings perform no device writes and are not readback.
See [encoder evidence and examples](docs/reference/06_encoder_units.md) for
provenance and the distinction between encoder counts and ESS position feedback.

## ESS module

[Registers.h](include/MotorControlRS/profiles/ess_rs/Registers.h) and
[Types.h](include/MotorControlRS/profiles/ess_rs/Types.h) expose register addresses,
immutable descriptors, native choices and bounded indexed lookups. The
[catalogue](docs/reference/05_ess_register_catalog.md) covers 221 logical records
and 242 register words, including I/O, tuning, homing, all sixteen stored
position/speed segments and explicitly reserved entries.

[ess_rs_registers.json](docs/reference/ess_rs_registers.json) is the single
transcription source; the generator produces the C++ tables and readable
inventory. Source conflicts, unknown signed encodings, scaling and application
semantics stay visible. Metadata coverage does not imply command implementation.
Generic Modbus or another manufacturer's registers are not substituted for ESS.

## ESS wire API

[Codec.h](include/MotorControlRS/profiles/ess_rs/Codec.h) builds FC03/FC06/FC10
requests into caller-owned byte buffers and checks complete replies against
the expected slave, function, length, count, CRC and write echo. Errors leave
payload outputs unchanged; the parsed word count resets to zero. `Status`
preserves raw device exceptions separately from malformed frames and CRC errors.

```cpp
#include <MotorControlRS/profiles/ess_rs/Codec.h>

uint8_t request[8];
const std::size_t length = MotorControlRS::ESS_RS::buildProbe(1, request, sizeof(request));
// length == 8; request is 01 03 00 00 00 01 84 0A. Nothing is transmitted.

// A synthetic complete reply for an offline parsing example:
const uint8_t reply[] = {1, 3, 2, 3, 5, 0x78, 0xB7};
uint16_t model = 0;
MotorControlRS::Status result = MotorControlRS::ESS_RS::parseProbe(reply, sizeof(reply), 1, model);
// Success publishes raw model 0x0305; it does not confirm a connected motor's identity.
```

FC03 accepts up to 16 documented readable words, excluding gaps/reserved/unknown
access. FC06 accepts documented writable single words; split writes to paired
fields are rejected. FC10 accepts four documented start/count windows:
`0x0024/2`, `0x0021/5`, `0x001D/3` and `0x0031/6`. These supported windows do
not establish a device-wide maximum. The malformed position example on manual
p16 is corrected in the builder and covered by an independent frame fixture.
Builders validate raw access/framing, not register-value meaning, motion limits,
readiness or persistence. Typed identity/configuration reads are implemented;
selected stopped-state setting changes use the installed
[driver settings API](docs/ess_driver_settings.md). The installed [position API](docs/ess_position.md)
implements finite relative/absolute motion and wrapped orientations. The installed
[action API](docs/ess_actions.md) prepares bounded enable/release, alarm-clear
and explicit normal/direct stop operations. It separates acknowledgement from
reported completion; physical action admission checks actual operation and transport prerequisites,
without requiring an analyzer or a special firmware mode.

For direct device-native positioning, `ESS_RS::PositionCommand` remembers the
desired speed, raw ramp words, endpoint and word order. Each
`prepareRelative`/`prepareAbsolute` copies that intent and exact target bits
into a caller-owned `MoveContext`; the existing `nextMove`/`advanceMove` stages
the profile, then starts only after a confirmed staging acknowledgement.
Success is `ACKNOWLEDGED` with completion `NOT_OBSERVED`. This API-only route
needs no prior observations and invents none; typed state reads provide separate
feedback. `buildStartPosition` is the lower-level start-only frame builder for
an already stored target/profile. Neither route enables the drive, changes I/O,
saves settings or retries implicitly. The ordinary CLI keeps the
observation-aware `prepareMove*` path. See the [position API](docs/ess_position.md).

The probe reads the read-only model word at `0x0000`; no consuming side effect is
documented. Its successful reply is seven bytes. Pure 32-bit helpers require an
explicit word order; signed helpers specify two's complement without asserting
that an unresolved ESS field uses it. See [codec evidence and limits](docs/reference/01_implementation_reference.md#implemented-codec-scope).

Small private RTU helpers handle byte packing and CRC. Profile policy stays in
ESS, informed by [five contrasting manufacturers](docs/reference/08_serial_protocol_review.md).
Access checks use a generated 320-byte table; basic codec use does not link
the descriptive catalogue. The same source ledger accounts for all 78
undocumented words in 15 gaps through `0x013F` and prevents reading across them.
Applications still own transport and response correlation: local FC06 echo and
a drive acknowledgement have identical bytes, and write acknowledgement does
not establish motion completion. No codec retries, clocks or I/O are hidden.
The [timing audit](docs/reference/09_timing_and_gap_audit.md) records actual
8N1 character length, RTU gaps, stop-ramp behavior and the response/persistence
deadlines that the manuals leave unspecified.

## Build and preview

The example-owned [RTU runner](examples/common/RtuRunner.h) exchanges frames
through bounded callbacks with supplied wire timing. It owns no allocations
and accepts caller-provided TX/RX and optional trace storage. Tests use a fake
adapter; ESP32-S3 hardware tests use the separate probe build below. See the
[runner guide](docs/runner.md) for deadlines, echo/recovery rules and memory sizes.

With CMake and a C++11 compiler, build and run the native tests and unit preview:

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

Use another available CMake generator if Ninja is absent. An application can
use `add_subdirectory` and link `MotorControlRS::MotorControlRS`, or install the CMake
package and use `find_package(MotorControlRS CONFIG REQUIRED)`. The root CMake file
also supports ESP-IDF `EXTRA_COMPONENT_DIRS`. A clean core-only IDF consumer
compiles all public headers without examples or vendor resources; the native
standalone firmware has finite read/load/motion/stop evidence for its recorded subset. Public headers require no
framework headers.

The same [unit preview](examples/units_preview/main.cpp) builds for the
ESP32-S3 bench with 16 MB flash and 8 MB PSRAM:

```powershell
.\scripts\pio.cmd run -e bench_s3_units
```

Build the standalone console with `.\scripts\pio.cmd run -e bench_s3_probe`.
Startup sends no motor command; explicit reads/actions/settings create traffic. See the
[guide](docs/esp32_probe.md) for uploading, JSONL commands, Python stress/watch
tools, PSRAM placement and qualification limits, and the
[bench report](docs/reports/2026-10-03_e2_probe.md) for measured results.

This preview prints conversions to USB; it does not initialize the motor UART
or send commands. Board pins are explicitly TX47, RX48, DE21 in
[BoardPins.h](examples/common/BoardPins.h). See the
[bench configuration](docs/reference/04_esp32_bench.md) for electrical and SDK
details. Builds do not upload to
COM13. Preserve the existing firmware before a later bench upload.

Generated files are checked with:

```sh
python scripts/generate_version.py check
python scripts/generate_ess_registers.py --check
```

## Checkout resources and remaining coverage

The [full API/CLI coverage matrix](docs/ess_api_cli_coverage.md) retains nine
named gaps: arbitrary position-profile candidates/archive restoration,
positioning start-speed writes, standalone velocity/homing parameter reads,
homing auxiliary choices/remaining methods, nonzero offsets, early collision
access and position-interruption options. Eighteen paired setters (two limits,
16 stored pulse targets) remain guarded. No fictional serial segment start,
torque/current motion or guessed acceleration formula fills these gaps.

- [Interactive console](docs/console.md), [Arduino](docs/esp32_probe.md),
  [native ESP-IDF](docs/esp_idf_probe.md) and [finite scenarios](docs/bench_scenarios.md).
- [Measured qualification](docs/reports/ess_release_29_2026-10-05.md),
  [verification/CI](docs/verification.md), [roadmap](docs/roadmap.md),
  [backlog](docs/backlog.md) and [engineering guidance](AGENTS.md).
- [Architecture](docs/architecture.md), [axis](docs/axis_contract.md),
  [profile](docs/profile_contract.md), [CLI](docs/cli_contract.md) and
  [discovery](docs/discovery_contract.md) contracts.

Canonical metadata is prepared for `janhavelka/MotorControl-RS`. The working
remote remains `janhavelka/ESS23-RS` until the new endpoint exists; follow
[the rename guide](docs/repository_rename.md), preserving bench evidence/backups.
`library.json` is version authority. Vendor references retain separate licensing
and original hashes/provenance; they are excluded from core artifacts. CANopen
and other manufacturers remain separately scoped future implementations.
