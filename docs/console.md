# Using the motor console

Open the board's serial terminal and type `help` or `?`. Ordinary commands print
readable headings, labeled values and next-step hints. `help COMMAND` shows the
exact syntax, with examples for common commands. Both Arduino and native ESP-IDF
use this console.

For a small move on the existing free-shaft example:

```text
speed 60
accel 100
decel 100
moveby 100 steps
```

Wait for `Move complete`. The example sends the selected parameters, sends start,
and polls the drive. It performs missing configuration/profile reads itself and
checks the starting state. Repeating `moveby 100` needs no manual result release
after success. Speed is motor RPM; acceleration/deceleration are **native drive
ramp settings**, not degrees/s² or a claimed physical acceleration. These local
choices apply on the next move and do not write the motor immediately.

`help move` shows this short workflow. `motion` shows the remembered choices;
`motion write` selects the default full parameter setup. Optional `motion stored`
uses the existing start-only policy and requires matching remembered parameters.
It is not a read-before-every-move optimization.

| Command | Meaning |
| --- | --- |
| `moveby 100` | Move by 100 command increments (default unit: steps). |
| `moveby 36 deg` | Move by 36 motor degrees, with an explicitly declared scale. |
| `moveby 1/10 turn` | Same angular displacement, expressed exactly. |
| `moveto 100 steps` | Move to native absolute target 100. |
| `moveto 36 deg` | Move to a host angular coordinate; also requires an origin. |
| `stepsperturn 1000` | Declare 1000 command increments per motor turn in the host. Use only your actual scale. |
| `stop normal` | Request the documented normal stop. |

Angles need the correct command increments per turn; the example never guesses
them from an ambiguous microstep setting. Millimetres (`mm`) need configured load
travel. `moveto` does not invent an origin. The current bench example keeps its
existing small-motion limits (at most 250 increments; native absolute start and
target must both be within 0–250). Those are example limits, not library range
limits. Unsupported direction/encoding or missing prerequisites fail explicitly.

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
- `stop normal` and `stop direct` request their distinct documented stop policies.
- `motor-release` explicitly requests winding release; `enable` requests enable.

`help stop` and `help motion-profile` describe the exact typed routes.
Use `@1 help move` for the full advanced move grammar. Existing `move relative|absolute|angle`
commands and public C++ requests remain available.
These commands keep the same prerequisites and sequence as the regular library
API. No motion or setting changes occur merely by opening the console or asking
for help. `recover` repairs host transport only, and `reset` clears host counters.
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
their existing schema, command correlations and operation semantics are unchanged.
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
