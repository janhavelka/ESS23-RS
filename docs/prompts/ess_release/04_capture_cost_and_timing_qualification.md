# 04 — Review capture cost and qualify available timing

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 03's owner-integrated read-only image. Read-only work in 05–07 may continue after this step's disposition if instruments are missing; dependent physical actions require the relevant evidence.

## Read and reuse

Inspect `E2Uart.*`, `E2Load.*`, `test/capture_service_test.cpp`, the pinned SDK source/configuration and `docs/reports/2026-10-03_capture_load.md`. Audit FieldCore's current backend and service constraints read-only. The historical 20-us sampler costs about 19–21% of one core inside capture alone.

## Work

- Reproduce the current unloaded and loaded baseline. Measure interrupt/sample work, active-owner gaps, failures and memory. Separate scheduler CPU estimates from capture section time.
- Decide from evidence whether to retain the present mechanism, reduce its cost without losing evidence, or replace it with a simpler qualified capture path. Do not implement multiple speculative backends or infer byte timing from untimed batches.
- Check physical TX completion, DE hold/release, first RX, final stop-bit publication, idle watermarks and echo assumptions with an independent TX/RX/DE capture when equipment is available. Preserve waveforms and align them with firmware traces.
- Establish the selected SDK's cache-off/interrupt-starvation policy. An IRAM-marked callback alone does not prove cache-safe execution. If such periods are unsupported, expose/enforce that operating restriction rather than claiming uninterrupted capture.
- Keep ambiguous intervals, FIFO/ring overflow and missing evidence explicit failures. Do not loosen a guard only to improve a pass rate or CPU percentage.

## Verify and disposition

Rerun native delayed-service/masked-interrupt tests and the affected application suite after changes, then repeat the original hardware scenarios on the final image. Missing analyzer/fixture evidence stays NOT RUN with the exact missing measurement. Record a supported operating envelope and which physical write/motion tests remain gated. Do not block independent codec/read-only progress or invent a qualification pass.

## Subagents and handoff

Assign an SDK/timing reviewer and an independent experiment reviewer. Deliver the measured selection, implementation fixes if justified, raw evidence and remaining gates; audit/refactor for one clear capture owner and complete the common commit/sync workflow.
