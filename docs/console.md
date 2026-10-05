# Using the motor console

Open the board's serial terminal and type `help` or `?`. Ordinary commands print
readable headings, labeled values and next-step hints. `help COMMAND` shows the
exact syntax, with examples for common commands. Both Arduino and native ESP-IDF
use this console.

Start with these commands:

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

`help move`, `help stop` and `help motion-profile` describe the exact typed routes.
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
their schema, command correlations and operation semantics are unchanged.

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
