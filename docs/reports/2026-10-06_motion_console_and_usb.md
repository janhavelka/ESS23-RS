# Motion console fixes and USB investigation, 6 October 2026

## Disposition

Motion workflow corrections are implemented and bench-tested. **The overnight
USB/controller nonresponse is still unresolved.** The user reset the controller
before this investigation; that recovered communication but removed the live
fault state. No motor alarm or failed motor-bus transaction was recorded in the
original overnight run: 339 completed moves and 38 stop cases had successful
individual cleanup before the unanswered local `stats` query. Its aggregate
FAIL and unknown final cleanup remain unchanged in the
[overnight report](2026-10-05_overnight_hil.md).

## Confirmed defects and corrections

- The simple wrapper rejected every subsequent command after any failed
  preparation, including an unsent conversion rejection. Delivered read-only
  preparation failures now reclaim automatically on the next simple command.
  Admitted uncertain/failed motor writes still require inspection; no replay or
  transport-recovery bypass was added.
- The simple console had no boot origin and erased an established fixed origin
  even after successful finite motion. It now captures the first checked
  stationary position before simple movement as RAM-only session zero. The
  standalone feedback/command-coordinate relation is explicitly ASSUMED, limited
  to nonnegative signed-32-bit feedback. Native codecs/core defaults are unchanged.
  No counter clear, startup movement or ESP NVS position storage occurs. Both
  `moveto ... steps` and angular simple targets use that host zero; the advanced
  explicit native-coordinate route remains available. Release, unknown/external
  movement and relevant configuration changes invalidate confidence without
  silently choosing another zero.
- The first additional repeat-target test found one-increment feedback error
  after successful arrival. Reissuing the same absolute endpoint caused a tiny
  move with no observed RUNNING transition and an uncertain result. The actual
  stop and profile restoration succeeded. The final simple workflow recognizes
  its retained successfully completed endpoint plus new stationary/arrived
  evidence and does not retrigger that same target. Any intervening trigger,
  invalidation or changed generation prevents reuse. This reports a satisfied
  command, not exact shaft alignment or a new movement completion.
- `speed 600 rpm`, `speed 600rpm`, `accel 200 ms` and `decel 50ms` now share
  strict existing range/number validation with bare values. Host replies state
  explicitly that no motor setting has changed yet: the next actual move sends
  parameters before its trigger. `settings` remains actual readback plus desired
  values; it never applies those desired values.

## USB diagnosis and retained evidence

The actual pinned Arduino 3.3.11
[HWCDC source](https://github.com/espressif/arduino-esp32/blob/3.3.11/cores/esp32/HWCDC.cpp)
already includes interrupt rearming and bounded writer progress. These are
upstream changes, not an untracked local patch. The adapter uses 64-byte writes,
zero TX timeout and bounded queues. `stats` also snapshots capture/heap/stack,
so its unanswered command alone cannot identify which layer stopped.

Espressif's open [connection-state issue](https://github.com/espressif/arduino-esp32/issues/12782)
describes a related USB symptom, including reconnect failures, but is **not proof
of this run's cause**. Windows currently has USB selective suspend enabled;
that setting is not evidence that a suspend occurred at the failure. No power
policy or SDK version was changed speculatively.

After the user's reset, 10,000 local queries passed in 111.172 seconds. A second
attempt completed 24,986 queries and stopped because its 100,000-record evidence
capacity was exhausted, not because of a board fault; it remains a failed test
attempt. A larger bounded reproduction uses 500,000 records/256 MiB for 100,000
queries. Its final disposition will be recorded separately.

The application now records 28 bytes of diagnostic progress in RTC RAM and
exposes the previous boot's record through `drv.previous_runtime`. Native tests
and an actual subsequent controller reset verify retention. It records last
service stage, uptime, loop/input counts and output pressure, never motor state
or pending requests. It neither resets a stalled controller nor retries work.
Power loss can erase it; a stage is not a stack trace or definitive root cause.
See the [stage map](../console.md#controller-reset-diagnostics).

## Verification and physical evidence

The full verifier passed all 20 groups, including 77 registered native/Python
tests, generated/reference checks, isolated C++11/C++17 source/install consumers,
four Arduino builds and four IDF application/core/portability builds:

```powershell
$env:IDF_TOOLS_PATH=(Resolve-Path build/p25/idf-tools).Path
$env:IDF_PYTHON_ENV_PATH=(Resolve-Path build/p25/idf-python).Path
$env:Path='C:/pio/packages/toolchain-xtensa-esp-elf/bin;C:/pio/packages/toolchain-riscv32-esp/bin;'+$env:Path
$env:PLATFORMIO_BUILD_DIR='build/usb_motion_fix/pio'
python scripts/verify.py --mode full --build-dir build/usb_motion_fix/full-final --idf-path C:/pio/packages/framework-espidf --idf-python build/p25/idf-python/Scripts/python.exe
```

An earlier full invocation failed because the IDF Python environment variables
were not exported; the corrected invocation above ran every group. An added
diagnostic clock read also exposed a fake-test timing assumption; the witness
now reuses the owner's existing sampled time, with no extra clock call.

The final Arduino GPTimer image was flashed and hash-verified on COM13:
621488 bytes, SHA-256
`ac1b023869f20ef99075ed96c8519dd2f14ab49203a288ded09272c90d3e7287`.
Exact five-move test input: four `moveby 90 deg`, then `moveto 0 deg`, at 60 rpm,
100 ms acceleration/deceleration; followed by `moveto 0 steps` with no new motion.
A preceding fractional-step rejection did not block those moves. Explicit unit
syntax was tested with host values 600 rpm/200 ms/50 ms, returned to 60/100/100
before any movement. **No 600-rpm test was performed.**

Final corrected run (`motion3`) PASS, 7.594 seconds. Raw stationary positions:
`50888, 51138, 51389, 51638, 51889, 50887`; return command target was exactly
50888. The test's declared readback comparison allowance is one command
increment; this is uncalibrated drive feedback, not independent shaft proof.
Final stop observed, exact original profile `[30,100,100,60,0,250]` restored;
alarm/speed zero, nonrunning/enabled, DE released, no queued/retained requests,
no recovery, load/monitor/debug disabled. Free internal RAM 336600 bytes, PSRAM
8177196, owner stack watermark 2180 bytes at that snapshot.

Retained attempts: `motion1` exposes the repeat-target defect and successful
cleanup; `motion2` passes the motor sequence but has a host test argument error
afterward, also with successful cleanup; `motion3` is the corrected aggregate.
Two initial read-only inspections had harness result-argument/correlation
errors; their raw records remain under `build/usb_motion_fix`. None is silently
reclassified as PASS or used as justification to replay an uncertain write.

[Motion/raw evidence archive](2026-10-06_motion_console_fix_evidence.zip), SHA-256
`fc689dc2ee1bbb62164e6305d3119517b971de220a538f9afc96eaf482434014`, contains
exact script, failed/passing raw attempts, upload logs, image and full-verifier
manifest. Complete local USB logs remain under `build/usb_motion_fix`.

Current FieldCore owner types were inspected read-only; its eight-byte TX
capacity and suffix/RTU modes remain unchanged. These are standalone application
policy/diagnostic fixes, with no framework types added to the core.
