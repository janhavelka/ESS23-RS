# Native ESP-IDF standalone consumer

This is a real ESP32-S3 application using ESP-IDF 5.5.5. It compiles the same
`ProbeApp.cpp`, console, owner, UART/capture and library operations as Arduino;
only framework startup and USB/task adapters differ.

See [build, board configuration, flash and recovery instructions](../../docs/esp_idf_probe.md)
and [prompt25 verification](../../docs/reports/ess_release_25_2026-10-05.md).
Board pins and memory choices are example Kconfig/defaults, not public APIs.
The default workload is zero and startup emits no motor request.

Use the native `idf.py`/CMake build. Arduino is built through the existing root
PlatformIO environments. Cross-platform motion/load qualification belongs to26.
