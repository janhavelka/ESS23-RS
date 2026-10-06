# Convenience rounding and ESS live-speed investigation

## Source finding

Rechecked the [original function PDF](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf), physical pages16,17,18,70,71. Page16
(printed14) documents positioning command0x0027: bit0 starts using supplied
parameters, bit2 selects absolute positioning and bit3 interrupts current
positioning to execute the new command immediately. Page71 repeats this in
the ESS appendix. Page70 identifies0x0023 as writable positioning speed but
does not specify continuous sampling or live update. The speed-mode pages
likewise do not establish live-update semantics. Original file SHA256:
`0ca2d7f6b69404ead3ba9af982076c54f86eb16b1c24ba237f344aac079b81eb`.

Therefore in-motion **position-command replacement** is documented. A bare
speed write changing the current trajectory and a smooth same-endpoint speed
transition are not established. A replacement experiment must retain the
original absolute endpoint; repeating a relative displacement can extend travel.
The manufacturer's A6-RS real-time speed FAQ is for another family and supplies
no ESS qualification. No live-speed API or hidden stop/restart is added here.

Current `speed` selects future host intent and rejects changes while the simple
session is pending. The current firmware does not issue the replacement start.
Exact-firmware comparison of a bare speed write versus new speed plus absolute
interrupt/start, with bounded travel, retained traffic/feedback and explicit
stop/restoration, remains open. An echo alone cannot prove a speed change.

## Rounding implementation

Simple CLI moves and application C++ `moveBy`/`moveTo` share nearest-step
defaults and the existing exact rational converter: ties to even, maximum
quantization error0.5 command increment. Core/detailed requests retain EXACT.
Requested/effective limits, overflow and generation checks are unchanged.
Simple results retain the effective native target and signed rounding error,
including no-op targets; human output reports nonzero rounding. No conversion
formula, result queue or motor sequence was duplicated.
The direct operation API still rejects a rounded-zero move before TX; the
interactive wrapper can report it as a no-op without creating a motor operation.
Existing direct native-step versus console boot-origin semantics are unchanged.

FieldCore's current `src/rs485/Rs485Task.cpp` remains a read-only application-owner
reference. Its owner and firmware types are not imported or edited.

## Verification

Native regression covers fractional degrees, both half-step ties, absolute
targets, zero-after-rounding/no motion TX, direct C++/CLI effective-target parity,
unchanged detailed EXACT rejection and human result formatting.
The quick verifier passed77 registered native/Python/check suites plus clean
source/install consumers and codec link isolation. The normal Arduino timer
image built and flashed successfully; hosted CI also checks both compilers and
the Arduino/native-IDF firmware. Exact-commit CI is checked after pushing.

COM13 pre-flash readback confirmed standstill. The final image's finite regression
passed42 frames with zero failures/timeouts/capture faults/RX errors. At60rpm,
`moveby 100 deg` selected278 command increments with+0.222222... rounding error;
drive position changed62060→62338. `moveby 0.1 steps` rounded to0 and sent no
motion command. `moveto 0 deg` returned62060. Explicit normal stop and exact
profile restoration to `[30,100,100,200,0,62060]` succeeded. Final speed/alarm0,
nonrunning/enabled, DE released, no pending owner/output work, load/monitor/debug
off. COM13 closed. These are drive observations, not independent shaft measurements.

Owner/capture gaps249/58us; internal free/minimum335776/330616 bytes;
PSRAM free8177196; owner stack headroom2452 bytes. Image SHA256:
`6256938c1de3369d85a28c0fba4976636b385fe6a93e4635c8e90570b24bb116`.
Native checks/build outputs are in `build/rounding_live_speed/` and
`build/ownership_fix/pio/bench_s3_load_timer/`. The
[retained evidence archive](2026-10-06_rounding_evidence.zip) contains exact
finite scripts, raw/structured evidence and before/after diagnostics. Archive
SHA256 `b6726693ae9126e9e8abdd173113dc067500050cb06c81d6aa78b6448220efa7`.

No in-motion speed write/retrigger experiment was performed; its transition
semantics remain unqualified. Rounding tests do not establish that capability.
