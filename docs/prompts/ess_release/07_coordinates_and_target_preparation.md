# 07 — Exact target preparation, coordinates and limits

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 05–06's configuration provenance and observation context. This block adds pure preparation and host configuration; no motor write is needed to validate arithmetic.

## Read and reuse

Read the complete axis contract, `include/MotorControlRS/Units.h`, `src/Units.cpp`, existing tests and encoder/unit references. Reuse conversion factors and range checks; do not implement a second CLI conversion path.

## Implement

- Add the actual axis configuration/reference types and validation needed by position preparation. Distinguish motor/load frames, command increments, full steps, identified encoder sources and configured travel.
- Preserve exact native and rational inputs, checked origin arithmetic, signed range and precision provenance. Define quantization with EXACT default and explicit rounding/tie policy. Radians require approximation/error limits.
- Validate requested and effective targets against applicable limits; report rounding error and zero displacement after quantization. Reject missing metadata only when needed by the requested conversion, frame, relative basis or limit check. Unsupported bases fail before any request is yielded.
- Keep position, velocity and acceleration preferences independent, including steps/s2, deg/s2, rad/s2 and rpm/s. Native device ramp-time encoding still requires later verified profile semantics.
- Introduce host-only configuration/origin operations only with their required stationary/reference evidence. Invalidate dependent knowledge when scales, polarity, origin or decoding assumptions change.
- Expose implemented host configuration/preparation through the existing console with strict exact-number parsing; no device settings change implicitly.

## Verify

Native tests cover signed integer extremes, factor cancellation, origin overflow, gear/lead requirements, linear encoder sources, ties-to-even, all rounding modes, NaN/infinity, tiny/huge values and stale references. Compare direct API and CLI effective targets. Hardware work is a short read-only regression if the example changes; free-shaft rotation cannot qualify linear machine travel.

Verify native relative requests do not require an unrelated host origin, gear ratio or linear lead when their basis and applicable limits need none of those.

## Subagents and handoff

Assign a numerical-boundary reviewer and an API/CLI semantic reviewer. Re-audit for duplicated conversions and needless type machinery. Deliver prepared-target/error contracts and configuration generation rules to 08–10; follow the common evidence and commit/sync workflow.
