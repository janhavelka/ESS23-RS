# ESP32-S2 portable application consumer

This native-IDF compile/link fixture consumes the staged installed core and the
actual platform-neutral `RtuRunner`, `RtuBusOwner`, `EssRtuValidator` and
`ProbeConsole` sources. It uses fixed caller-owned storage and an explicitly
missing port. There is no ESP32-S3 UART adapter, GPIO/pin selection, clock, USB,
PSRAM policy or motor callback. Unsupported console motor routes remain
unavailable; no synthetic wire event is treated as hardware evidence.

The installed ESP-IDF 5.5.5 SDK and Xtensa tools provide a maintained ESP32-S2
target. After exporting that SDK, stage the core as described by the
[core-only consumer](../idf_consumer/README.md), then build from the repository
root using absolute stage/build paths:

```powershell
$portableRepository = (Get-Location).Path
$portableCore = Join-Path $portableRepository build/core-package/MotorControlRS
$portableBuild = Join-Path $portableRepository build/idf-s2-portable
idf.py -C test/portability_consumer -B $portableBuild -D "MOTORCONTROLRS_COMPONENT_PATH=$portableCore" set-target esp32s2
idf.py -C test/portability_consumer -B $portableBuild -D "MOTORCONTROLRS_COMPONENT_PATH=$portableCore" build
```

Use a new stage/build directory for a clean check. The separate core-only
consumer should also be built with `set-target esp32s2`; this fixture deliberately
includes example sources and does not replace installed-public-header isolation.

The fixture links real probe codec/request validator calls, rejects a request
through the unavailable runner port, constructs the bounded owner and feeds
local/version/capability/unavailable-probe commands through the actual console.
Building does not execute `app_main` or prove those runtime assertions. Native
tests execute the corresponding behavior. No S2 firmware is flashed, no S2
transceiver or board wiring is claimed, and no S2 hardware qualification follows
from a successful compile. The ESP32-S3 application remains the physical bench
consumer with its own measured platform contract.
