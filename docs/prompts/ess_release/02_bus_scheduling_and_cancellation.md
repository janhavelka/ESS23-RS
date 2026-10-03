# 02 — Fair scheduling, cancellation and urgent work

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: 01's admission, completion reservation and parser settlement are verified in current code.

## Read and reuse

Read `docs/axis_contract.md` on stop/sequencing and the owner/runner delivered by 01. Compare FieldCore's cancellation and recovery paths; sensor cancellation is not a motor-stop implementation.

## Implement

- Extend that owner with bounded producer fairness and explicit dispatch deadlines. Preserve FIFO order where promised; distinguish queue waiting from response deadlines.
- Preserve 01's absolute request deadline across dispatch, deferred work, cancellation and recovery. On expiry during TX, settle physical transmission and retain the correct uncertain outcome; never extend the deadline or replay the request to obtain success.
- Provide a small reserved admission path for urgent work that ordinary queue/result pressure cannot silently consume. It prepares future stop delivery; this step does not implement an ESS stop command.
- Urgent work gets the next permitted bus opportunity after the in-flight frame and required recovery settle. Document finite capacity, urgent-full behavior and the conditions under which latency is bounded. Do not promise ordinary fairness under an unbounded urgent flood.
- Cancel an unsent request without TX and retain one cancellation result. Active cancellation settles the runner and retains possibly executed/unknown status; it never truncates physical TX.
- Prevent stale sequence continuations from dispatching after cancellation/configuration change. A wait between operation steps releases the bus for eligible work.
- Keep recovery explicit, discard stale buffered traffic and validate complete request expectations. Modbus RTU has no wire request ID: a bit-identical delayed response after recovery may remain indistinguishable from a fresh reply. No finite idle guard or host ID removes that limitation. Priority cannot bypass a fault or clear an uncertain operation.
- Define recovery's queue/generation policy: refuse recovery until pending work is resolved, or terminally cancel it with reserved results. Preserve unread and uncertain outcomes; invalidate old continuations and never implicitly resume queued writes after recovery. Retain a distinct bounded recovery outcome even when ordinary result storage is full. Physical TX settlement still governs when recovery can occur.
- Derive a conservative urgent latency bound from current transaction/deadline/service budgets and record conditions that make physical stop delivery impossible.

## Verify

Test two or more fake producers, saturated ordinary/result storage, urgent reservation exhaustion, expiry while queued, during active TX and deferred work, cancel in every runner phase, repeated cancellation, stale continuation, fairness, delayed service and late replies. A last byte before the deadline with the final framing gap after it must fail; qualified frame closure before the deadline serviced later may succeed. Verify no deadline renewal, automatic retry or altered retained outcomes through the actual owner. A stop-shaped fake request proves scheduling only.

Include recovery with active, queued and unread terminal work, repeated/failed recovery and old-generation continuation. Compete ESS requests with a synthetic non-motor RTU producer using its own validator; prove fairness and result isolation without importing another driver or implementing ASCII framing here.

## Subagents and handoff

Assign a scheduling/liveness reviewer and a cancellation/uncertainty test reviewer. Audit for duplicated queues and overlapping ownership flags. Deliver scheduling and cancellation contracts plus tests to 03; complete the common audit, report and commit/sync steps.
