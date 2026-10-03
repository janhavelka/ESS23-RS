# Platform boundary, prompt and optional I/O audit

Reviewed 2026-10-03 from repository baseline `fdb9926`. The scope is a general
RS485 motor library with manufacturer profiles, starting with ESS. FieldCore
is a future consumer, not the product specification for this repository.

## Findings and fixes

| Finding | Change | Evidence |
| --- | --- | --- |
| No E2 sensor-bus implementation or FieldCore dependency exists in `include/` or `src/`. Example names and product-specific prose nevertheless suggested one. | Renamed adapter, fixture, board/build environments and prompt 03; removed unrelated product/bus requirements from live prompts. | Actual source/include/build audit and all 30 prompt blocks reviewed. |
| The UART adapter read `BoardPins.h` internally and assumed active-high DE. | `Esp32S3Uart::begin(Pins, baud)` receives checked TX/RX/DE and polarity. Only the application selects the bench preset. | Alternate GPIO4/5/6 and active-low tests, invalid/colliding pin rejection, setup-failure/retry tests and physical regression on the existing pins. |
| Board configuration included an unused UART-index setting although the adapter supports UART2 only. | Removed the misleading setting; documented the current UART2/115200-8N1 adapter limits. | Call-site search, native tests and four firmware builds. |
| Broad FieldCore/product guidance blurred the reference boundary. | Prompts describe a portable request/wait/result workflow and a later RS485-task handoff for a motor device that does not yet exist. Bench pins and memory settings are example-only. | Current FieldCore RS485 source inspected read-only; no changes there. |
| Unconnected versus disabled drive I/O was not explicit. | Axis/profile/CLI contracts and dependent prompts now allow unused I/O, document native no-function assignment and require signals only for methods using them. | Original ESS PDF tables inspected visually; existing register ledger/enums checked. |
| Long board provenance text mixed bench facts with unrelated product composition. | Replaced it with [standalone bench configuration](../reference/04_esp32_bench.md). Preserved original vendor downloads and dated bench evidence. | Current paths/links checked; historical report links updated without changing recorded measurements. |

The reusable core remains three C++ translation units with no board, framework,
clock, GPIO, allocation or FieldCore dependency. The portable runner uses
caller callbacks and storage. MCU/SDK capture and USB/load tasks remain in the
standalone example. Other RS485 manufacturers belong in profiles when actual
hardware and need justify implementation; this audit adds no new profile.

FieldCore source was inspected at `960dfdba2a4785f99ef4e67dcd47006a6379616d`:
`Rs485OwnerTransaction.h`, `Rs485Backend.h`, `Rs485DeviceBinding.h`,
`Rs485Task.cpp` and `ArduinoRs485Backend.cpp`. Useful similarities are bounded
admission/results, one bus owner, per-request validation and explicit transport
evidence. Its eight-byte TX limit, echo stripping and measurement-only results
still need later motor integration work. Unrelated local FieldCore edits were
left untouched. This repository does not implement its sensor buses or product
composition.

## Optional drive I/O

ESS function-manual physical PDF pages 27-28 and 73-74 define function `0` as
no terminal function. Existing generated `InputFunction::UNDEFINED` and
`OutputFunction::UNDEFINED` already encode it. X0-X3 function registers are
`0x0041..0x0044`; Y0/Y1 function registers are `0x004C..0x004D`.

The future typed function setter/CLI `none` choice must use this same reviewed
encoding. No new register, generic GPIO API or placeholder setter was added.
Typed I/O operations remain prompt 15 work. Unconnected wiring alone does not
disable an assignment; polarity inversion is not disable; disabling an output
assignment does not establish its electrical level. Serial-versus-release-input
precedence remains unresolved for ESS. Readiness checks must account for actual
configured inputs without requiring optional switches for every serial move.
Switch-based homing and external segment triggers still require their signals.

## Verification

- All 13 native CTest suites passed after the refactor, including the three
  suites compiling the real ESP32 adapter against the SDK fake.
- Separate installed-library consumers included every public header and ran
  a probe builder under strict C++11 and C++17, warnings as errors, RTTI off,
  without example, SDK or FieldCore include paths. Both passed.
- All four firmware environments built: `bench_s3_units`, `bench_s3_probe`,
  `bench_s3_load_poll`, `bench_s3_load_timer`.
- Generator/version/Python checks passed through CTest; five preserved contrast
  references passed offline hash verification.
- Markdown relative links, the 30-prompt index and whitespace checks passed.
  Local VS Code metadata was regenerated for the renamed default environment.
- An independent reviewer checked adapter polarity, GPIO bounds, timer release,
  failure cleanup and caller/build changes. A separate source/manual review
  checked the I/O contracts and prompt prerequisites.

The new active-low test first exposed an old fake assumption that TX requires
GPIO high. The fake now models transceiver active level independently; both
polarities pass. The adapter still requires exactly one application owner and
instance for UART2; SDK-driver detection cannot detect another raw adapter.
The final ESP32-S3 ELF reports a 1,696-byte adapter object, including its
1,536-byte capture ring; explicit pin configuration adds 16 bytes to the prior
object. No additional allocation or task was introduced.

The first firmware build used an inherited `PLATFORMIO_CORE_DIR=C:\\pio`
whose compiler path was incomplete. The selected compiler existed and ran in
the established user `.platformio` installation. Re-running with that core
directory set for the verification command passed; no SDK/source workaround
or repository-wide machine path was added.

## Physical regression

COM13 identity matched the recorded ESP32-S3 bench. The original 16-MiB
firmware backup hash was verified unchanged before uploading. All motor traffic
was the checked model-register read at address 1, 115200 8N1, TX47/RX48/DE21.
No motor setting, I/O assignment or movement was changed.

| Campaign | Checked probes | Result |
| --- | --- | --- |
| Previous-image baseline | 10 | PASS |
| Refactored timer image, no injected work | 20 | PASS |
| Refactored image: 2000 us work / 10 ms, 5000 us owner delay, 128 console bytes | 30 | PASS |
| Explicit workload disable and final regression | 3 | PASS |
| Final retained rebuild: ordinary / loaded / workload-off checks | 10 / 10 / 3 | PASS |

Loaded probe latency was 14,756-17,014 us; maximum observed owner gap 8,086 us
and capture gap 52 us. The load fixture produced 223 complete diagnostic lines
and dropped 10 under its bounded backpressure policy. All correlated command
responses passed. Reported internal free memory was 336,848 bytes, PSRAM free
8,380,332 bytes, owner stack headroom 5,728 bytes and worker stack 2,952 bytes.
The scheduler estimated 31% busy on the owner's core; capture section time
is a separate measurement, not an additive CPU percentage.

Exact campaign summaries, local raw-log paths/hashes and the retained image
are in [the evidence manifest](2026-10-03_platform_scope_audit.json). Raw logs
and firmware remain under ignored `build/bench/`. COM13 is left on the refactored
timer image with injected workload disabled. No automatic probes run afterward.

These short read-only runs do not qualify alternate physical wiring/polarity,
electrical TX/RX/DE timing, cache-off operation, motion or I/O disable effects.
Those remain distinct open checks. The next implementation block is still
prompt 01, the small standalone RS485 bus-owner reference.

## Separate bootloader readback issue

IDE metadata regeneration cleared generated firmware build directories after
the first 53 refactored-image probes. Attempting to retain that image by reading
the application region through esptool exposed a separate host/bootloader issue.
The first attempt failed on Windows output encoding. UTF-8 fixed that reporting
error, but three subsequent flash-read attempts stopped after 188,416 complete
bytes, including a traced run and a direct-to-file run without PowerShell output
handling. The trace receives 4096 bytes of the next SLIP packet and then waits
three seconds before `Packet content transfer stopped`. The motor firmware is
not executing while the bootloader stub handles this transfer.

The exact cause is unresolved. Retained trace and logs distinguish the failure
from an RS485 transaction or a proven application defect. A targeted stub/ROM
read comparison and USB capture at the failing packet are still needed; a longer
timeout or successful motor probe would not establish a fix. No library timing
guard or retry policy was changed to conceal it.

The final source was rebuilt, its binary copied to ignored `build/bench/` before
upload, and flashed with successful upload hash verification. That exact retained
image passed the additional 23 probes above, including load and workload-off
checks. It is the image left on COM13. The original firmware backup is unchanged.
The manifest distinguishes the first refactored image's logs from final-image
evidence rather than assigning the rebuilt binary hash to earlier tests.
