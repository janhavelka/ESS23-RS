# Native ESP-IDF standalone probe

[`examples/probe_idf`](../examples/probe_idf/) is an ESP-IDF 5.5.5 consumer
of the same MotorControl-RS core, runner, bus owner and CLI application used by
the Arduino example. The native application has its own USB transport, task
startup and SDK configuration. Its CMake build consumes the actual core
component under the name `MotorControlRS`; the checkout directory name is not
an API or component requirement.

The native CMake/Ninja firmware build passed on 2026-10-05 with ESP-IDF 5.5.5.
Source sharing establishes the command/operation path; it does not establish
hardware frame, timing or motion parity. Native-board read-only checks pass;
the [prompt25 report](reports/ess_release_25_2026-10-05.md) records the final image,
SDK configuration, resource measurements and corrected startup framing. Broader native
motion and load qualification belongs to prompt26. See the
[current Arduino guide](esp32_probe.md) for retained historical evidence and the
[CLI coverage inventory](ess_api_cli_coverage.md) for supported operations.

## Install and build

Install the exact ESP-IDF 5.5.5 SDK and its ESP32-S3 tools using Espressif's
Windows ESP-IDF installation, or a separate SDK checkout. The standard
PowerShell checkout workflow is:

```powershell
git clone --branch v5.5.5 --recursive https://github.com/espressif/esp-idf.git C:/esp/esp-idf-v5.5.5
Set-Location C:/esp/esp-idf-v5.5.5
.\install.ps1 esp32s3
. .\export.ps1
idf.py --version
```

Run `export.ps1` in each new shell before invoking `idf.py`. Keep its SDK tools
and constrained Python environment together; an arbitrary system Python or an
Arduino build macro does not select a compatible native SDK. The reviewed local
build used the SDK at `C:/pio/packages/framework-espidf` and an isolated,
SDK-constrained Python environment under `build/p25/idf-python`.

From the repository root, use a separate absolute build directory:

```powershell
$probeRepository = (Get-Location).Path
$probeApplication = Join-Path $probeRepository examples/probe_idf
$probeBuild = Join-Path $probeRepository build/idf-native
idf.py -C $probeApplication -B $probeBuild set-target esp32s3
idf.py -C $probeApplication -B $probeBuild menuconfig
idf.py -C $probeApplication -B $probeBuild build
```

`sdkconfig.defaults` seeds a new configuration; subsequent builds use the
generated `examples/probe_idf/sdkconfig`. Inspect that actual configuration
after changing defaults or reusing a build directory. `set-target` resets SDK
configuration, so preserve deliberate local board settings before using it
again. Firmware, raw bench records and backups under `build/bench` must be kept
when cleaning build caches. No native PlatformIO launcher is delivered.

In `menuconfig`, select **MotorControl-RS standalone board** and supply the
actual RS485 TX, RX and combined DE/RE pins, direction polarity and receiver
topology. Defaults describe the recorded bench: TX47, RX48, DE21, high selects
transmit, and the local receiver is disabled during transmit. UART2 is owned
exclusively by this application. Use distinct, usable board pins; a compile
guard reserves GPIO19/20 for the USB Serial/JTAG console, and flash/PSRAM pins must also remain
reserved. Declaring a topology does not measure it or establish motor settings.

The default board recipe is ESP32-S3, 16 MiB QIO flash at 80 MHz, 8 MiB OPI
PSRAM at 80 MHz and a 240 MHz CPU. It retains the existing
[16 MiB partition layout](../examples/common/partitions_16mb.csv), including
application offsets `0x10000` and `0x800000`. Adapt board memory settings only
to the actual hardware.

## Runtime and memory requirements

The native consumer checks the actual SDK version, S3 target and required
configuration at compile time:

| Requirement | Native configuration |
| --- | --- |
| SDK | Exactly ESP-IDF 5.5.5; the adapter's GPTimer cleanup follows its reviewed state transitions |
| Scheduler | Two cores, static task allocation, 1000 Hz tick |
| Load accounting | Runtime statistics from `esp_timer`, U32 counters |
| External storage | PSRAM enabled and initialized at boot |
| Capture operating envelope | Power management disabled; `GPTIMER_ISR_CACHE_SAFE` disabled |
| Driver storage | IRAM interrupt handler and `GPTIMER_OBJ_CACHE_SAFE` required |
| Startup failure output | Primary USB Serial/JTAG console enabled |
| Correlated output | Default SDK logging disabled; bootloader logs disabled in defaults |

Defaults also place the SDK GPTimer interrupt handler in IRAM, which selects
internal timer-object storage. The application's capture callback and helpers
execute from flash. Capture therefore does not support cache-disabled activity,
flash writes or sleep; placing the SDK wrapper in IRAM does not make the whole
callback cache safe. The adapter uses a 20 us GPTimer alarm, a 1 MHz timer
resolution and interrupt priority 2. It preserves physical TX completion,
DE-release and RX interval evidence through the shared runner. Software timing
observations leave `timing_qualified:false`; external electrical qualification
remains separate.

The final IDF image measures `sizeof(App)=206528`, UART/capture object 1720 and
load fixture 4816 bytes. Link-map placement confirms the UART, fixture, task
control block and owner stack are internal; App is explicitly allocated in PSRAM.
The read-only checks leave 2292 bytes of owner and 3272 bytes of worker stack
headroom, with 8177408 bytes PSRAM free. These are observations for that workload.

`app_main` starts one priority-1 owner on core 1 with an explicit 8192-byte
internal stack and static internal task control block. That task initializes
the shared application, then calls its cooperative service loop. The optional
fixture is enabled in this native example and creates the same priority-2
worker on core 1 with a 4096-byte internal stack and one bounded diagnostic
ingress slot. Its initial workload, injected delay and console output are zero.
It never performs motor I/O or writes USB. Runtime statistics are estimates;
U32 counter deltas are valid only within one counter revolution, approximately
71 minutes.

The UART/capture object and load fixture stay internal. The large `App`,
retained results, caches, traces and console queues use one explicit
`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT` allocation. There is no large internal
fallback. USB uses the native driver with a 1024-byte TX ring, 256-byte RX ring
and zero-tick reads/writes; normal output attempts at most 64 bytes per service
turn and retains unsent bytes. Startup failure emits one bounded boot record.
Bootloader and application default logging are both disabled. Driver startup
queues a checked newline before any JSON record so retained ROM/bootloader USB
bytes cannot concatenate with the first correlated reply. Failure to queue that
boundary prevents application startup; it does not extend a host deadline or
relax reply parsing.
Console/allocation failure prevents application startup; UART/capture/worker
failure leaves motor admission unavailable and cached diagnostics accessible
when application storage exists. Startup sends no motor request.

Use `memory`, `stats` and `load` to retain internal/PSRAM free, minimum and
largest-block figures, owner/worker stack headroom, capture errors and service
gaps. Native fake tests cover bounded USB pressure and initialization failures;
they do not measure hardware scheduler load, RAM placement or electrical timing.

## Flash, inspect and recover

Close other COM13 users and inspect the intended board before flashing. The
standing bench authorization and wiring are recorded in
[hardware bench notes](hardware_bench.md). From the exported SDK shell:

```powershell
idf.py -C $probeApplication -B $probeBuild -p COM13 flash
python scripts/bench_scenarios.py --port COM13 --out build/bench/idf_quick --scenario quick --firmware "$probeBuild/motorcontrol_probe.bin"
```

The finite `quick` scenario is read-only. `version`, `status`, `health` and
`memory` provide cached/local observations; explicit reads/probes generate
traffic through the same checked library/owner path. Keep raw TX/RX, parsed
results, actual serial settings, timing, errors and resource records. A frame
or acknowledgement does not prove motion completion, and uncertain motor
writes must never be replayed automatically. Use the ordinary
[finite scenario contract](bench_scenarios.md) for any later authorized motion
check rather than introducing native-only motor semantics.

To return to the ordinary Arduino timer firmware, rebuild and upload its
existing environment, then rerun the same read-only quick scenario:

```powershell
.\scripts\pio.cmd run -e bench_s3_load_timer -t upload --upload-port COM13
python scripts/bench_scenarios.py --port COM13 --out build/bench/arduino_restored --scenario quick
```

If USB application startup fails, use the board's ROM download mode through
its BOOT/RESET controls and the same flash command. Do not erase the entire
flash as a routine recovery step. Preserve the intended firmware/configuration
and verify the port after a reset before sending console traffic.

The original full-flash CO2control snapshot remains
`build/bench/co2control_com13_before.bin`, SHA-256
`85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.
It is a historical product image, distinct from restoring the ordinary Arduino
motor example. Its whole-flash restoration has not been tested. The
[original backup record](reports/2026-10-03_e2_probe.md) provides the recorded
esptool restoration command and partition verification limits; check the file
hash and intended board before using it. Device configuration in flash backups
stays outside Git.

Prompt25 also retains the complete immediately preceding Arduino motor image
in `build/p25/original-image/flash-16mb-v4.bin` (16 MiB), SHA-256
`09b0700e6b4f392e9bc3dbf4d935029cdacd0d2fe3d96d2be1613614680ea408`.
Its application at0x10000 matches the previously recorded 575424-byte image
`4ae880946615c4828992f864e983da36417123df39c15c82a045dd38f30487d3`.
Keep this backup when cleaning generated build directories. The report archive
preserves the previous application/bootloader/partition regions separately;
the whole-flash backup remains local. Whole-flash rollback was not performed.

## Shared application boundary

[`ProbeApp.cpp`](../examples/probe_cli/ProbeApp.cpp) contains the owner,
command callbacks, operations, caches, evidence, no-replay policy and bounded
service/output logic. Arduino and native IDF compile that same source with the
same console, adapter, runner and public codecs. `ArduinoPlatform.cpp` and
`IdfPlatform.cpp` supply only console I/O, idle scheduling and boot diagnostics;
their startup files supply pins/topology and the framework task lifecycle.

SDK time, PSRAM allocation and the ESP32 load fixture remain example concerns.
The reusable core has no Arduino, ESP-IDF, task, clock, GPIO, heap or FieldCore
dependency. Current FieldCore sources were inspected read-only for bounded USB
and single-owner conventions; no FieldCore product code or its sensor-protocol
behavior is imported. These remain independent consumers of the library.
