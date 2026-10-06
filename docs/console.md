# Using the motor console

Open the board's serial terminal and type `help`. Ordinary commands print
readable headings, labeled values and next-step hints. `help COMMAND` shows the
exact syntax, with examples for common commands. Both Arduino and native ESP-IDF
use this console.

For firmware diagnosis, `drv` reports owner-watchdog subscription, feed errors,
completed turns, reset reason and the previous retained runtime progress marker.
`stats` reports `cache_interrupted` separately from a sampling-gap violation;
either can require explicit transport recovery. `reset` clears statistics, not
that fault. A watchdog restart is not a motor stop and never replays a move.
See the [firmware health report](reports/2026-10-06_firmware_health.md).

Start with one read and one small move on the existing free-shaft example:

```text
settings
moveby 100 steps
```

`settings` reads the drive now. It shows **Microstep / subdivision**, direction,
word order, control algorithm, encoder setting, soft-limit enable, positioning
speed, ramp times and target. A separate **Next move** section shows the host's
chosen values. Reading settings changes no motor parameter. The drive's
subdivision value and the host's angle conversion are explicitly separate.
Unknown firmware codes are printed as unknown, rather than guessed.

At boot the example chooses **60 rpm**, **100 ms acceleration ramp** and **100 ms
deceleration ramp**. It starts no movement. `moveby` performs missing read-only
preparation, sends the selected parameters and start, and polls the drive.
Wait for `Move complete`; repeating `moveby 100` needs no manual result release
after success or a read-only preparation rejection. Actual enable, alarm, encoding and limit checks still apply.

| Command | Meaning |
| --- | --- |
| `settings` | Read actual drive settings; also show choices for the next move. |
| `subdivision` | Read the drive's subdivision and related settings. |
| `subdivision options` | Show the documented range and useful example values. |
| `subdivision 1600` | Read/check/set/read back subdivision automatically while stopped. |
| `moveby 100` | Move by 100 command increments (default unit: steps). |
| `moveby 36 deg` | Move by 36 motor degrees using the host's scale. |
| `moveby 1/10 turn` | Same angular displacement, expressed exactly. |
| `moveto 100 steps` | Move to 100 command increments from this boot session's zero. |
| `moveto 0 deg` | Return to this boot session's zero. |
| `speed 90` / `speed 90 rpm` / `speed 90rpm` | Choose 0..3000 rpm; zero is accepted as a setting but prevents a move. |
| `accel 100 ms` / `decel 100ms` | Choose each ramp time in 0..2000 ms; these are not acceleration in steps/s^2. |
| `stepsperturn 1000` | Declare command increments per motor turn in the host; no drive subdivision write. |
| `stop normal` | Stop using the configured deceleration. |
| `stop fast` | Request ESS emergency stop without that ramp; RS485 command, not a hardwired safety circuit. |

Speed/ramp choices apply on the next move, not immediately. These manufacturer
positioning ranges replace the old 60 rpm / 250-increment experiment limits.
A move must also meet the drive's starting-speed setting, shown by `settings`.
Native target encoding and configured limits still apply. Motion observation is
bounded to 30 seconds by default (`ApplicationOptions::moveTimeoutMs`, 1..30000).
A timeout is a failed/possibly uncertain observation, not proof the shaft stopped.

Set subdivision with one command, for example `subdivision 1600`. The firmware
reads configuration and stopped state, uses the typed setter, verifies readback,
and refreshes configuration/state. No preparatory commands are needed.
`subdivision` reads it; `subdivision options` lists the integer range 400..51200
and useful examples. An unchanged setting sends no setting write. A matching host scale/origin is
preserved; missing host context is established from fresh stopped feedback.

A successful change updates the standalone host's assumed command steps/turn
and establishes a new RAM-only zero at the checked stationary position. The
completion message says so. It discards the old scale's profile snapshot; the
next move takes a fresh one. No movement or nonvolatile save is implicit.
`stepsperturn` remains a host-only declaration; the detailed `driver set`
route retains explicit preparation and invalidation semantics.

Successful ordinary interactive results remain inspectable until another bus
command needs their storage. They are recycled only after terminal output has
left both output queues. Failed/uncertain results, pending results and explicit
`@ID`/JSON/API results still require review/release. `status` and `result` do not
consume results. `read state` prints a short summary; `result ID` or `@1 result ID`
shows full evidence. Routine successful use does not require `release` commands.

`moveby` and `moveto` round to the nearest command increment, with half-step
ties going to the even integer. Error is at most half a command step. For
example, with 1000 command steps/turn, `moveby 10 deg` requests 27.777... steps
and uses 28. A changed target is reported with its rounding error; a target
that rounds to zero displacement sends no motion command. Requested and rounded
limits remain checked. Advanced `move`/`prepare` requests still default to exact
conversion and expose explicit rounding options.

The ESS manual documents replacing a positioning command while running via
the positioning interrupt bit. It does not guarantee that writing speed alone
updates an active move, or that replacement is a smooth speed transition.
The current `speed` command remains a next-move setting and rejects changes
during an active move. See [the investigation](reports/2026-10-06_rounding_and_live_speed.md).

The boot angle scale is a declared **ASSUMED 1000 command increments per turn**,
not a measured calibration or an inferred subdivision relationship. Set the
correct scale for your machine before relying on angles. Explicit target changes
clear that declaration. Millimetres (`mm`) require configured load travel;
the simple commands establish a RAM-only zero from the first checked stationary
feedback before movement in that boot session. No motor counter is cleared and
no axis position is saved in ESP NVS. The feedback-to-command coordinate relation
is an explicit standalone ASSUMED convention, restricted to nonnegative signed
32-bit values. This is not homing or calibrated position proof. Motion occurring
before that first observation cannot be reconstructed. Completed finite moves
keep the zero. Interrupting our move and then confirming a stop also keeps
that fixed offset, but requires fresh position feedback before the next move.
Idle observation expiry and `motion-profile restore` also preserve the fixed
zero: neither changes the motor counter or coordinate scale.
Enabled feedback variation does not erase that fixed counter offset. Our move
settling remains associated with its command until fresh state confirms standstill.
Motor release, unexpected running, counter clear or interpretation changes
invalidate it without silently choosing a new zero.

The advanced `move absolute ... steps native ...` route retains explicit device
counter semantics. Negative encoding remains unavailable unless
its existing profile prerequisite is established.

`help` is one complete grouped menu, covering everyday control and diagnostics.
Each command has one spelling. `help COMMAND` shows detailed syntax and examples.
Existing `driver`, `read config` and `motion-profile` commands remain
for specific operations and automation. `config` describes the host connection;
`settings` now means actual motor settings, replacing the former host alias.
`motion write` selects default full setup. Optional `motion stored` requires
matching remembered parameters and does not read before every repeated move.

Normal motion output is short:

```text
Move complete (operation 12).
Drive reported running, then target reached.
No drive alarm reported.
Details: @1 result 15.
```

This describes checked drive feedback; it is not independent shaft measurement.
If preparation fails, the console says no motion command was sent and gives the
reason. A delivered preparation-only rejection is inspectable until the next
simple command automatically reclaims it; it does not lock the motor. Results
from admitted failed/uncertain motor operations remain available under the
child ID printed by `Details`. They do not keep owning the motor after a
confirmed stop. You can issue the next move immediately; `release N` frees
retained result storage only, before or after that next move. Eight ordinary
result slots are finite: a full store reports `results_full`, not an ownership
conflict. An uncertain write is never automatically retried.

The next explicit stop replaces a delivered successful stop result in its
reserved slot, so repeated successful stops do not fill ordinary storage.
A failed/unknown stop is retained in a free ordinary slot before admitting
another stop; if none exists, inspect and release a terminal result first.
Active or undelivered stops cannot be replaced. Local cancellation, `reset`
and `recover` never substitute for a confirmed motor stop.

For basic communication inspection:

```text
help
version
config
probe
read identity
read config
read state
status
health
```

`probe` and the explicit `read` commands communicate with the selected motor.
`status` and `health` inspect cached observations; they do not refresh the drive.
Use `useaddr N` to select another address locally while idle. It does not change
the motor's address. Addresses, raw values and unknown interpretations remain
visible rather than being guessed.

## Operations and results

An admitted operation receives an operation number and finishes asynchronously.
The acceptance message is not proof of a completed action. Read the terminal
outcome, execution evidence and completion observation separately, especially
when execution is unknown.

- `result N` inspects the retained operation without consuming it.
- `release N` releases a completed host result; it does not release motor windings.
- `cancel N` cancels local work; it does not stop a motor.
- `stop normal` and `stop fast` request their distinct documented stop policies.
- `motor-release` explicitly requests winding release; `enable` requests enable.

`stop fast` is the ESS emergency-stop command over RS485: it bypasses the
configured deceleration ramp. `stop normal` uses that ramp. Both use the existing
priority stop path after the in-flight bus transaction settles. A serial stop
requires working communication and is not a hardwired emergency-stop circuit.
`cancel` does not send either stop command.

`help stop` and `help motion-profile` describe the exact typed routes.
Use `help move` for the full advanced move grammar (`@1 help move` for JSON). Existing `move relative|absolute|angle`
commands and public C++ requests remain available.
These commands keep the same prerequisites and sequence as the regular library
API. No motion or setting changes occur merely by opening the console or asking
for help. `recover` repairs host transport only, and `stats reset` clears host counters.
Neither command replays a motor write or erases an uncertain outcome.

## Diagnostics

`debug raw` shows copied TX/RX bytes and software observations. `debug decoded`
adds checked function/register interpretation. `debug off` stops this display.
Protocol consumers retain the original bytes. Use `drv`, `stats`, `memory` and
`load` for owner, transport, memory and available software timing measurements.
See the [debug workflow](traffic.md) for effects and measurement limits.

## Automation

Prefix a command with `@` and a nonzero decimal ID to receive the existing JSONL
protocol, for example `@42 read state`. Python tools already use this spelling;
their existing schemas, command correlations and operation semantics remain available.
`@ID settings` adds a `motor_settings` report; the old host alias is now `config`.
The new simple move commands add a `simple_move` report with a session operation
ID and a separate `move_operation_id` for the ordinary typed move's full evidence.
Release the session ID, not its owned child. A subsequent simple move reclaims
its previous delivered successful session. Explicit JSON/API results remain
retained; ordinary successful human results may also be recycled when another
human bus command starts. Failed/uncertain results stay pinned.

Reply format is attached to each admitted operation. A human `status` request
cannot change a pending machine operation's terminal format, and a machine query
cannot change a pending human operation's format. `result N` uses the format of
that inspection request. A debug mode selection chooses its traffic display
format; a passive query does not change it.

Formatting stays in the example application. Output uses fixed buffers and the
same bounded queue on both platforms. A slow terminal may delay or drop diagnostic
display, but it cannot consume motor results or block the bus owner. There are no
ANSI color codes or blocking direct serial prints. Machine commands retain
single-line JSON; human replies are complete multiline blocks.

## Controller-reset diagnostics

`drv` includes `previous_runtime`: a 28-byte diagnostic record in RTC RAM,
read once at startup. It is not NVS and contains no motor position, queued work
or replay state. After a controller reset it can retain the prior uptime,
service-loop count, input-line count, output queue/backpressure and last stage:
1 startup, 2 owner, 3 operations, 4 console, 5 USB output-space check,
6 USB write, 7 idle, 8 snapshot, 9 capture statistics, 10 heap statistics,
11 stack statistics. Power loss may erase it. A stage is a progress witness,
not a stack trace or proof of a particular USB fault.

Repeated absolute simple targets use retained successful completion plus a
fresh stationary/arrived observation to avoid retriggering the same target.
Feedback may differ by the drive's arrival window; this does not claim exact
shaft alignment. A changed target, generation or intervening motion cannot
reuse that completion. The advanced native route remains explicit.

### Known high-subdivision completion discrepancy

On the recorded ESS23-RS20,51200 subdivision with speed2000 and a900-degree
relative request can leave the drive reporting running after the main movement.
Do not release/replay the move to bypass this. `stop fast` established stopped
state in the reproduced case; normal stop did not. The same assumed2.5-turn
request completed at1600/2000 and51200/60. Use `subdivision 1600` for the tested
2000-speed path; changing subdivision explicitly establishes a new session zero.
The exact drive cause remains [open with evidence](reports/2026-10-06_completion_discrepancy.md).
