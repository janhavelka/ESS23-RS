# Motor bench and testing authorization

## User report on 2026-10-02

| Item | Reported setup |
| --- | --- |
| Host port | COM13 |
| Current board firmware | CO2control |
| Physical board | User-confirmed E2, revision 2.0.0 |
| RS485 pins | User-confirmed DE GPIO21, TX GPIO47, RX GPIO48 |
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

COM13 originally exposed the board running CO2control; see current status below.
Do not assume it is a
transparent USB-to-RS485 adapter or send raw RTU frames to an unknown console.
Inspect that firmware's available CLI/bridge and board configuration first.
Record the actual path from host to motor and any firmware change needed to
run the standalone test application. Preserve enough firmware/build
information to reproduce or restore the bench setup.

The USB console baud rate and the motor RS485 baud rate are separate facts.
The user-confirmed revision 2.0.0 bench uses DE21/TX47/RX48. The standalone
application selects those pins explicitly; its adapter has no board preset.
See the [bench configuration](reference/04_esp32_bench.md). The original E2
board label is historical identification, not a motor-bus requirement.
Direction timing, echo behavior, wiring/termination, power supply and current
DIP positions still need their own qualification.

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

- [Prompt 05](reports/ess_release_05_2026-10-04.md), 2026-10-04, uploaded the typed-read timer image. Five identity/config pairs (including loaded/common/profile routes), 13 model probes and one long capture read passed: 44 frames/468 RX bytes, no transport errors. Model4EEA/version0029/node1/DIP0, subdivision1000, configuredencoder4000 and unknown algorithm3 are retained without inferred mapping. Workload is off, DE released and no pending/retained work or recovery requirement remains; wiring/input levels and physical encoder/model/firmware qualification remain unknown.

- Latest verification: [prompt 05 fresh audit](reports/ess_release_05_audit_2026-10-04.md),
  2026-10-04: one identity/config pair and ten model probes passed
  (16 transactions/138 RX bytes), unchanged timer image. Load remains off,
  DE released, no pending/retained work or recovery requirement.

- [Prompt 04 fresh audit](reports/ess_release_04_audit_2026-10-04.md),
  2026-10-04, repeats 140 successful reads plus expected pre-TX timeout/recovery
  with the stricter host harness on the unchanged image. Workload is off, DE
  released and no pending/retained work or recovery requirement remains.
- [Prompt 04](reports/ess_release_04_2026-10-04.md), 2026-10-04,
  uploaded the recorded final timer image to inspected COM13. 140 read-only
  transactions passed (64 fixed 37-byte replies); the expected 20-ms pre-TX
  failure and explicit recovery passed. Ending load0/0/0, DE released, no
  pending/retained work or recovery requirement. Original backup unchanged.
  No independent analyzer was available; electrical timing remains NOT RUN.
- The [fresh prompt 03 audit](reports/ess_release_03_audit_2026-10-04.md)
  uploaded the corrected timer console and passed 37 checked read-only probes
  plus the expected zero-TX 20-ms-delay failure and explicit recovery.
  Workload is disabled, DE settled, no pending/retained work at cleanup.

- Prompt 03 connects the real BusOwner and responsive console. Current timer
  unloaded/load/interleaved and delayed-observation evidence, exact image and
  the reproduced 20-ms setup-budget boundary are in
  [the prompt 03 report](reports/ess_release_03_2026-10-03.md). Workload is disabled
  at cleanup; no motor writes or motion occurred.

- The [fresh prompt 01 audit](reports/ess_release_01_audit_2026-10-03.md) uploaded
  the corrected Runner and passed 10 unloaded, 10 loaded and 3 final checked
  read-only probes; workload is disabled. Exact current-image evidence is there.
- Prompt 01 rebuilt the Runner/timer console and passed 10 unloaded, 10 loaded
  and 3 final read-only probes. The exact retained image and evidence are in
  [the prompt 01 report](reports/ess_release_01_2026-10-03.md). Workload is disabled;
  bus-owner integration/absolute-deadline hardware tests remain for prompt 03.
- Bench access and motion-test authorization: recorded from the user.
- COM13 inspected on 2026-10-03; original CO2Control-node 1.3.0 flash backed up.
- Board now runs the MotorControl-RS 0.6.0 timer-capture load/probe console;
  workload is explicitly disabled at the end of testing. See the
  [capture/load audit](reports/2026-10-03_capture_load.md).
- The [platform-boundary audit](reports/2026-10-03_platform_scope_audit.md)
  rebuilt and flashed `bench_s3_load_timer` with explicit pin configuration.
  Read-only probes and the loaded regression passed; motor settings were unchanged.
- The [0.5.1 audit](reports/2026-10-03_audit.md) adds 20 checked probes and
  timeout/recovery/alias/reset evidence; the original flash backup is unchanged.
- Repeated checked model reads succeed at node 1, 115200 8N1, TX47/RX48/DE21.
- Raw model: `0x4EEA`; exact model/firmware mapping and settings remain unknown.
- External TX/RX/DE timing qualification remains open; see the explicit bench
  turnaround exception and sampling assumptions in the report.
- Motion, conversion, stop and persistence qualification: not performed.

See the [dated bench report](reports/2026-10-03_e2_probe.md) for evidence, firmware
backup/restore information, measured intervals, memory and qualification limits.

Track subsequent work in [the backlog](backlog.md). This dated setup note is
the starting point; update it with observations instead of silently treating
old settings as current measurements.
