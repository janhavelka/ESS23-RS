# E2 HW2.0.0 motor bench and platform reference

Inspected on 2026-10-02. This records source evidence for the standalone
example, following the user's clarification that COM13 runs an **E2 board,
hardware revision 2.0.0**, with **DE GPIO21, TX GPIO47 and RX GPIO48**.
Those physical assignments are the example's selected bench configuration.
No serial port was opened, no firmware was flashed, and no RS485 transaction
or GPIO measurement was performed during this review.

## Physical board and firmware product are separate

The user-confirmed pins match FieldCore's retained
[`TunnelMonitorS3Hw200Board.h`, lines 16-39](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/include/TunnelMonitor/board/tunnelmonitor/TunnelMonitorS3Hw200Board.h#L16).
That header identifies ESP32-S3-N16R8, 16 MB flash, 8 MB OPI PSRAM and
active-high combined DE/RE. Its
[pin records, lines 227-232](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/include/TunnelMonitor/board/tunnelmonitor/TunnelMonitorS3Hw200Board.h#L227)
identify UART2. These are matching electrical/software-reference facts;
they do not identify the currently installed firmware from its product name.

| Example setting | Value | Evidence and qualification |
| --- | --- | --- |
| Named example board | `E2S3Hw200Board` | User-confirmed E2 HW2.0.0 bench |
| UART TX / RX | GPIO47 / GPIO48 | User confirmation and matching FieldCore HW200 source |
| DE/RE | GPIO21 | User confirmation and matching FieldCore HW200 source |
| Transmit / receive level | High / low | Matching FieldCore board default and backend; not measured on this bench |
| UART controller | 2 | Matching FieldCore pin records and `ArduinoRs485Backend.cpp:13` |
| Console rate | 115200 | Both inspected CO2Control build configurations; separate from motor baud |
| MCU/memory build baseline | ESP32-S3-N16R8, 16 MB flash, 8 MB OPI PSRAM | Matching FieldCore HW200 board and its PlatformIO board manifest; not queried from COM13 |

[`BoardPins.h`](../../examples/common/BoardPins.h) contains only this named
example selection, under `RS485MotionExample`. It performs no hardware
initialization and exposes no pins through the reusable library.
[`BuildConfig.h`](../../examples/common/BuildConfig.h) contains the console
rate. Motor address, actual serial framing, subdivision and word order remain
explicit commissioning inputs; no default address is inferred from the board.

The current FieldCore product selection is more restrictive than the
electrical board inventory. Its
[`config/build_profiles.json`, boards/profiles/environments](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/config/build_profiles.json#L319)
lists three board definitions but only two selected product profiles:
`tunnelmonitor_hw210` and `co2control_hw200`.
[`SelectedBuildProfile.h:3-20`](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/include/TunnelMonitor/product/SelectedBuildProfile.h#L3)
selects those same two. The current CO2Control profile includes the EE871 E2
component; its generated firmware build disables the RS485 owner. Its
[`Co2ControlS3Hw200Board.h:75-79`](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/include/TunnelMonitor/board/co2control/Co2ControlS3Hw200Board.h#L75)
classifies GPIO21/47/48 as inactive in that application. That omission does
not contradict the user's confirmation of the physical board connection.
There is no current selected FieldCore E2-plus-RS485 product environment or
runtime pin override in the inspected profile selection. Future integration
must add the appropriate ownership/configuration there; the standalone
example uses its own explicit board selection.

The separate older `Projects/CO2Control` repository targets a different
v1.10/N4R2 board. Its
[`AppConfig.cpp:8-26`](https://github.com/janhavelka/CO2Control/blob/ac1ed35f317e7716709f2a98b6f2897e36ab5489/src/config/AppConfig.cpp#L8)
maps TX16/RX18/DE17. Those values are **not used** for this E2 HW2.0.0 bench.
Its RS485 implementation is a stub
([`CO2Control.cpp:1932-1940`](https://github.com/janhavelka/CO2Control/blob/ac1ed35f317e7716709f2a98b6f2897e36ab5489/src/CO2Control.cpp#L1932)).

## Matching build baseline

Use the FieldCore N16R8 memory/platform baseline for the new standalone
example. Its current
[`co2control_wifi` environment, generated lines 794-807](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/platformio.profiles.generated.ini#L794)
and the TunnelMonitor environment use the same platform and memory recipe.
Reuse build facts rather than selecting either whole product application.

| Item | Exact inspected value |
| --- | --- |
| Platform | `https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip` |
| PlatformIO board | Local `esp32-s3-wroom-n16r8` board definition |
| Framework | `arduino` in both FieldCore firmware environments |
| CPU / flash clock | 240 MHz / 80 MHz in the board JSON |
| Flash sizes | `board_build.flash_size = 16MB`, `board_upload.flash_size = 16MB` |
| Flash / Arduino memory / PSRAM | `qio`, `qio_opi`, `opi` |
| USB console flags | `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1` |
| Other relevant flags | `BOARD_HAS_PSRAM`, C++17; `CORE_DEBUG_LEVEL=0` |
| Monitor | `monitor_speed = 115200`; choose port explicitly when live testing starts |
| Upload | Board JSON has `speed = 921600`, `require_upload_port = true` |

Exact memory, USB identity, clocks, upload and framework eligibility are in
[`boards/esp32-s3-wroom-n16r8.json:1-57`](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/boards/esp32-s3-wroom-n16r8.json#L1).
The board JSON is repository-local, not a claim that every installation has
that board ID. A standalone repository needs its own deliberately maintained
minimal board definition or equivalent explicit memory settings. It must not
depend on a sibling checkout to build.

The standalone repository now supplies
[`boards/e2_s3_n16r8.json`](../../boards/e2_s3_n16r8.json) and
[`platformio.ini`](../../platformio.ini). They select that memory/platform
baseline without importing a FieldCore product. The local
[`partitions_16mb.csv`](../../examples/common/partitions_16mb.csv) preserves the
same partition dimensions and upload ceiling; the preview implements no OTA
service. Neither the board manifest nor a successful build measures the memory
actually installed on COM13.

FieldCore uses its application-specific
`partitions/tunnelmonitor_s3_ota_16mb.csv`, two OTA slots, and an 8,323,072-byte
upload ceiling. A standalone example must choose and document its own
partition/build policy. Do not silently carry FieldCore product source
filters, OTA services, cloud dependencies, generated-profile scripts or
product macros into the motor library. The original v1.10 CO2Control's
4 MB `huge_app.csv`/`qio_qspi` recipe is not the N16R8 recipe.

The FieldCore
[build authority](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/docs/guidelines/board_build_ota.md#L38)
also records that resolved upload images use DIO with its QIO/OPI recipe.
When flashing is later requested, use the resolved build/upload recipe rather
than constructing manual flash arguments from a single configuration field.

Both example headers are ordinary C++ with no Arduino or ESP-IDF includes.
They can be consumed unchanged by a native ESP-IDF application. Native IDF
still needs its own target, memory, partition and USB-console configuration;
Arduino USB macros do not configure an IDF application. No native-IDF console
adapter or build qualification is claimed by these headers.

## Direction timing and reusable transport pieces

FieldCore's
[`ArduinoRs485Backend.cpp:13-57`](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/src/rs485/ArduinoRs485Backend.cpp#L13)
uses `Serial2`/UART2, initializes manual DE/RE to receive, and starts 8N1.
Its direction writer applies the configured polarity at lines 250-252.
An optional HIL-only branch uses hardware half duplex; this is not the normal
production branch. Its flush waits for UART TX completion with a timeout
before the owner releases direction.

The owner asserts DE, waits its pre-TX guard, writes, waits for TX drain,
then releases DE before waiting for a reply
([`Rs485Task.cpp:1050-1164`](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/src/rs485/Rs485Task.cpp#L1050)).
Current defaults are a 2 ms pre-TX guard and 20 ms inter-request gap
([`Rs485Devices.h:25-27`](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/include/TunnelMonitor/contracts/Rs485Devices.h#L25)).
These are FieldCore scheduling policies, not measured E2 transceiver enable
times or ESS timing requirements. The later example adapter must establish
its own bounded drain/turnaround and RTU silence behavior. The copied pin
facts alone do not qualify echo suppression or timing.

The smallest reusable peer boundary is `SimpleModbusPort.h`: a caller-owned
UART context plus bounded serial, GPIO and clock callbacks. The updated
[VibWire version](https://github.com/janhavelka/VibWire-108/blob/9365df6a51800755f417a1fa251088ee42be9dc0/examples/common/SimpleModbusPort.h#L17)
provides non-waiting TX-drain polling and GPIO failure reporting. Its full
`SimpleModbusRtu.h` is FC04-specific and is not an ESS transport unchanged.
The SHZK
[RTU helper](https://github.com/janhavelka/SHZK-PT/blob/361c47c0588fe967e9dc5507ce7a3d85a2f0eb6a/examples/common/SimpleModbusRtu.h#L63)
already handles FC03/06/10 reply shapes with bounded buffers, but its callback
flush can block and its helper defaults include retries. These behaviors
require explicit review before adapting it to motor commands. Reuse the
small callback/adapter boundary and relevant frame-state behavior; retain
zero automatic replay of uncertain motion writes.

FieldCore's whole `Rs485Task` brings its owner queues, measurement contracts,
runtime timing, health and result routing. It is not a small standalone
transport dependency. Its current eight-byte TX limit and request-echo
stripping also block general motor writes, as described in the
[ecosystem review](02_ecosystem_review.md). None of its service code is copied
by the example board headers.

## Provenance and current qualification

The references above describe local source at these commits. FieldCore had
unrelated WiFi/settings changes; the board, manifest and RS485 files inspected
here matched the cited revision. Sibling repositories were read only.

| Repository | Inspected commit | Reuse/licensing evidence |
| --- | --- | --- |
| FieldCore-node | `e097860b1f992ab8d3822f39724e36a849dde390` | `library.json:16` says `UNLICENSED`; used here as electrical/build evidence, no application source copied |
| CO2Control | `ac1ed35f317e7716709f2a98b6f2897e36ab5489` | MIT `LICENSE`; older board distinguished above |
| SHZK-PT | `361c47c0588fe967e9dc5507ce7a3d85a2f0eb6a` | MIT `LICENSE`; potential example-helper source |
| VTN4xx | `6cc143307c824928c33e646fa6fd515403b97f98` | MIT `LICENSE`; peer callback/RTU example, not board evidence for this bench |
| VibWire-108 | `9365df6a51800755f417a1fa251088ee42be9dc0` | MIT `LICENSE`; potential callback/drain interface source |

The MIT files identify Jan Havelka, Thymos Solution s.r.o., copyright 2026.
Any later copied or adapted substantial helper must retain its copyright and
permission notice and record the source revision and changes. This pass adds
fresh minimal example constants and documentation, without copying a full
transport, board service or application.

The historical FieldCore
[COM13 fixture report](https://github.com/janhavelka/FieldCore-node/blob/e097860b1f992ab8d3822f39724e36a849dde390/docs/reports/hil-testing/condensed/archive/2026-09-13_com13_empty_co2control_full_hil.md#L21)
records a CO2Control product image on 2026-09-13. It is historical evidence;
it cannot establish today's image or the available motor-control path.
The user's current hardware clarification governs the selected bench pins.
Treat COM13 as the board's firmware connection until inspected; no transparent
USB-to-RS485 bridge has been established.

The two example headers passed a combined host syntax compile with MinGW-w64
G++ 15.1.0, `-std=c++11 -Wall -Wextra -Werror -pedantic`, independently of
either embedded framework. Actual USB identity/image, memory detection, board-level
direction polarity/timing/echo and motor communication remain unmeasured in
this pass. Further observations belong in the
[hardware bench record](../hardware_bench.md).
