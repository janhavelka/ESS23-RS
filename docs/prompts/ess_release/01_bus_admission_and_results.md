# 01 — Bounded bus admission and retained results

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: inspect the current runner/adapter and the baseline report; no earlier prompt implementation is required.

## Read and reuse

Read `docs/runner.md`, `docs/architecture.md`, `docs/reference/10_runner_platform_review.md`, `examples/common/RtuRunner.*` and its native tests. Reinspect FieldCore's actual RS485 request, queue and result owners read-only; record compatible vocabulary and deliberate differences.

## Implement

- Add one small application-owned RTU bus owner under `examples/common/`, using the existing Runner for its supported RTU transactions. Start with FIFO admission and one active transaction; scheduling policy follows in 02. Use the portable callback boundary; no board, product or firmware module types belong in this owner.
- Keep admission, service and result access in one cooperative owner context. Multiple logical producers do not imply thread-safe public calls. Add synchronized ingress only if an actual caller needs it, with bounded storage and explicit tests.
- Give requests bounded storage and immutable request expectations. Copy admitted bytes or enforce a documented lifetime that real callers can satisfy; never retain a caller's stack buffer accidentally.
- Separate pending-request and retained-result capacities. Reserve completion storage at admission, or reject explicitly; never overwrite an unread terminal result.
- Use stable request IDs plus generation/ownership rules. Retain target, expected response, raw result, accepted TX count and uncertainty under the original request context.
- Distinguish admission rejection, queue expiry and transport completion. Return a terminal result exactly once per admitted request; inspecting a retained result is non-consuming until explicit release.
- Retain one immutable absolute request deadline from admission through queue residence and execution. Dispatch and intermediate waits never renew it. Keep response/capture deadlines separate and cap transaction budgets by the remaining request deadline; delayed task servicing must still respect valid evidence of an on-time physical response.
- On-time response means qualified frame closure, including the required final idle gap under the retained timestamp/watermark bounds. Receiving the last byte before the deadline alone is insufficient.
- Implement any small Runner deadline/evidence extension needed by that contract. Its current request has only a timeout relative to physical TX end; reducing that timeout at dispatch alone does not bound queue/setup/TX time. Check the absolute deadline before new physical actions, process captured on-time completion before declaring expiry, and expose the closure bounds needed for an uncertain deadline decision. Do not rely on optional trace storage or treat every `Result::endedUs` as the same kind of timestamp.
- Refuse dispatch while the runner needs recovery. FRAME still requires the admitted request's checked validator before application success; the shipped ESS caller uses its profile parser. Preserve checked device exceptions as terminal rejections, distinct from malformed/foreign frames. Provide a completion-validation handoff that prevents another dispatch before classification.
- Keep construction/I/O separate, storage caller-owned and all scans/copies bounded. No FreeRTOS dependency or generic scheduler framework.

## Verify

Native tests use the real runner: zero/full capacities, transient input buffers, result pressure, release and slot reuse, stale IDs, repeated polls, invalid requests, wrong-address/CRC replies, checked exceptions and recovery interlock. Exercise FC03 one/16-word replies (7/37 bytes), response-identical FC06 acknowledgement and all four reviewed FC10 shapes using fake responders. Cover FIFO/read-budget/storage boundaries. Test expiry during bus wait/setup before TX and closure intervals straddling the absolute deadline. No raw write qualification on the motor in this step.

## Subagents and handoff

Assign one source/ownership reviewer and one failure-test reviewer. Independently audit the final integrated diff. Deliver actual admission/result APIs, lifetime rules, parser settlement contract, memory sizes and focused tests for 02; run the common audit/commit/sync workflow.
