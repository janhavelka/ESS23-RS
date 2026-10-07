# Getting started

MotorControl-RS 1.0.0 has a C++11 core and ESP32-S3 console examples.
Start with the [root README](../README.md) for CMake installation or the
[Arduino](esp32_probe.md) / [native ESP-IDF](esp_idf_probe.md) guide for firmware.
The core needs no Arduino, ESP-IDF, Python, vendor PDFs or FieldCore headers.

## First console session

Use the board's serial port. Only one terminal or Python process may own it.
Opening/resetting the controller can discard host snapshots while the drive
retains its own settings. Boot never starts a move or restores an old request.

```text
help
version
config
probe
settings
read state
```

`config` reports the host connection. `probe`, `settings` and `read state`
communicate with the selected drive. `status` and `health` inspect cached values
without sending requests. `useaddr 1` selects a local target; it does not write
the drive's address. `help COMMAND` shows exact syntax.

The bench example uses address 1 and 115200 8N1. Other boards need their actual
TX/RX/DE wiring and receiver topology. External terminals may be unused;
unconnected wiring and a disabled drive function are different facts.

## A small move

For the secured, unloaded, enabled bench motor, this sequence explicitly sets
subdivision, then moves away from and back to the session zero. Send each move
after the previous command completes.

```text
subdivision 1600
speed 60 rpm
accel 100 ms
decel 100 ms
moveby 90 deg
moveto 0 deg
```

`subdivision 1600` checks stopped state, writes only if needed, reads back the
setting and aligns the example's host angle scale. It establishes a RAM-only
zero from checked stationary feedback when required. It does not move, save to
nonvolatile memory or clear the drive counter. `subdivision options` lists the
supported integer range and useful values. `subdivision` alone reads settings.

`speed`, `accel` and `decel` change host intent. **The next move sends those
parameters before start.** They do not alter a move already running. `settings`
shows both actual drive parameters and next-move choices. A released drive
requires an explicit `enable`; a move never silently enables it or changes I/O.

Simple moves perform missing read-only preparation. They round to the nearest
command increment, with half-step ties to even, and report the effective target
when rounding changes it. `moveto 720 deg` means two turns from the origin, not
an orientation normalized to zero. Requested and effective limits are checked.

The boot convention starts from the first checked stationary position, not from
an ESP NVS coordinate. Before an explicit subdivision/scale choice, the example
uses an **assumed 1,000 increments/turn**. This is not calibration. Known loss of
position confidence, release, counter clear or interpretation changes can
invalidate the origin; the firmware does not silently choose a replacement.
See [origins and result handling](console.md).

## Position limits and stop

The default speed ceiling is `min(2000, floor(12000000 / subdivision))` rpm.
Both native ramp words must be 100..2,000 ms. At 51,200 subdivision the ceiling
is 234 rpm; at 1,600 it is 2,000 rpm. `settings` displays the current ceiling.
A larger stored preference causes a clear no-write rejection, not silent slowing.

These are [tested default limits](ess_position.md#default-position-operating-limits)
for the recorded free-shaft motor, not proof for every drive or load. Native ramp
time is not an acceleration unit. The default observation deadline is 30 seconds;
a timeout does not prove that the motor stopped.

- `stop normal` requests deceleration and checked stopped-state observation.
- `stop fast` requests the ESS stop without the deceleration ramp.
- `cancel ID` cancels local work; it is not a motor stop.
- `motor-release` releases winding drive; `release ID` only frees host result storage.

After an uncertain move, preserve its result and request an explicit stop.
A confirmed stop frees motion ownership; old evidence can remain retained.
Do not replay an uncertain movement. Serial stops require communication and are
not hardwired emergency-stop circuits.

## Results and common refusals

Successful ordinary human results are recycled after output delivery when a
later bus command needs storage. Routine successful use needs no `release`.
Failed/uncertain, explicit `@ID`, JSON and direct API results remain retained.
Inspect `result ID`, then `release ID` after review. Inspection is non-consuming.

| Message | Meaning / action |
| --- | --- |
| Operating limit or preparation rejected | No motion command was sent; correct the stated setting or missing evidence. |
| Motor has active or unresolved work | Wait for the active move, or request and confirm stop before another move. |
| `results_full` | Review retained terminal results, then release their host storage. |
| Recovery required | Inspect the fault; `recover` repairs host transport, cancels old queued work and never replays motor writes. |
| Unknown execution / completion | Keep evidence, explicitly stop/reconcile; acknowledgement alone is not completion. |
| Port access denied | Close the other terminal/process before using this port. |

Detailed diagnostics: `drv`, `stats`, `memory`, `result ID`, and optional
`debug raw` / `debug decoded`. `debug off` disables traffic display.

## Direct core API

This prepares the same minimal presence query used by `probe`, without I/O:

```cpp
#include <MotorControlRS/profiles/ess_rs/Discovery.h>
int main() {
    MotorControlRS::ReadTarget target;
    target.id = 1;
    target.address = 1;
    target.generation = 1;
    MotorControlRS::ESS_RS::PreparedProbe probe;
    const auto status = MotorControlRS::prepareProbe(
        probe, MotorControlRS::DriveProfile::ESS_RS, target, 1, 100, 100000);
    return status && probe.length == 8 ? 0 : 1;
}
```

The application retains target, tuple, ID, generation and deadline, sends the
prepared bytes, supplies real transport evidence and checks the reply. A valid
response with an unknown model remains an unidentified responder.

For motion, [PositionCommand](ess_position.md#native-commands-and-remembered-intent)
prepares native commands; its acknowledgement does not prove completion.
Observation-aware preparations also require checked configuration, state and
reference evidence. [C++ moveBy/moveTo](move_example.md) shows the existing
example owner's application API. Never fabricate prerequisite flags.

## Repeatable checks

The default finite regression is motor read-only:

```powershell
python scripts/bench_scenarios.py --port COM13 --scenario quick --count 10 --out build/bench/quick-new
python scripts/bench_scenarios.py --list
```

Use a new evidence prefix each time. Named motion/settings scenarios require
explicit selection; `--plan-only` prints a plan without opening the port.
See [scenario bounds and cleanup](bench_scenarios.md). No scenario replays an
uncertain write or treats a disconnected host as proof of standstill.
