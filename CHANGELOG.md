# Changelog

## 0.1.0

- Add framework-independent displacement, velocity and acceleration conversion
  with independent unit preferences, explicit rational scales and provenance.
- Add the ESS-RS register catalogue, native choices and documented uncertainties
  from the original function manual; generate C++ metadata from one JSON source.
- Add native tests, CMake/ESP-IDF component and PlatformIO packaging foundations.
- Record the E2 revision 2.0.0 bench pins and FieldCore build provenance.

This release does not yet build or exchange motor frames, run motion commands,
or implement the planned standalone CLI. No hardware tests have run.
