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
attempt. The larger bounded reproduction passed all 100,000 queries (`stats`,
`version`, `host`, `config` repeated) on the corrected image, with 400,094
evidence records / 150,017,487 bytes and no evidence exhaustion. Maximum logged
command duration was 63 ms. The last reply arrived at 09:12:46 UTC; the complete
session, including port closure, finished at 09:13:16 UTC (1131.844 seconds).
The approximately 30-second closing delay is investigated separately below;
it was not a missing firmware reply. This shorter test does not replace the
failed six-hour overnight run or prove that its failure is corrected.

Windows USB/Kernel-PnP/System logs around 04:15–04:35 CEST contain no recorded
USB-device failure/removal explaining the overnight timeout. The actual compiled
HWCDC source matches pinned upstream 3.3.11 byte-for-byte (SHA-256
`c5ed5fdd05aa0df9b74d390812643599223f96b459256718d0ad328aeaba6a8a`).
Neither an unlogged host USB failure nor a controller/service stall is excluded.

An explicit reopen after 195 seconds with the port closed passed without a
controller reset (uptime continued to 1413013 ms). Instrumented Windows
`SetCommTimeouts`, `GetOverlappedResult` and `CloseHandle` calls then returned
immediately. The preceding close delay did not reproduce and its exact call
site was not captured. It is not established as the cause of the overnight
missing reply, which happened while the port was open. No automatic reconnect,
recovery, SDK patch or speculative Windows power-setting change was introduced.

The stress run's final counters show zero failed transactions, timeouts, capture
faults, RX errors, console drops or output blocking. Free internal RAM remained
336600 bytes, PSRAM 8177196 bytes and stack watermark 2180 bytes. These finite
checks do **not** identify a definitive USB culprit. The next occurrence needs
its live fault state or retained diagnostic stage before further diagnosis;
no USB fix is claimed.

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

Implementation commit `bba676f739d4ffa5cb793a5922546845f82078c9` is synchronized;
all four [exact-commit CI jobs](https://github.com/janhavelka/ESS23-RS/actions/runs/37439647130)
passed (GCC, Clang, Arduino, native IDF).

## Final bench state and follow-up artifacts

The same hash-verified image was uploaded/reset after the USB tests. Fresh
configuration and stationary feedback established RAM-only zero at native
50887; `moveto 0 deg` returned already-at-target with no movement write. The
result was released, final speed/alarm were zero, DE released, pending/reserved/
retained/output queues empty, recovery false, load/monitor/debug off. COM13 was
closed and released. Host defaults are 60 rpm and 100 ms ramps; no ESP NVS
position exists. Fixed origin remains known for the user's subsequent commands.

`final_zero` is a retained failed **harness** attempt: it compared against an
undecoded pre-configuration pair (`pair_known=false`), although firmware
correctly established zero and sent no motion. The corrected `final_zero2`
loads configuration first and asserts the pair is known; after another explicit
controller reset it passed, with 20 checked read frames and zero errors.

[USB/final-state evidence](2026-10-06_usb_followup_evidence.zip), SHA-256
`41527710662f0b52f34268cd9137589846e4d6f261766af598e178ea0b040211`,
contains exact scripts, summaries, bounded stress tail, SDK/event-log inspection,
both zero-check attempts and upload logs. Full stress raw log is retained locally
under `build/usb_motion_fix/after_stress.jsonl`, SHA-256
`b7e470b2ca008b146ea30955c727347810bd7fee058c5d615c971b0ce14bb5af`.
