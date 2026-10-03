# RS485Motion

A framework-independent serial motion library, starting with STEPPERONLINE
ESS23-RS10/RS20. The repository directory remains `ESS23-RS`; the common C++
namespace and package name are `RS485Motion`.

The implementation supplies **configurable units, the ESS register catalogue
and checked ESS Modbus RTU codecs**. Transport, motion commands, discovery
orchestration and the full standalone CLI are the next stages. No motor
communication or motion has been tested. Other reviewed drives, including
Leadshine iEM-RS, are design contrasts rather than implemented profiles.

The core has no Arduino, ESP-IDF, FieldCore, UART, GPIO, clock, heap, retry or
health-service dependency. Applications own those responsibilities. Common
motion vocabulary and complete native profile access remain the accepted
[architecture](docs/architecture.md).

CANopen is planned as a separate future library, initially for the Lichuan
CL86-C's verified capabilities. Both libraries will follow the same documented
motion contract; shared units/types will be extracted only when the second
implementation needs them. This repository continues with ESS and selected
serial drive profiles. `RS485Motion` is the working name pending a replacement.

## Units API

Position, velocity and acceleration preferences are independent. Supported
spatial units are command steps, motor full steps, identified encoder counts,
turns, degrees, radians and configured millimetres. Time denominators can be
seconds, minutes or milliseconds, including rpm/s.

```cpp
#include <RS485Motion/RS485Motion.h>
#include <RS485Motion/profiles/ess_rs/Defaults.h>

using namespace RS485Motion;
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
floating-point conversion. Origins, wrapped-angle paths, target quantization
and device-specific ramp encoding follow later.

`makeBenchUnitConfig()` assumes 1000 command steps per motor turn, 4000 decoded
encoder counts per motor turn and direct 1:1 coupling. Both model datasheets
specify 1.8° full steps, giving 200 full steps per turn. Linear travel has no
default lead. These settings perform no device writes and are not readback.
See [encoder evidence and examples](docs/reference/06_encoder_units.md) for
provenance and the distinction between encoder counts and ESS position feedback.

## ESS module

[Registers.h](include/RS485Motion/profiles/ess_rs/Registers.h) and
[Types.h](include/RS485Motion/profiles/ess_rs/Types.h) expose register addresses,
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

[Codec.h](include/RS485Motion/profiles/ess_rs/Codec.h) builds FC03/FC06/FC10
requests into caller-owned byte buffers and checks complete replies against
the expected slave, function, length, count, CRC and write echo. Errors leave
payload outputs unchanged; the parsed word count resets to zero. `Status`
preserves raw device exceptions separately from malformed frames and CRC errors.

```cpp
#include <RS485Motion/profiles/ess_rs/Codec.h>

uint8_t request[8];
const std::size_t length = RS485Motion::ESS_RS::buildProbe(1, request, sizeof(request));
// length == 8; request is 01 03 00 00 00 01 84 0A. Nothing is transmitted.

// A synthetic complete reply for an offline parsing example:
const uint8_t reply[] = {1, 3, 2, 3, 5, 0x78, 0xB7};
uint16_t model = 0;
RS485Motion::Status result = RS485Motion::ESS_RS::parseProbe(reply, sizeof(reply), 1, model);
// Success publishes raw model 0x0305; it does not confirm a connected motor's identity.
```

FC03 accepts up to 16 documented readable words, excluding gaps/reserved/unknown
access. FC06 accepts documented writable single words; split writes to paired
fields are rejected. FC10 initially accepts only the manual's `0x0024`, two-word
window. This is an implementation restriction, not a discovered device maximum.
Builders validate raw access/framing, not register-value meaning, motion limits,
readiness or persistence; typed command helpers remain future work.

The probe reads the read-only model word at `0x0000`; no consuming side effect is
documented. Its successful reply is seven bytes. Pure 32-bit helpers require an
explicit word order; signed helpers specify two's complement without asserting
that an unresolved ESS field uses it. See [codec evidence and limits](docs/reference/01_implementation_reference.md#implemented-codec-scope).

Small private RTU helpers handle byte packing and CRC. Profile policy stays in
ESS, informed by [five contrasting manufacturers](docs/reference/08_serial_protocol_review.md).
Applications still own transport and response correlation: local FC06 echo and
a drive acknowledgement have identical bytes, and write acknowledgement does
not establish motion completion. No codec retries, clocks or I/O are hidden.

## Build and preview

With CMake and a C++11 compiler, build and run the native tests and unit preview:

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

Use another available CMake generator if Ninja is absent. An application can
use `add_subdirectory` and link `RS485Motion::RS485Motion`, or install the CMake
package and use `find_package(RS485Motion CONFIG REQUIRED)`. The root CMake file
also supports ESP-IDF `EXTRA_COMPONENT_DIRS`; native ESP-IDF firmware validation
is still pending. Public headers require no framework headers.

The same [unit preview](examples/units_preview/main.cpp) builds for the E2
ESP32-S3 bench using the FieldCore N16R8 memory/platform baseline:

```powershell
.\scripts\pio.cmd run -e e2_s3_units
```

This preview prints conversions to USB; it does not initialize the motor UART
or send commands. Board pins are explicitly TX47, RX48, DE21 in
[BoardPins.h](examples/common/BoardPins.h). See the
[board audit](docs/reference/04_co2control_platform.md) for the physical-board
versus current FieldCore product-profile distinction. Builds do not upload to
COM13. Preserve the existing firmware before a later bench upload.

Generated files are checked with:

```sh
python scripts/generate_version.py check
python scripts/generate_ess_registers.py --check
```

## Repository guide

| Path | Responsibility |
| --- | --- |
| `include/RS485Motion/`, `src/` | Public common API and implementation |
| `include/RS485Motion/profiles/ess_rs/`, `src/profiles/ess_rs/` | ESS catalogue, raw codecs, probe and word conversion |
| `src/rtu/` | Small private byte/CRC/frame helpers, without device policy |
| `examples/common/` | Board/build settings and later platform transport |
| `examples/units_preview/` | Desktop/Arduino consumer of the current units API |
| `test/` | Native units, catalogue and independent protocol verification |
| `scripts/` | Reference preparation and deterministic generators |
| `docs/reference/` | Register inventory, source evidence and implementation questions |
| `docs/vendor/`, `docs/standards/`, `docs/pdf-extracted-md/` | Preserved original references and searchable extracts |

Start with [documentation](docs/README.md), [remaining work](docs/backlog.md),
[bench notes](docs/hardware_bench.md) and [engineering guidance](AGENTS.md).
The [axis](docs/axis_contract.md), [profiles](docs/profile_contract.md),
[discovery](docs/discovery_contract.md) and [CLI](docs/cli_contract.md) contracts
describe the intended complete library beyond this first implementation.

Code is MIT licensed. Vendor PDFs, CAD, software and standards retain their
owners' rights and are excluded from the distributed source package.
