# Changelog

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
