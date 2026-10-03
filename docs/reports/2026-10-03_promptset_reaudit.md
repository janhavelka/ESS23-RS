# Prompt set re-audit against implementation

Date: 2026-10-03. Audited MotorControl-RS `ea1e402` and the prepared 30-prompt
sequence. FieldCore HEAD remained `f3facd993a6896ca02f2c65f5be745cf1a171403`,
with unrelated work in progress. FieldCore was inspected read-only; none of its
files, builds or hardware interfaces was changed.

This pass corrects instructions and acceptance criteria. It does not execute
the numbered implementation prompts or claim to fix runtime behavior already
present. Source limitations below have explicit owning prompts and backlog rows.

## Findings and corrections

| Finding from actual source | Correction and implementation owner |
| --- | --- |
| [Runner Request](../../examples/common/RtuRunner.h) has a response timeout from physical TX completion, not an absolute deadline from admission. Reducing it at dispatch cannot account for subsequent bus wait/setup/TX time. | 01 requires the small runner deadline/evidence extension needed to enforce the overall deadline before new physical actions, while retaining on-time captured replies. 01/02 test expiry at each boundary and ambiguous closure intervals. |
| [Runner completion](../../examples/common/RtuRunner.cpp) uses the frame closure upper bound for a successful `FRAME` result's `endedUs`; many failure paths use application service time. [Deferred evidence tests](../../test/runner_test.cpp) already preserve successful closure at 3560 us when serviced at 20000 us. | 01 must expose/document sufficient timing bounds rather than treating all terminal timestamps identically, parsing an optional trace, or cancelling before inspecting captured completion. This finding does not describe `endedUs` as universally a poll timestamp. |
| [Current recovery](../../examples/probe_cli/main.cpp) has one active request. Adding a queue without defining recovery's effect can resume stale writes. FieldCore's [owner](../../../FieldCore-node/src/rs485/Rs485Task.cpp) explicitly interrupts outstanding work and invalidates module state during recovery. | 02 defines queue disposition, generation invalidation and bounded recovery results; 03 exercises the real CLI path. Preserve unread and uncertain terminals and never implicitly resume old writes. FieldCore's force-direction cleanup is not imported: physical TX settlement remains mandatory here. |
| Stateless [ESS codecs](../../include/MotorControlRS/profiles/ess_rs/Codec.h) and [units](../../include/MotorControlRS/Units.h) do not protect a staged motion sequence. A transaction queue alone permits another producer to overwrite target/speed before the first trigger. | 08 adds an application-owned same-axis operation reservation across the sequence, following the existing axis/profile contracts. 09 injects a competing write between staging acknowledgement and trigger. Compatible reads and other targets remain eligible. |
| 08 previously superseded continuations when stop was requested, before explicitly reserving its resources. A later unsupported/full rejection could unintentionally cancel active work. Generic move-readiness checks could also block stop. | Validate stop semantics and reserve urgent/result capacity before superseding anything. Rejected admission leaves active work intact. Stop uses its own prerequisites, including tests with stale feedback, alarms, missing unrelated origins/scales and unknown prior execution. |
| New typed operation/event APIs do not exist yet. Existing `MotorControlRSExample::Rtu` types are application helpers outside the installed library. | 05/08 explicitly keep public handwritten types in the core and map transport evidence in the application. Test header isolation and distinguish invalid event envelopes from valid correlated failure events. |
| [Application snapshot/completion](../../examples/probe_cli/main.cpp) computes age from `finishedUs`, set when the owner handles completion. Delayed captured feedback can appear newly observed. The same timestamp also guards recovery. | 03 separates observation, delivery and recovery-settlement time; 06 applies that to feedback caches. Add delayed-owner and repeated-cached-query tests. The current runtime limitation remains open until that implementation; simply repurposing the timestamp could break recovery. |
| The runner assumes Modbus-style function/exception envelopes. FieldCore's [VibWire module](../../../FieldCore-node/src/rs485/VibWireDeviceModule.cpp) uses ASCII requests and CRLF completion through its [transaction contract](../../../FieldCore-node/include/TunnelMonitor/rs485/Rs485OwnerTransaction.h). | 01/03 now describe an RTU reference with a validator selected by the admitted request. 02 adds a synthetic competing RTU producer, without adding another driver. 30 preserves FieldCore's existing ASCII path and extends its owner rather than replacing it with this runner. |
| FieldCore's [startTransport](../../../FieldCore-node/src/rs485/Rs485Task.cpp) chooses baud per transaction. A transient UART change is different from changing a motor's logical endpoint or configuration. | 19/30 separate intended per-request tuple, active UART tuple and logical configuration generation. Mixed-device servicing must not invalidate prepared motor work merely because another device used a different baud. |
| Existing [capture tests](../../test/capture_service_test.cpp) and [hardware evidence](2026-10-03_capture_load.md) focus on seven-byte model replies. Passing these does not qualify longer frames. | 01 explicitly tests 7/37-byte FC03 replies, five-byte exceptions, all reviewed FC10 shapes and bounded buffers. 04/29 own the available physical frame-size envelope; write checks wait for typed operations and missing fixtures stay open. |
| Core [CMake](../../CMakeLists.txt) promises C++11. FieldCore [profile flags](../../../FieldCore-node/platformio.profiles.generated.ini) use C++17 and its [flag script](../../../FieldCore-node/scripts/apply_cxx_flags.py) disables RTTI. | No present incompatibility was found. 27 makes both consumer checks concrete, and 30 records compatibility without copying FieldCore types or C++17-only public declarations. |

The existing FieldCore
[owner tests](../../../FieldCore-node/test/native/test_owner_rs485/test_rs485_task_fake.cpp)
were also inspected for recovery/result expectations. FieldCore's useful
patterns are bounded admission, reserved results, explicit request identity,
passive diagnostics and one transport owner. Its device framing, task topology,
retry policy and timestamp guarantees must be evaluated independently.

## Audit method and verification

Three independent reviewers traced transport/platform behavior, motion/profile
contracts and current FieldCore source. The lead checked the reported source
paths, corrected the prompt owners and coverage map, and requested focused
re-review. All three focused re-reviews passed after correction. No additional
missing ESS register family was identified; unresolved
register semantics still remain explicitly unresolved.

The existing native baseline was configured and rebuilt in Release mode:

```powershell
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

All **13 CTest suites passed**, including the conditional Python generator and
bench-harness suites, which were present in this run. This checks the current
implementation; it does not verify unimplemented queue or motion requirements.
Documentation checks passed: 30 contiguous numbered prompts, unique index
entries, required sections, 198 local links across 39 checked Markdown files
and diff whitespace. The largest numbered prompt is 585 words plus the shared
contract.

No production source, firmware settings or motor behavior changed. No hardware
regression was run for this documentation-only correction. Electrical timing,
motion, long-frame and platform qualification gates remain open as recorded.

The order stays 01 through 30, with no added transport framework or FieldCore
implementation. Start with
[01: bounded admission and retained results](../prompts/ess_release/01_bus_admission_and_results.md)
when implementation is dispatched. Root guidance now requires actual source,
caller/test and relevant read-only FieldCore review on every implementation or
audit prompt.
