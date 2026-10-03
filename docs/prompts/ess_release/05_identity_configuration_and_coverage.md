# 05 — Typed identity/configuration reads and coverage tracking

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 01–03's working request/result path; 04 has a recorded disposition. This step performs only reviewed non-changing reads.

## Read and reuse

Read the profile/discovery contracts, ESS JSON ledger, generated descriptors, original function-manual ESS appendix and `docs/reference/06_encoder_units.md`. Reuse `Codec.h`, checked parsers and the existing units configuration. `Types.h` is generated: do not insert handwritten operation structs there.

## Implement

- Establish a compact operation coverage inventory linked to ledger record IDs. Track read/write/action obligations, source certainty, model availability, implementation, CLI reachability and native/hardware evidence separately. Avoid another register-address source of truth.
- Implement real typed identity/version/address/DIP preparations and decoders. Preserve raw model `0x4EEA` and undocumented firmware values; do not guess their model mapping.
- Add the bounded read-only configuration subset needed for motion: direction, subdivision, paired-word order, serial settings, algorithm, configured encoder resolution and input-function/polarity settings needed to establish serial-motion prerequisites. Read windows stay within reviewed access and the 16-word limit.
- Record the application's declared external wiring separately from drive input
  assignments and observed levels. Do not require external I/O for serial-only
  operations; retain unresolved input effects as prerequisites for the specific
  affected operation. This step only reads settings; typed I/O changes belong to 15.
- Separate stored/pending settings from the observed active transport tuple. Resolve pair decoding before using a pair; unresolved physical units remain raw with a reason.
- Introduce only the common capability and operation types actually consumed by these reads. Caller-owned bounded state, supplied events/time and yielded requests may support a multi-read operation; there is no I/O inside it.
- Put handwritten public operation/event types under `include/MotorControlRS`, separately from generated enums. Map runner evidence in the application; public signatures must not depend on example `Rtu` or console/cache types. Compile new public headers with the installed core and no example include path.
- Add `read identity`, `read config`, minimal `caps` and profile identity/configuration routes through these same public APIs.

## Verify

Test malformed replies, exceptions, unknown enums, wrong target/generation, gaps, partial read failure and immutable previous observations. Run the typed reads on COM13 and retain exact raw data/configuration provenance. Configured encoder counts do not identify the encoder's manufacturer or prove physical resolution. Record missing label/firmware identification explicitly.

## Subagents and handoff

Assign a manual/coverage reviewer and a decoder/API test reviewer. Audit no-change side effects and descriptor generation. Deliver the actual observation/preparation types and remaining motion prerequisites to 06–09; finish the common audit, documentation and sync steps.
