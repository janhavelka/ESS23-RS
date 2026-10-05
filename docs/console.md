# Using the motor console

Open the board's serial terminal and type `help`. Ordinary commands print
readable headings, labeled values and next-step hints. `help COMMAND` shows the
exact syntax, with examples for common commands. Both Arduino and native ESP-IDF
use this console.

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
after success. Actual enable, alarm, encoding and limit checks still apply.

| Command | Meaning |
| --- | --- |
| `settings` | Read actual drive settings; also show choices for the next move. |
| `moveby 100` | Move by 100 command increments (default unit: steps). |
| `moveby 36 deg` | Move by 36 motor degrees using the host's scale. |
| `moveby 1/10 turn` | Same angular displacement, expressed exactly. |
| `moveto 100 steps` | Move to native absolute target 100. |
| `moveto 36 deg` | Move to a host angular coordinate; also requires an origin. |
| `speed 90` | Choose 0..3000 rpm; zero is accepted as a setting but prevents a move. |
| `accel 100` / `decel 100` | Choose each ramp time in 0..2000 ms; these are not acceleration in steps/s^2. |
| `stepsperturn 1000` | Declare command increments per motor turn in the host; no drive subdivision write. |
| `stop normal` | Stop using the configured deceleration. |
| `stop fast` | Request ESS emergency stop without that ramp; RS485 command, not a hardwired safety circuit. |

Speed/ramp choices apply on the next move, not immediately. These manufacturer
positioning ranges replace the old 60 rpm / 250-increment experiment limits.
A move must also meet the drive's starting-speed setting, shown by `settings`.
Native target encoding and configured limits still apply. Motion observation is
bounded to 30 seconds by default (`ApplicationOptions::moveTimeoutMs`, 1..30000).
A timeout is a failed/possibly uncertain observation, not proof the shaft stopped.

The boot angle scale is a declared **ASSUMED 1000 command increments per turn**,
not a measured calibration or an inferred subdivision relationship. Set the
correct scale for your machine before relying on angles. Explicit target changes
clear that declaration. Millimetres (`mm`) require configured load travel;
`moveto` does not invent an origin. Negative encoding remains unavailable unless
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
reason. Failed, cancelled or uncertain results stay retained: inspect `result N`
and explicitly `release N` after review. Release is local result housekeeping,
not motor winding release. An uncertain write is never automatically retried.

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
only its previous delivered successful session; unrelated results are retained.

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
