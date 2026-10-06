# Firmware stall and crash-path investigation, 6 October 2026

The overnight failure is still unresolved. This investigation found and fixed
three concrete firmware gaps, including a physically reproduced secondary
crash during watchdog dump generation. These are not evidence that USB alone
caused the original failure. The original failed aggregate remains FAIL.

## Evidence and fixes

The original run completed 2,262 cycles, including 377 motion cases with
successful individual cleanup, before local console nonresponse. Its retained
motor-bus errors and drive alarms were zero; RAM and stack measurements were
stable. This gives no evidence of gradual exhaustion, but does not exclude
corruption, deadlock, interrupt starvation or a blocked owner after the last
received record. The last unanswered local command could not itself identify
which subsystem stopped making progress.

The flash dump retrieved before modifying firmware is **not from that run**.
Its CRC is valid and embedded ELF SHA-256 is
`b05b481d0a5e749c82a76bf55f00ad7b86f1f57ed9a33af8865aebca54e3b9cf`.
It matches FieldCore's deliberately induced September 15 interrupt-watchdog
fixture (the archived image manifest), rather than the overnight motor image.
Raw dump SHA-256 is
`fae32aee1b33d3599017d38acff6f4351a5f587104f1c04615a0eb98fbfdbe7f`.
Raw memory dumps remain private under `build/firmware_health`; they are not
shipped as public evidence.

1. **Owner supervision:** Arduino's pinned SDK watched CPU0 idle, while the
   application ran on CPU1 without task-watchdog subscription. A blocked owner
   could leave idle tasks healthy. The shared application now subscribes its
   owner with a configured five-second panic timeout, preserving configured
   idle checks. It feeds only after a completed application turn, at most once
   per 100 ms, not from another task or timer. Setup/feed failures are visible
   and block new work. This is not a motor communication-loss stop, automatic
   recovery, or permission to replay an uncertain request.
2. **Crash-dump capture interrupt:** a temporary no-motor-work fixture stopped
   servicing the owner after three seconds while yielding to idle tasks. The
   watchdog detected `loopTask`, but flash coredump writing then caused
   `Cache disabled but cached memory region accessed` and `Re-entered core dump!`.
   Exact baseline ELF decoding places the fault at `Esp32S3Uart::captureSample`
   (`0x42012b3b`), interrupted during `esp_flash_erase_region` and coredump
   preparation. The pinned GPTimer ISR is in IRAM even with callback cache-safe
   mode disabled; our callback was entering flash. A small IRAM callback now
   checks cache availability, touches only a verified internal-RAM flag while
   cache is disabled, and returns. Resuming capture latches a transport fault
   until explicit recovery. Capture storage outside internal RAM is rejected.
   This does **not** qualify capture across cache-disabled intervals.
3. **Native-IDF USB startup:** inherited HWCDC interrupt enables are masked
   before the IDF USB driver installs its RX/TX handler. Pending status and
   FIFO contents are preserved. FieldCore documented a retained BUS_RESET
   enable causing an interrupt storm after CPU reset; its current adapter
   already implements this fix. This closes the same missing initialization
   step here, but the overnight image used Arduino HWCDC, not this IDF path.

FieldCore-node was inspected read-only at
`d19758865852f4e215ea05c6572d060edc4f0c66`: `WatchdogRuntime.cpp`,
`SystemRuntime.cpp`, `SerialCli.cpp` and its September 15 USB reset report.
No FieldCore file or public core API was changed. The standalone application
has one owner, so it watches that task directly rather than importing
FieldCore's multi-module quorum.

Light heap poisoning, stack canary/watchpoint, interrupt watchdog and flash
ELF coredump support were already enabled in Arduino's SDK. Native-IDF now
explicitly selects those diagnostics. One heap-integrity check runs at startup
before capture starts; a failure prevents bus initialization. No periodic heap
walk, extra control-loop allocation or unbounded diagnostic queue was added.
Inspection of capture/worker locking did not establish a deadlock cycle.

## Reproduction and verification

The deliberate stalled-owner baseline and fixed runs are retained separately:
`stall-console.log` versus `stall-console2.log`. The baseline ELF SHA-256 is
`1a41f72c2e525b13babbdb33f267d37881a4d54a90e42346401c85ca0cce4a6a`.
The fixed run produced an ELF-matched, CRC-valid dump identifying missing
`loopTask (CPU1)`, with no secondary cache panic. The restarted application
reported reset reason 6, the previous RTC progress stage, a subscribed healthy
watchdog, and zero motor requests. No movement was replayed. The configured
timeout is five seconds; this experiment does not claim exact reset latency.
The temporary stall fixture is preserved only as an evidence script and is
absent from shipped firmware/commands. Dump decoding confirmed panic identity;
not every task backtrace was available from SDK debug information.

An initial host check falsely recognized JSON `owner_watchdog.timeout_ms` as
a panic. The panic detector now requires a whole-word `watchdog`; regression
tests retain real panic/reset detection and accept the new diagnostic record.
The failed host attempt remains in evidence.

The final full verifier passed all 20 groups and all 77 registered tests,
including native/Python tests, generated/reference checks, isolated source and
installed consumers, four Arduino builds, native-IDF application, S3/S2 core
consumers and portable S2 application pieces. Python console tests include
240 cases. Native tests cover watchdog setup/feed failures and blocked output,
heap failure before capture, inherited USB masks without FIFO/status loss,
internal capture storage, brief cache interruption, retained fault through
statistics reset and explicit recovery. A first IDF build exposed the missing
`spi_flash` component dependency; it was corrected, not skipped. Inspection of
the generated SDK configuration also caught silently ignored coredump defaults
in the minimal IDF component set. The application now explicitly depends on
`espcoredump` and checks the actual flash/ELF/CRC configuration at compile time.

Commands and final manifests are under `build/firmware_health/final2`. Build with:

```text
python scripts/verify.py --mode full --build-dir build/firmware_health/final2 --idf-path C:/pio/packages/framework-espidf --idf-python build/p25/idf-python/Scripts/python.exe
```

The environment uses `IDF_TOOLS_PATH=build/p25/idf-tools`,
`IDF_PYTHON_ENV_PATH=build/p25/idf-python`, both installed Xtensa/RISC-V
toolchain bin directories on PATH, and
`PLATFORMIO_BUILD_DIR=build/firmware_health/pio`.

## Motor and loaded-console regression

COM13, secured free shaft, address 1, 115200 8N1 (reported actual baud 115211),
ordinary GPTimer firmware; no external timing or shaft measurements claimed.
The final Arduino image SHA-256 is
`9d53797f5f1d48e439f806b51e5191901819b87467455773d573edb2274ca93f`.
The ordinary human commands exercised explicit unit suffixes, a rejected
fractional native step without ownership lockout, four `moveby 90 deg` requests
at 60 rpm/100 ms ramps, `moveto 0 deg`, a repeated zero no-op, stop, fresh
standstill and exact restoration. Readback positions were
`61430, 61680, 61931, 62180, 62431, 61430`; original profile
`[30,100,100,600,0,50887]` was restored exactly. This recorded pre-test profile
is not changed into an assumed default. All 80 checked frames passed with
zero transport failures, timeouts, capture faults or RX errors.

The final Arduino loaded run passed 100 probes with 2,000 us worker load,
5,000 us owner delay and 128-byte diagnostic lines. Its 49.804-second load
window had owner/capture maximum gaps 8,034/56 us, 4,621 diagnostic lines
accepted and 359 dropped, CPU0/CPU1 busy 0/32%, worker stack watermark 3,268
bytes. Diagnostic drops did not lose correlated protocol results. Final free/
minimum internal RAM was 336,208/331,048 bytes, PSRAM 8,177,196/8,177,196 bytes,
owner stack watermark 2,052 bytes. Transport counters remained zero for
failures/timeouts/capture faults/RX errors; the watchdog remained healthy.
Injected load was explicitly disabled after the scenario.

Final native-IDF 5.5.5 image SHA-256:
`4ab2f1bcca541ca5888a2865e3dc1094172e03b58389102bf92f7f8743bdf38b`.
The generated SDK configuration explicitly confirms flash ELF/CRC coredumps,
task watchdog panic, light heap poisoning and stack canary. Ten quick probes
and the same 100-probe loaded scenario passed. The 49.352-second load window
had owner/capture maximum gaps 8,032/54 us, 4,527 diagnostic lines accepted,
407 dropped, CPU0/CPU1 busy 0/32%, worker stack watermark 3,272 bytes. Final
internal free/minimum was 361,987/325,368 bytes, PSRAM
8,177,396/8,177,396 bytes, owner stack watermark 2,596 bytes. All 147 accumulated
checked frames passed, no timeout/capture/RX errors, cache interruption or
watchdog feed error. Its earlier read/load checks without the coredump component
remain separately labeled `idf-quick`/`idf-loaded`; final checks are
`idf-final-quick`/`idf-final-loaded`. No IDF motion was needed for this diagnostic
change, and no IDF physical watchdog injection is claimed.

After restoring and resetting the final ordinary Arduino image, the read-only
zero check passed: 20 checked frames, zero errors, stationary raw position
61430 established as RAM-only zero. `moveto 0 deg` was an already-at-target
no-op, its result was released, speed/alarm were zero, drive enabled and
nonrunning, DE released, owner pending/reserved/retained/output queues empty.
Load/monitor/debug are off and COM13 is closed. No automatic motion is queued;
the watchdog fixture is not installed. Host move defaults remain 60 rpm/100 ms.

[Evidence archive](2026-10-06_firmware_health_evidence.zip), SHA-256
`d5b49edd3c68aa7a8f02e0973a02a5bb6ac7b19694dac67b0f57618f7ea16231`,
contains exact scripts/commands, baseline and corrected panic logs, image/dump
hashes, bounded raw console records, failed attempts and final verifier logs.
Images, matching ELFs and private memory dumps are retained locally under
`build/firmware_health`; the archive deliberately excludes raw memory dumps.

## Remaining diagnosis

A repeat of the failed overnight duration remains open. Another stall needs
its matching image, reset reason, RTC progress marker, fresh dump (if any),
and host USB state to distinguish blocked owner, interrupt starvation and USB
link failure. Stable heap readings or a later passing short test alone cannot
close it. Watchdog supervision makes an owner stall detectable; it does not
establish the original trigger or guarantee physical stopping after a reset.
