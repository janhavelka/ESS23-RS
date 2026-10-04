# Changelog

## Unreleased

- Add typed ESS driver-settings reads and seven stopped-state single-word
  updates with checked readback, immutable partial progress and settings effects.
  Preserve raw limit pairs and reject unreviewed pair writes before traffic.
  Extend existing console/Python correlation and conservative cache invalidation.


- Preserve reference age across host preference changes and invalidate origins
  on external movement during stop or other axis reservations. Retain clear
  uncertainty through failed observations, result release and recovery.

- Add exact wrapped paths and shared finite absolute/angle motion preparation,
  copied reference provenance, zero-only ESS device position clear and coordinate
  confidence invalidation. Expose common/native console and Python routes;
  physical movement and clear remain gated by outstanding qualification.

- Add bounded ESS enable/release, alarm-clear and explicit normal/direct stop
  operations, application axis reservations and reserved stop delivery. Preserve
  write uncertainty separately from checked status completion; physical actions
  remain gated by independent timing and echo qualification.

- Add exact host target preparation, native/rational input, frame-aware scaling,
  origin/reference validation, quantization and limits. Reuse the units factor
  planner; host-only console configuration and previews perform no motor writes.
- Fix referenced relative-displacement limits, exact zero-radian preparation,
  radian cancellation onto zero and malformed decimal fraction operands.

- Remove product-specific naming from the standalone RS485 reference and
  release prompts. Rename example environments to `bench_s3_*` and keep
  FieldCore guidance limited to a future RS485 motor integration.
- Rename the example adapter to `Esp32S3Uart`; applications now supply checked
  TX/RX/DE pins and direction polarity. Native tests cover alternate wiring,
  active-low direction and initialization failures. Core APIs are unchanged.
- Specify optional/unconnected motor I/O and explicit ESS no-function assignment
  in the contracts and prompts. Typed I/O setters remain planned work.

## 0.6.0

- Separate scheduled wire arrivals from owner/capture service in native SDK
  fixtures. Exercise FIFO and capture-ring overflow, late/foreign replies,
  missing timing evidence and interrupt masking without automatic replay.
- Add optional GPTimer capture and autonomous DE release on E2, with a bounded
  internal ring and atomic `TxObservation` in the application runner port.
  Delayed delivery uses physical completion/release evidence. Timer lifecycle
  failures retain ownership and require explicit recovery.
- Add a bounded E2 task/console load fixture and Python load campaigns with
  retained failures, service gaps, latency, CPU estimates, RAM and stack data.
  Fix complete-line serialization, maximum USB payload admission and consistent
  measurement reset windows. The reusable core and ESS access policy are unchanged.
- Record native and read-only hardware audit evidence and a release roadmap.
  External electrical timing, cache-off behavior, motion and FieldCore integration
  remain unqualified; the FieldCore repository was not edited.

## 0.5.1

- Fix RX silence evidence around FIFO/state-machine races and reject snapshots
  spanning a whole character. Require fresh evidence after host recovery.
- Keep terminal fault reporting and cached commands available while TX/DE
  cleanup is pending. Retain codec/exception details; checked device exceptions
  do not require transport recovery. Admit no probe after a newly sampled fault.
- Report truncated RX evidence, expose DE state, distinguish synchronous `done`
  from probe `accepted`, and use a consistent command name for `ping` replies.
- Check profile/address and successful result evidence in the Python harness.
  Add native tests of the actual application loop using the shared SDK fake.

These fixes do not establish external UART timing qualification or motion
support. The framework-independent core and ESS register policy are unchanged.

## 0.5.0

- Add the E2 UART2 polling adapter, read-only JSONL probe console and Python
  probe/stress/health/memory tools. Native SDK fakes compile the real adapter;
  recorded bench probes use TX47/RX48/DE21, address 1 and 115200 8N1.
- Represent timing as conservative intervals, reject ambiguous framing and
  retain uncertainty in traces. Add an explicit per-request turnaround minimum;
  the E2 bench uses 304 us after observed replies failed the 1750 us default.
  Host admission and final frame gaps remain 1750 us.
- Allocate the console, runner, frame buffers and history once in PSRAM; keep
  capture state internal and expose memory/stack measurements.
- Fix Arduino macro collisions: `DefaultDirection::DEFAULT` becomes `NORMAL`,
  and `SoftLimitEnable::DISABLED` becomes `LIMITS_OFF`. Register values are unchanged.

External sampling/DE timing qualification, exact identity for raw model `0x4EEA`,
native ESP-IDF firmware and motion remain open. This polling bench adapter is
not a drop-in replacement for FieldCore's shared bus owner.

## 0.4.0

- Add an application-owned RTU runner under `examples/common/`, tested with
  a native fake adapter. Physical TX drain, timestamped receive framing,
  explicit echo policy, deadlines, cancellation and recovery are separate
  from the existing checked ESS codecs.
- Retain transaction results and raw buffers, with optional caller-owned
  trace storage and saturating counters. No allocation, hidden retries or
  platform code enter the runner or reusable core.
- Document FieldCore reuse, ESP32 adapter timing requirements, PSRAM placement
  and future Python bench automation. Measure runner storage on native and
  ESP32-S3 compilers.

The production UART adapter, standalone motor CLI and motion workflows remain
future work. Native tests and target compilation do not qualify hardware;
COM13 was not opened or flashed. The runner is example code in the repository,
not an added transport dependency in the distributed core package.

## 0.3.0

- Rename the package to MotorControl-RS and namespace/include/CMake identity to
  MotorControlRS. Update consumers to the new includes and target names; the
  repository URL remains unchanged pending the user's GitHub rename.
- Support the four documented ESS FC10 windows, adding the position, speed
  and homing examples. Correct the malformed p16 example in independent tests.
- Fix unnecessary gearing/lead requirements in same-basis unit conversions
  and an intermediate-underflow error on binary64 `long double` platforms.
- Generate compact codec access policy separately from descriptive catalogue
  strings. Record and validate every undocumented appendix interval: 78 words
  in 15 gaps, plus the documented reserved/unspecified-access entries.
- Record RTU framing, motion/configuration timing and source conflicts from
  the original manuals. Unspecified firmware deadlines remain unqualified.

No transport or motion workflow is added; COM13 remains untested.

## 0.2.0

- Add bounded ESS FC03/FC06/FC10 request builders and checked response parsers,
  with exact expectations, raw exceptions, write echoes and unchanged outputs
  on failure. FC10 initially supports only the documented 0x0024/two-word write.
- Add the one-word model-register probe, explicit 32-bit word conversions and
  independent native codec tests. No I/O, retries or timing enter the core.
- Preserve five contrasting manufacturers' manuals and document their protocol
  differences, probe candidates and implications for future serial profiles.

This release builds and parses frames but does not exchange them with a motor.
Transport, motion sequences, full typed native commands and CLI remain future
work. Hardware behavior is unqualified; other reviewed drives are not supported.

## 0.1.0

- Add framework-independent displacement, velocity and acceleration conversion
  with independent unit preferences, explicit rational scales and provenance.
- Add the ESS-RS register catalogue, native choices and documented uncertainties
  from the original function manual; generate C++ metadata from one JSON source.
- Add native tests, CMake/ESP-IDF component and PlatformIO packaging foundations.
- Record the E2 revision 2.0.0 bench pins and FieldCore build provenance.

This release does not yet build or exchange motor frames, run motion commands,
or implement the planned standalone CLI. No hardware tests have run.
