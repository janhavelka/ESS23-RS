# 06 — Typed state, feedback and separate health observations

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 05's exact target/configuration context, real typed-read machinery and coverage inventory.

## Read and reuse

Read the axis/profile/CLI contracts, original ESS status/alarm tables and `docs/reference/09_timing_and_gap_audit.md`. Reuse typed read planning, codecs, owner cache and Python console support from earlier steps.

## Implement

- Decode reviewed alarms, motion flags, input/output state, speed and paired position. Preserve raw/unknown bits and retain an explicit reason when units or signed encoding cannot be established.
- Check the ESS enabled/released polarity in the original table. Treat subdivision-equivalent position feedback separately from raw encoder counts and commanded position.
- Record validity, source, configuration generation, last attempt, last success and age per observation block. Failed refreshes retain previous valid values; multiple reads are not an atomic motor snapshot.
- Reuse 03's separate observation/delivery timestamps. Bound observation age conservatively from available transaction evidence; do not claim an exact drive sample time absent documentation. Delayed owner service and repeated cached queries must not refresh feedback age.
- Keep communication freshness, drive alarm, readiness and active-operation outcome separate. A successful identity read does not refresh motion state or resolve an uncertain write.
- Add `read state`, cached `status`/`health`, explicit `health check` and bounded opt-in observation polling through shared APIs. Consuming fields require an explicit owner and cannot enter generic polling accidentally.
- Extend Python state/health checks now; later harness consolidation must reuse them.

## Verify

Test stale/mismatched generations, unknown alarms, partial refresh, absent feedback, separate field ages, disabled monitoring, poll cancellation and poll scheduling behind urgent work. On COM13 establish a stationary raw/decoded baseline with repeated checks, confirming no motion or settings changes. Record unsupported actual/commanded distinctions instead of fabricating state.

## Subagents and handoff

Assign a source/bitfield reviewer and a health/cache test reviewer. Audit observation freshness and passive cache reads through the actual API and application. Deliver state evidence and completion-observation limitations for 07–09; apply the common audit and commit/sync workflow.
