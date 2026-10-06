# Subdivision access and retained boot-session origin

## Findings and corrections

Before resetting or flashing COM13, inspection of the previous image found
`origin_native=62060` but `origin_known=false`. The motion-profile session
reported restored with no unsettled write. Restoring the speed/ramp/staged-target
window had incorrectly called full axis invalidation. That window does not
change the counter or its scale. Restoration now expires prepared work and
position observations while preserving the fixed host origin and limits.
An expired position sample likewise no longer erases the fixed origin; a new
move still needs fresh feedback. Release, external movement and actual scale
changes retain their separate invalidation rules. Boot zero remains RAM-only,
established from the first qualified stationary observation, with no NVS write.

Added `subdivision`, `subdivision N` and `subdivision options` to ordinary help,
dispatch and capabilities. The short forms expand to the existing checked
`driver read` / `driver set subdivision N` path. There is one writer and one
validation/retained-result path. Original function-manual physical page 69
documents register 0x0011, integer range 400..51200, default 1000, rather than a
discrete preset list. The helper labels its suggested values as examples.
The [original PDF](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf)
SHA256 is `0ca2d7f6b69404ead3ba9af982076c54f86eb16b1c24ba237f344aac079b81eb`.

Subdivision-only admission can now establish its prerequisites from fresh
configuration, stationary feedback and inactive input observations on the
declared unwired fixture, rather than requiring an inaccessible qualification
flag. Unknown wiring, active assigned inputs and moving feedback reject before
TX. Other setting families are not enabled by this change. Genuine subdivision
changes invalidate host scale/origin and the simple wrapper's old scale intent.
No implicit persistence write or motion is added.

Hardware exposed a second cache bug: after changing subdivision, an invalidated
historical driver snapshot could override a newer successful configuration
read and invalidate it again. Current configuration now takes precedence over
invalid driver history. Historical comparison remains available when both are
invalidated by a host tuple change. A native regression failed before this fix
and passes afterward; the existing host-tuple baseline regression also passes.

The current FieldCore `src/rs485/Rs485Task.cpp` was inspected read-only. These
changes remain in the shared standalone application; no framework types or
second owner were introduced. Independent console/source and origin reviewers
checked the implementation and native regressions.

## Use

`subdivision options` is local. To change the drive setting while stopped, run
each command and wait for its completion:

```text
read config
subdivision
read state
subdivision 1600
```

This changes the actual drive parameter. `stepsperturn` remains a separate host
conversion declaration. A real scale change requires re-establishing the host
scale/reference; ordinary profile restoration and idle time do not.

## Verification and retained failures

The [evidence archive](2026-10-06_subdivision_origin_evidence.zip) includes exact
scripts and incremental raw/structured records, including failed attempts:

Archive SHA256: `04d3fc4aa5c152b8f921fd9bd4fe33249833953d86cea96456d33842f6fcd359`.

- Initial inspection scripts had host argument errors; the third inspection
  captured the old origin state without resetting the controller.
- `hil1` passed the origin/motion regression and 1000-to-1600 setting update,
  then rejected restoration before TX because of the stale cache bug above.
  Its human-output helper timed out waiting for a completion record after that
  rejection and marked framing uncertain. This was not evidence of USB loss.
  The failed aggregate and unknown cleanup are retained, not relabeled PASS.
- `explicit-restore` used fresh checked evidence to restore 1000 and confirm
  stopped state before the corrected firmware was flashed. No uncertain write
  was replayed.
- `hil2` reran the same command order on the corrected image: boot-zero no-op,
  relative 90 degrees, profile restore, absolute 10/90/0 degrees, profile restore,
  subdivision 1000-to-1600-to-1000 with checked readback, normal stop and standstill.
  All passed: 100 frames, 100 successes, zero failures/timeouts/capture faults/RX
  errors. The 10-degree target rounded to raw 62088 with +0.222222 degrees error.
- After restoring the original subdivision and motion profile, a deliberate
  controller reset and `final-zero` read-only check established zero at raw
  62060. `moveto 0 deg` was a no-op with no new motion command. Final speed/alarm
  zero, enabled/nonrunning, DE released, no pending owner/output work,
  load/monitor/debug off. COM13 is closed.

The corrected normal Arduino timer image was built and flashed using
`scripts/pio.cmd run -e bench_s3_load_timer -t upload --upload-port COM13`, with
`PLATFORMIO_BUILD_DIR=build/ownership_fix/pio`. Image SHA256:
`2fcd0a55c02e099158a1df44bfc98ef5e6370de227736f54c853455c6e5f207d`.
HIL owner/capture gaps were 426/58 us; internal free/minimum 335776/330616 bytes,
PSRAM free 8177196 bytes, owner stack headroom 2292 bytes. Original profile
`[30,100,100,200,0,62060]` and subdivision 1000 were restored.

Native regressions cover idle expiry, restoration followed by absolute moves,
genuine invalidation, help/dispatch/range parity and the cache conflict. Run:
`python scripts/verify.py --mode quick --build-dir build/subdivision_origin/verified`.
The first final invocation found the not-yet-written report link; the report
is included in the passing rerun: all 77 registered suites and all 15 verifier
stages, including clean source/install consumers and codec link isolation.
Exact-commit hosted CI is checked after push,
including GCC, Clang, Arduino and native-IDF builds.

Drive feedback is the motion evidence. Independent shaft angles/electrical
timing and subdivision scale activation were not measured. No save or physical
restart-dependent persistence test was performed. The earlier overnight USB
failure is not resolved by these coordinate/cache corrections.
