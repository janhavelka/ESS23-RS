# Motor bench and testing authorization

## User report on 2026-10-02

| Item | Reported setup |
| --- | --- |
| Host port | COM13 |
| Current board firmware | CO2control |
| Motor connection | Motor connected to the board's RS485 bus |
| Indicator | Green light visible on the motor |
| Settings | User left the motor at its defaults |
| Mounting | Motor bolted to the table; shaft is free |
| Available testing | Communication and motion experiments when implementation is ready |

These are user-reported facts, not measurements made by this library. The
exact connected model, firmware, address, serial format, subdivision and
word order have not been read. ESS23-RS20 is the project's initial target;
that alone does not establish the identity of the attached motor. The LED
observation is recorded without assigning an unverified ready/alarm meaning.

The user explicitly permits motor testing on this setup, including motion,
and describes the free-shaft arrangement as suitable for experimentation.
This authorization persists for the described bench; routine communication,
commissioning and motion tests within that scope do not need another
permission question. Use documented commands and record what actually ran.
This note does not start a test session or claim any completed validation.

## Establish the host connection when testing starts

COM13 currently exposes the board running CO2control. Do not assume it is a
transparent USB-to-RS485 adapter or send raw RTU frames to an unknown console.
Inspect that firmware's available CLI/bridge and board configuration first.
Record the actual path from host to motor and any firmware change needed to
run the future standalone test application. Preserve enough firmware/build
information to reproduce or restore the bench setup.

The USB console baud rate and the motor RS485 baud rate are separate facts.
The board model, UART, TX/RX and DE/RE pins, direction polarity, echo behavior,
wiring/termination, power supply and current DIP positions remain to be
established. Discover them from available firmware/configuration and bench
observations when needed; do not invent pin assignments or defaults here.

The ESS manual lists 115200 baud and 8N1 defaults, but address selection and
actual settings require verification. Treat those values as candidate
commissioning settings, not measurements. The user report of defaults does
not resolve the manual's address/DIP or scaling inconsistencies; see the
[ESS implementation reference](reference/01_implementation_reference.md).

## First test sequence when real code exists

1. Verify that COM13 still identifies the intended board and establish its
   current firmware/console or bridge path. Keep one owner of the port/bus.
2. Use an implemented, reviewed non-changing probe and bounded discovery as
   needed. Record raw requests/replies, timing, local echo and identity
   confidence. No response does not establish that the drive is absent.
3. Read identity and relevant configuration/status. Resolve address, word
   order, command/feedback units and reference state before converted motion.
4. Exercise explicit enable/release, small position/angle/speed commands and
   stop behavior, then broaden to the documented native commands and desired
   operating modes. Record configuration and physical observations with the
   command's acknowledgement/completion evidence.
5. Exercise interruption, timeouts, lost acknowledgements and recovery with
   retained uncertainty. Track persistent and communication changes explicitly
   so subsequent tests use the actual active configuration.

Tests use the application-owned transport and caller-owned library contexts.
Neither this authorization nor a green indicator replaces protocol or motion
evidence. Hardware qualification records must name the exact tested model,
firmware, host firmware/build, pins/adapter, serial tuple, scale, test commands
and observed results. Logs must separate protocol success from physical
completion and record changes to the setup for the next session.

## Current status

- Bench access and motion-test authorization: recorded from the user.
- COM13 inspection and connection test: not performed.
- Motor identity/settings readback: not performed.
- Motion, conversion, stop and persistence qualification: not performed.

Track subsequent work in [the backlog](backlog.md). This dated setup note is
the starting point; update it with observations instead of silently treating
old settings as current measurements.
