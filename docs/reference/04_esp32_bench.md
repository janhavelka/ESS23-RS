# ESP32-S3 standalone bench configuration

Updated 2026-10-03. This file describes the available motor-test hardware,
not a required board or a FieldCore product. The library core and portable
RS485 runner do not depend on this MCU, SDK, USB console or PSRAM.

## Selected electrical configuration

| Item | Bench choice |
| --- | --- |
| MCU / memory recipe | ESP32-S3, 16 MB flash, 8 MB OPI PSRAM |
| TX / RX | GPIO47 / GPIO48 |
| Combined DE/RE | GPIO21, high transmits, low receives |
| UART | UART2, exclusively owned by the standalone adapter |
| Motor link currently exercised | 115200 baud, 8N1, address 1 |
| USB console | COM13; 115200 console setting is separate from motor baud |

TX47/RX48/DE21 were confirmed by the user. The historical board label was
"E2 revision 2.0.0"; it does not name a motor protocol or a required bus
service. Keep that label only as bench provenance. GPIO selection is supplied
by the application through [BoardPins.h](../../examples/common/BoardPins.h).
[Esp32S3Uart](../../examples/common/Esp32S3Uart.h) takes explicit pins and
polarity, rejects invalid wiring before setup and has no board-header import.
The current adapter supports UART2 and 115200 8N1 only. Other controllers or
serial formats need their own verified adapter changes; configurable pins do
not qualify every ESP32-S3 board's electrical wiring.

The [probe guide](../esp32_probe.md) describes commands and timing restrictions.
The [hardware record](../hardware_bench.md) preserves the original firmware
backup, authorization, current image and measured motor responses. Physical
TX/RX/DE timing remains unqualified; historical pin matches do not prove it.

## Standalone build

[platformio.ini](../../platformio.ini) and the local
[bench board manifest](../../boards/bench_s3_n16r8.json) select:

- pioarduino 55.03.311, Arduino 3.3.11 and its ESP-IDF 5.5.5 libraries;
- 240 MHz CPU, 80 MHz flash, QIO flash and OPI PSRAM recipe;
- USB CDC console and an explicitly chosen upload port;
- `bench_s3_units`, `bench_s3_probe`, `bench_s3_load_poll` and
  `bench_s3_load_timer` example environments.

The [16-MiB partition layout](../../examples/common/partitions_16mb.csv)
retains the already used bench offsets and upload ceiling. The standalone
application implements no OTA or product storage service. Changing partition
layout is unnecessary for decoupling and would complicate the preserved
firmware restoration procedure. ISR/capture state and stacks remain internal;
large example buffers use PSRAM. This is an example allocation policy only.

Native ESP-IDF can consume the core through CMake. A standalone native-IDF
application remains planned; Arduino USB macros do not configure it.
Another platform can use the same core and [portable runner](../runner.md)
with its own callbacks and caller-owned storage.

## Source provenance and future integration

The initial pin and N16R8 build cross-check used FieldCore commit
`e097860b1f992ab8d3822f39724e36a849dde390`, specifically
`include/TunnelMonitor/board/tunnelmonitor/TunnelMonitorS3Hw200Board.h`,
`boards/esp32-s3-wroom-n16r8.json` and the RS485 backend. These were read-only
references. No FieldCore product source, services or generated build profile
is needed here. The older CO2Control firmware is relevant only to the original
bench firmware backup; its product features and other board pinouts are not
motor-library requirements.

Future FieldCore integration is for a motor device/product that does not yet
exist. The useful reference is the RS485 workflow: one bus owner, bounded
admission/results, physical TX completion, per-request validation and explicit
motion uncertainty. Its task keeps other existing device protocols when later
extended. See the [RS485 integration review](10_runner_platform_review.md).
No E2 sensor-bus implementation or CO2control integration belongs in this repo.
