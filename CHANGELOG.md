# Changelog

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
