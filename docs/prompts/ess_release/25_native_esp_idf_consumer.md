# 25 — Native ESP-IDF standalone consumer

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: the shared public API/CLI and application owner are real; the current root CMake IDF component registration alone is not an IDF firmware example.

## Read and reuse

Read architecture/ownership, current CMake/package metadata, E2 board/pins/partitions, selected capture SDK dependencies and all application consumers. Inspect FieldCore's platform boundary read-only; do not import its product headers.

## Implement

- Add a real native ESP-IDF E2/S3 example using the same core, portable console semantics, owner, operations and evidence/result contracts as Arduino.
- Separate unavoidable framework startup/USB/task/allocation code into small platform adapters. Extract shared application logic only where the second real consumer proves reuse; avoid two copies of the motor workflow.
- Keep exclusive UART/DE ownership and the measured capture/clock contract. Check SDK compatibility explicitly; do not assume Arduino's prebuilt SDK flags match the native IDF configuration.
- Use caller-owned bounded storage, measured PSRAM placement and required internal ISR/driver/stacks. Expose initialization/allocation failure without a silent large internal fallback.
- Preserve CLI names, profile coverage and host-only semantics. Framework choice must not silently omit native motor features or change rounding/stop behavior.
- Document standalone build, flash and recovery commands and keep library component consumption independent of examples and vendor PDFs.

## Verify

Build a clean IDF application and clean core component consumer with public-header isolation. Run portable console/operation scenarios against both implementations. Perform an initial read-only COM13 smoke on the IDF image, recording image, SDK configuration and memory; preserve the known image/backup. Full cross-platform motion/load evidence belongs to 26 and uses its prerequisites.

## Subagents and handoff

Assign a platform/dependency reviewer and an application-parity reviewer. Audit duplicated state/CLI logic and framework leakage. Deliver reproducible IDF build settings, adapters and source-sharing boundaries to 26; complete common review/report/commit/sync.
