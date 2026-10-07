# MotorControl-RS

C++11 library for RS485 motor control. Version **1.0.0** implements the ESS-RS
profile, exact target preparation, checked Modbus RTU codecs and bounded typed
operations. Applications own transport, time, storage and scheduling. The core
performs no I/O, allocation or automatic retry.

The repository also contains a shared Arduino/native ESP-IDF ESP32-S3 console,
bus owner and finite Python bench tests. These are application examples, not
dependencies of the installed library.

Start with [getting started](docs/getting_started.md), the
[interactive console](docs/console.md) or the [C++ move example](docs/move_example.md).
See [all documentation](docs/README.md), [changelog](CHANGELOG.md) and
[1.0.0 scope and evidence](docs/releases/1.0.0.md).

## Supported scope

| Area | Included |
| --- | --- |
| Core | Exact units/coordinates, quantization, limits, profiles and caller-owned sequences |
| ESS wire protocol | Checked FC03/FC06/FC10 builders and parsers; raw exceptions and explicit pair order |
| Operations | Typed reads/settings, position, velocity, actions/stop, selected homing, commissioning and discovery |
| Standalone firmware | One shared console/application on Arduino and native ESP-IDF ESP32-S3 |
| Packages | CMake source/install consumers and PlatformIO; C++11, plus C++17 with RTTI disabled |

Typed native coverage is partial. The [API/CLI inventory](docs/ess_api_cli_coverage.md)
lists nine named gaps and 18 guarded paired setters. Current settings do not
provide torque/current motion; stored segments require external triggers.
Other manufacturers and CANopen are not implemented.

The tested motor is an ESS23-RS20, raw model `0x4EEA`, firmware word `0x0029`,
at address 1 / 115200 8N1 with a secured free shaft. These raw words are not a
resolved universal model/version mapping. S3 has recorded hardware evidence;
S2 has core/portable compile checks only, with no supported S2 UART adapter.

## Build and use the core

Requires CMake 3.16+ and a C++11 compiler. No Python, firmware SDK or vendor
download is needed to build the exported core package.

```sh
cmake -S . -B core-build -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/absolute/core-install
cmake --build core-build
cmake --install core-build
```

Consumer `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MotorConsumer LANGUAGES CXX)
find_package(MotorControlRS 1.0.0 EXACT CONFIG REQUIRED)
add_executable(motor_consumer main.cpp)
target_link_libraries(motor_consumer PRIVATE MotorControlRS::MotorControlRS)
set_target_properties(motor_consumer PROPERTIES
    CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
```

Consumer `main.cpp`:

```cpp
#include <MotorControlRS/profiles/ess_rs/Codec.h>

int main() {
    uint8_t request[8] = {};
    const std::size_t length = MotorControlRS::ESS_RS::buildProbe(
        1, request, sizeof(request));
    return length == 8 ? 0 : 1;
}
```

This builds a model-read frame; it sends nothing. Configure the consumer with
`-DCMAKE_PREFIX_PATH=/absolute/core-install`, then build and run it.
Source consumers can use `add_subdirectory` and the same target.

The core ZIP contains `include/`, `src/`, CMake, metadata, license, README and
changelog. Documentation links, examples, tests and references require the full
checkout. Public headers carry API buffer, lifetime, unit and error contracts.

## Operation model

1. Supply the target, actual configuration and required reference evidence.
2. Prepare the typed operation. Invalid or unsupported requests fail before TX.
3. Execute yielded requests through one application-owned transport.
4. Validate replies and return events/time to the sequence.
5. Retain the terminal result and its evidence.

A write acknowledgement is not motion completion. Local cancellation is not a
motor stop. A transmitted write with no confirmed reply may have executed;
never replay it automatically. Raw codecs validate wire access and framing,
not readiness or motion limits.

See [position](docs/ess_position.md), [actions/stop](docs/ess_actions.md),
[observations](docs/ess_reads.md), [units/coordinates](docs/axis_preparation.md)
and [ownership](docs/architecture.md).

## Position defaults and limits

Typed finite positioning defaults to all of:

- At most **2,000 rpm**.
- At most **200,000 command increments/s**.
- Native acceleration/deceleration time words **100..2,000 ms**.

The speed ceiling is `min(2000, floor(12000000 / subdivision))` rpm.
At subdivision 51,200 it is 234 rpm. `settings` displays the current ceiling.
Preparations require the active subdivision and reject excess values without
silent clamping. Explicit caller policies remain possible under the caller's
qualification responsibility.

These defaults come from one tested motor/firmware and are not universal drive
ratings. High-speed reset-like events and persistent RUNNING reports remain
unresolved. [Four-hour testing](docs/reports/2026-10-07_rate_boundary.md) and
[408 boundary cases](docs/reports/2026-10-07_position_limits.md) support the
documented envelope. Actual shaft speed, calibrated travel and electrical
timing were not independently measured.

Observation age expiry defaults to off (`maxAgeUs`/`maximumAgeUs=0`). Missing
evidence, generation changes, transaction deadlines and new-event completion
checks still apply. Applications needing time-based expiry must configure it.
No communication-loss stop or industrial certification is claimed.

## Firmware and verification

- [Arduino build/flash](docs/esp32_probe.md)
- [Native ESP-IDF build/flash](docs/esp_idf_probe.md)
- [Named finite test scenarios](docs/bench_scenarios.md)
- [Quick/full verifier and CI](docs/verification.md)

```sh
python -m pip install -r scripts/requirements-verification.txt
python scripts/verify.py --mode quick
python scripts/verify.py --mode full
```

Full mode requires the pinned PlatformIO and ESP-IDF toolchains described in
the verification guide. Hosted CI builds/tests software; it does not run COM13.

## License and repository

Library source is MIT licensed. Original vendor and Modbus reference files
retain their own licensing and hashes; they are excluded from core packages.
`library.json` controls the version; `Version.h` is generated from it.

Package name: `MotorControl-RS`. Namespace/include/CMake identity:
`MotorControlRS`. The active repository is `janhavelka/ESS23-RS`; canonical
metadata is prepared for `janhavelka/MotorControl-RS` under the
[rename guide](docs/repository_rename.md).
The [FieldCore handoff](docs/fieldcore_handoff.md) describes future integration;
FieldCore is not a library dependency.
