# One-command subdivision and interactive result ownership

The user's log exposed successful human reads filling the eight result slots,
and a saved position-profile snapshot blocking the next move after subdivision
changed. These were application workflow failures, not evidence of motor failure.

## Changes

- `subdivision N` now uses the existing bounded simple application session:
  configuration read, driver baseline read, stationary/input checks, the same
  typed subdivision setter, checked readback, refreshed configuration/state and
  host coordinate establishment. The caller needs only this one command.
  Range validation happens before reads. An unchanged value performs no setting
  write, preserving a matching host scale/origin or establishing missing host
  context from fresh stationary evidence. `subdivision` reads without setting changes.
- A definite changed setting supplies the standalone's explicitly assumed
  command scale and a new RAM-only zero from fresh stationary feedback. Old
  prepared motion and the old scale's restoration snapshot are discarded.
  No implicit motion, save, retry or counter clear occurs. The completion text
  reports the new zero. The core and detailed driver API keep explicit caller
  prerequisites and invalidation; the convenience policy belongs to the app.
- On a new ordinary human bus command, successful human results are recyclable
  only after their terminal output has left both bounded queues. Pending,
  failed/uncertain, programmatic and explicit `@ID`/JSON results are retained.
  Inspection alone does not recycle anything. The simple session owns and
  releases its internal reads; failed setting results survive the next session.
- Fixed the inverse stale-cache comparison: a current configuration read must
  not be invalidated repeatedly by an older invalidated driver snapshot.
  Historical baselines remain available when no current configuration exists.
- Fixed feedback-origin invalidation. The standalone's assumed RAM zero is a
  fixed counter offset; enabled feedback variation does not prove the counter
  reset. It no longer erases that offset or stales a freshly read setting context.
  Owned move settling remains attributable until a new stationary state baseline.
  Release, unexpected running, counter clear, changed interpretation and unknown
  encoding still invalidate through their existing paths. Homed/API references
  retain their stricter policy. No arbitrary position-error tolerance was added.
- Ordinary `read state` terminal output summarizes flags, alarms and raw
  position/speed. Detailed `result`/JSON retains the complete original evidence.
  Successful human replies no longer instruct the user to release every result.

These changes reuse the shared Arduino/IDF console, simple session, typed reads,
driver sequence and owner. No new writer, transport, conversion engine or core
API was added. Current FieldCore `src/rs485/Rs485Task.cpp` was inspected read-only;
its request ownership remains separate and was not modified.

## Verification

Native application regression runs twelve alternating subdivision changes and
then an absolute move, with no manual preparatory command or result release.
It covers same-value no-write, post-write configuration disagreement, checked
write rejection, cancellation during the driver read, child lifetime protection,
new reads after failure, twenty successive interactive probes, failed-result
retention, output backpressure, passive inspection and unchanged full-store
behavior for explicit JSON ownership. The shared cases compile/run for both
Arduino and native-IDF fake applications. Console tests cover strict values,
range edges, help and the new session routing.

The final quick verifier command is
`python scripts/verify.py --mode quick --build-dir build/simple_subdivision/checked-final`.
It covers all 77 registered suites and 15 stages, including generated/reference
checks and clean source/install consumers. Exact pushed-commit CI is required
for GCC, Clang, Arduino and native IDF; CI links are available from commit checks.

## Hardware failures, correction and rerun

The [evidence archive](2026-10-06_simple_subdivision_evidence.zip) preserves the
initial inspections, failed campaigns, explicit restoration and final three
campaigns. `preflash` hit the older simple-result inspection format in the Python
harness (a correlation rejection, not a USB hang); `preflash-checked` expected a
result the user had already released. Neither sent motor writes. The corrected
`preflash-ready` collected fresh state/configuration, recording subdivision 1600.

`hil1` restored subdivision only after an admission rejection; `hil2` lost the
origin before the return move. Both retained failures and confirmed cleanup stop.
Native fault reproduction showed two concrete application errors:

1. After arrival, residual speed/position could continue changing. A second
   preparatory state read treated this as external movement after the original
   operation had terminated. The new stationary-baseline flag keeps that movement
   associated with its original command; it does not declare standstill early.
2. Even enabled feedback variation at reported zero speed invalidated the fixed
   host offset. In `hil3`, the return succeeded but this invalidation changed the
   configuration generation between the driver baseline and its setter. The new
   exact rejection diagnostic identified `previous driver-settings context does
   not match`. A native case changing feedback 100 to 101 reproduced the rejection
   between baseline and setter. The offset correction fixes that root cause.

`hil4` passed the original sequence after both corrections. Final firmware then
passed `final-hil1`, `final-hil2`, `final-hil3` consecutively, without resetting
between campaigns: 36 ordinary human state reads, subdivision 1600/51200/1600,
small requested 1-degree absolute moves and returns at both scales, profile
read/restore and checked normal stop. Every requested workload ran. Commands use
bounded nearest-step conversion (4 and 142 increments); independent shaft angle
and physical scale accuracy remain unmeasured. No manual result release was
needed for ordinary human commands; explicit diagnostic JSON owns/releases its
own results. Failed attempts remain failures and are not folded into the pass.

Final image SHA256:
`30a9bc5125620c27b54e9a47abc5644ec77ba86b3241d785649d9a4ef2ac7033` (631264 bytes).
Build/flash: `scripts/pio.cmd run -e bench_s3_load_timer -t upload --upload-port COM13`,
`PLATFORMIO_BUILD_DIR=build/ownership_fix/pio`; Arduino 3.3.11 / IDF 5.5.5,
115200 8N1 (actual adapter baud 115211), timer capture.

Final three campaigns: 580/580 checked frames, zero failures, timeouts, RX errors,
capture faults or input drops. Owner/capture maximum gaps 375/57 us; internal
free/minimum 335776/330616 bytes; PSRAM free/minimum 8177196 bytes; owner stack
headroom 2612 bytes. CPU1 samples 24–30%, CPU0 0%; injected load disabled. Bounded
trace overwrite counts describe diagnostic history, not protocol loss.

Final state: subdivision 1600 restored, RAM host scale 1600 and origin raw62346;
speed/alarm zero, enabled/nonrunning, DE low, no pending owner/output work,
monitor/load/debug off. `final-state` confirms the origin remains known. Staged
profile is `[30,100,100,60,0,62451]`; the target remains the retained baseline
from the diagnosed interrupted test, not a request to execute it. Speed/ramps
are unchanged from the first fresh profile read; subdivision is restored to the
user's 1600. No save, counter clear or autonomous motion is armed. COM13 closed.
No persistence, endurance, electrical or independent shaft qualification is claimed.
