# Capture, load and implementation audit — 2026-10-03

MotorControl-RS 0.6.0 now receives replies and releases DE while the application
owner sleeps. The work stays in the example layer; the reusable motor core has
no new task, UART, clock or allocation dependency. FieldCore was not edited.

The implementation and load comparison are complete for the measured bench
cases below. Independent electrical timing and cache-off qualification remain
open. This is a working reference, not an industrial certification or a claim
that every supported input setting meets a deadline.

## What changed and why

- **Independent native wire events.** The shared SDK fake schedules start/end
  of RX characters and physical TX completion separately from capture and
  owner polls. Delaying a task cannot move the wire events along with it.
- **Bounded background capture.** Optional 20-us GPTimer sampling reuses the
  same E2 capture logic and a 64-entry internal ring. FIFO batches, overflow,
  missing timing or a full ring fail explicitly. The owner still supplies a
  fresh admission sample before its runner poll.
- **Actual DE release evidence.** Background RX alone would miss the response
  if a sleeping owner left DE asserted. Capture now releases DE after observed
  TX idle and hold, and returns one atomic TX/end/release observation. The
  runner advances through drain/hold before processing buffered replies.
- **Load and diagnostics.** One small competing task, controlled owner delay
  and serialized console traffic exercise the same probe path. Python records
  failures, latency and resource data and stops without replay or recovery.

See [E2Uart](../../examples/common/Esp32S3Uart.h),
[RtuRunner](../../examples/common/RtuRunner.h),
[E2Load](../../examples/probe_cli/Esp32Load.h), the
[wire fixture](../../test/capture_service_test.cpp), and the
[probe/load guide](../esp32_probe.md) for code and commands.

## Audit findings and fixes

| Finding | Applied fix and check |
| --- | --- |
| Separate TX-end and uncertainty callbacks could observe different updates | One `TxObservation`; bounds are checked together. Native async completion and invalid-bound tests. |
| Delayed polling could mistake a task wake-up for the physical DE release time | Retain actual release bounds; judge the TX deadline from physical evidence. Timer-mode observations remain pending until release evidence is available at the supplied time. |
| A new DE assertion could retain the previous transaction's TX uncertainty | Reset all per-TX evidence together. Native cancellation/failure-before-enqueue regression. |
| Timer startup/shutdown failures could lose handle ownership or leave callbacks targeting expired state | Track running/enabled cleanup stages, retain handles for explicit cleanup, latch failures. Native failure injection at each SDK stage. Destruction fails explicitly if cleanup cannot finish. |
| ISR and task access could race over capture state | One critical-section guard and atomic statistics snapshots. Reject future byte/end bounds until observable. |
| Console output could interleave or inherit a partial diagnostic line | Serialize whole output operations and begin each on a new line. Short/disconnected output still fails host framing; no command replay. |
| The largest requested diagnostic did not fit the default USB TX ring | Allocate a 1024-byte ring before USB startup. Repeated hardware runs now show accepted diagnostic lines at the 256-byte payload setting. |
| Reset mixed old workload time with new capture counters | Reset one fixture measurement window while preserving settings, result and fault state. Checked on COM13. |
| Successful probes could hide a fixture that generated no requested console load | Python now rejects a load campaign with no competing iterations or no complete console lines when requested. Native regression and a new hardware quick test. |

The code continues to use the original checked ESS parser after the runner
collects a frame. Neither a frame nor a write acknowledgement establishes
motion completion. No new motor writes were introduced.

## Software verification

- **13/13 CTest suites passed**, Release build with warnings treated as errors.
  The Python suite contains **44 cases**.
- New wire tests cover delayed owner service, delayed capture, FIFO batches,
  FIFO overflow, capture-ring overflow, interrupted/ambiguous timing, stale
  watermarks, late and foreign replies, masked interrupts and no replay.
- Runner tests cover delayed physical-release delivery, echo, cancellation,
  malformed/future/overlapping bounds, hold and deadline uncertainty, and
  stuck-TX cleanup. A failed operation retains its failure through later input.
- All four PlatformIO environments build: units, basic probe, polling load
  and timer load. The core remains independently consumable through CMake.

The FreeRTOS load task/USB scheduling is exercised on hardware. Native tests
cover the actual adapter, runner, console and basic application loop; they do
not pretend to reproduce the full ESP32 scheduler or USB electrical behavior.

## Bench comparison

COM13 still identifies USB `303A:1001`, serial `3C:0F:02:CD:6B:98`, on E2
revision 2.0.0. UART2 uses TX47/RX48/DE21. Tests read only ESS model register
`0x0000` at address 1, 115200 8N1. Checked model result remains `0x4EEA`;
the exact model/firmware mapping is unresolved. The 304-us first-reply override
and other host timing policies remain as documented in the probe guide.

| Campaign | Work per 10 ms / owner delay / console payload | Checked probes | Result |
| --- | --- | ---: | --- |
| Final polling baseline | 0 / 0 / 0 | 20/20 | Pass; mean 5.368 ms |
| Final polling with delayed service | 2000 us / 5000 us / 128 bytes | 0/1; stopped | `TX_TIMEOUT`, retained diagnostics, no replay |
| Timer development comparison | 2000 us / 5000 us / 128 bytes | 100/100 | Pass; mean 15.619 ms; 747 complete diagnostic writes |
| Timer after USB capacity fix | 5000 us / 5000 us / 256 bytes | 200/200 | Pass; 1690 complete diagnostic writes, 19 dropped |
| Final timer artifact, heavy load | 5000 us / 5000 us / 256 bytes | 100/100 | Pass; mean 22.033 ms; 852 complete diagnostic writes, 6 dropped |
| Final timer artifact, unloaded smoke | 0 / 0 / 0 | 10/10 | Pass; mean 5.412 ms |
| Final Python workload-evidence check | 5000 us / 5000 us / 256 bytes | 10/10 | Pass after adding the no-load detection regression |

An earlier heavy campaign passed probes but emitted **zero** diagnostic lines:
the 256-byte payload plus framing exceeded the default USB ring. It remains
in the evidence as the finding that led to the capacity fix. It is not counted
as successful full console-load qualification.

The final heavy run measured:

| Measurement | Observed value |
| --- | ---: |
| Maximum active-owner service gap | 10023 us |
| Maximum capture gap | 53 us |
| Probe duration range | 18.998–22.969 ms |
| Capture samples | 432158 over 8.586 seconds |
| Capture section time / elapsed window | About 20.0% of one core |
| Scheduler non-idle estimates | Core 0: 0%; core 1: 60% |
| Free / minimum internal heap | 336864 / 331704 bytes |
| Largest internal heap block | 278516 bytes |
| Free / minimum PSRAM | 8380332 / 8380332 bytes |
| Largest PSRAM block | 8257524 bytes |
| Owner / competing-task stack headroom | 5728 / 2952 bytes |

The capture section measured about 19–21% across the later runs. This is a
material cost, not negligible overhead. Idle-based CPU estimates can charge
interrupt time to idle or another interrupted task; the two measurements are
not additive. Interrupt dispatch/return is outside `capture_us`. Memory and
stack figures describe these short campaigns, not worst-case lifetime use.

The adapter object is 1680 bytes on ESP32-S3, including a 1536-byte capture
ring. The load task has a fixed 4096-byte internal stack. ISR/driver storage
stays internal; application frame buffers, trace and console remain in PSRAM.

Explicit host checks also passed: resetting the measurement window preserved
load settings; a probe to nonresponding node 247 timed out; the next probe was
rejected until explicit recovery; reset did not clear that fault; recovery
followed by the address-1 probe succeeded. These were deliberate audit actions,
not recovery hidden inside a campaign.

## Capture choice and limits

GPTimer is the smallest extension that reuses the reviewed capture logic and
separates wire observation from the owner schedule. The linked SDK is ESP-IDF
**5.5.5**. The timer handler is in IRAM, but
`CONFIG_GPTIMER_ISR_CACHE_SAFE` is disabled. The selected code must therefore
not claim continuous capture through flash/cache-disabled intervals.
[Espressif GPTimer documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/peripherals/gptimer.html)
describes these cache and interrupt constraints.

UART driver events do not supply the individual start/end timing required by
this runner, and ESP32-S3 RS485 direction switching is software-controlled.
[Espressif UART documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/peripherals/uart.html)
explains the driver mechanisms. This example keeps its exclusive LL/FIFO owner;
it does not install a second UART driver beside it.

The 20-us timer is selected as a measured reference. Before adopting it in a
larger firmware, decide whether its CPU cost is acceptable or whether a more
efficient UART interrupt capture path merits implementation and qualification.
Do not reduce sampling frequency just to improve a CPU number without testing
the resulting timing bounds and failure behavior.

Still unqualified: independent TX/RX/DE waveforms, RX publication and idle
assumptions, physical GPIO delay, cache-off/flash activity, arbitrary competing
interrupt load, longer frames on real hardware, extended soak/disconnect/power
cycles, and all motion/stop operations. `timing_qualified` remains false.

## Retained evidence and next work

The [machine-readable evidence summary](2026-10-03_capture_load.json) retains
campaign counters, memory/timing data and SHA-256 hashes of local JSONL logs.
Final polling evidence uses `load_poll_final_baseline` and
`load_poll_final_delay`; final timer evidence uses `load_timer_release_*`.
Earlier development logs are identified separately and are not asserted to
use the final binary hash.

- Final polling firmware: 330512 bytes, SHA-256
  `f815fb71dc1414bfc89fb00f8c47b0dc4a73cc8ad07fbf4f6e8a31b69de444a1`.
- Final timer firmware: 333936 bytes, SHA-256
  `2a0cdd29ce0b4a746c3488f6dcc4f74425d8559966363d6960789c36e4129ef1`.
- Original 16-MiB CO2control backup was rechecked unchanged:
  `85c089bc3956b22037af7e6f0d7945f6ed778bb1a0526fb5b2ba3e8fea02d0c1`.

COM13 is left on the timer load/probe firmware, with `load 0 0 0` explicitly
applied and three final checked probes passed. There is no automatic motor
traffic after the host stops issuing probes. Original-firmware restore remains
untested as recorded in the earlier bench report.

Next implement the small bounded bus owner, then typed ESS identity/state
reads. Keep electrical qualification and CPU-budget work visible in parallel.
The [roadmap](../roadmap.md) defines the remaining release gates.
