# 10 — Absolute positions, wrapped angles and coordinate changes

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 07 exact preparation and 09's real finite-motion path; preserve 08 stop behavior. Implement software cases independently of unavailable linear mechanics.

## Read and reuse

Read axis position/angle/origin sections, CLI grammar and current operation/state implementations. Reuse the native mover, conversion/quantization helpers and configuration generations.

## Implement

- Add absolute position and wrapped-angle preparations with explicit frame and fresh reference. Preserve multi-turn absolute targets; 720 degrees is two turns, not an orientation normalized to zero.
- Resolve positive/negative/shortest angular paths, half-turn ties, same-angle behavior and limits without silently selecting another revolution or direction.
- Expose relative bases only when supported and observable; never convert a relative request using stale actual/commanded/queued-endpoint knowledge.
- Complete host origin updates with no motor traffic. Implement ESS device-position clear as a separate explicit native action only with resolved documented semantics; reject arbitrary nonzero counter writes if ESS only supports zero.
- Invalidate origins/reference confidence after release/external movement, uncertain device clear, lost position knowledge or relevant settings changes according to actual evidence.
- Give steps, fullsteps, turns, degrees, radians and configured travel the same public preparation path and CLI reachability.

## Verify

Test multi-turn and wrap boundaries, direction/tie policies, stale position, generation changes, target/rounded limits, zero-after-quantization, lost clear acknowledgement and no traffic on invalid requests. On COM13 run small equivalent step/degree/radian moves and origin checks using qualified settings. Record physical comparison separately from encoder-derived observations; linear travel remains unqualified without a real configured mechanism.

## Subagents and handoff

Assign a coordinate/path reviewer and a CLI/direct-API parity reviewer. Audit reference invalidation and remove duplicated target math. Deliver completed coordinate behaviors and explicit gaps to 11 under the common review, report and commit/sync workflow.
