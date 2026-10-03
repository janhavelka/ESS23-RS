# Changelog

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
