# ESS release prompt preparation and source audit

Date: 2026-10-03. MotorControl-RS baseline: `4ae0c4d` (0.6.0).
FieldCore reference HEAD: `f3facd993a6896ca02f2c65f5be745cf1a171403`.
FieldCore had unrelated in-progress changes; it was inspected read-only.
Source paths below describe the inspected tree, not a permanent compatibility
guarantee. Recheck relevant definitions, callers and tests at each dispatch.

## Delivered scope

The [prompt index](../prompts/ess_release/README.md) contains 30 ordered blocks
covering the remaining [roadmap](../roadmap.md). Each block names prerequisites,
existing code to reuse, implementation boundaries, failure tests, hardware
disposition and two subagent roles. The shared
[execution contract](../prompts/ess_release/execution_contract.md) requires an
independent final audit, fixes, focused rechecks, evidence and commit/push.
The [coverage map](../prompts/ess_release/coverage.md) assigns the common motion,
native ESS, platform and release requirements to concrete blocks.

Preparation executes none of the numbered prompts. The library still has its
existing units, register catalogue, codecs, runner and read-only probe/load
foundation; bus ownership and typed motor operations remain to be implemented.

The structure follows FieldCore's
[settings-transfer prompt set](../../../FieldCore-node/docs/prompts/settings_transfer/README.md)
and its
[execution contract](../../../FieldCore-node/docs/prompts/settings_transfer/execution_contract.md):
serial dispatch, shared rules, verified predecessor APIs, independent review
and evidence handoffs. FieldCore-specific verification scripts, product
workflows and queue/task architecture were not copied.

## Actual source audit

This audit checked source to choose compatible contracts, not merely the
wording of FieldCore's prompts. It is not an exhaustive correctness audit of
either firmware. FieldCore requires later integration work; matching names
alone would not provide compatibility.

| Inspected source | Finding and consequence for this sequence |
| --- | --- |
| MotorControl-RS [Runner](../../examples/common/RtuRunner.cpp) and [runner contract](../runner.md) | Reuse physical TX settlement, interval/watermark evidence, final framing gap and explicit recovery. No new transaction engine or guessed timestamps. A retained host ID cannot distinguish every bit-identical delayed RTU reply. |
| [E2 adapter](../../examples/common/Esp32S3Uart.cpp), [console application](../../examples/probe_cli/main.cpp) and [console](../../examples/probe_cli/ProbeConsole.cpp) | Extend real call paths and tests. Keep host load controls local, bounded console work responsive and one UART owner. Capture cost and electrical qualification remain open. |
| [Units](../../src/Units.cpp), [codec](../../include/MotorControlRS/profiles/ess_rs/Codec.h) and [ledger](../reference/ess_rs_registers.json) | Reuse checked conversions, request expectations and generated access policy. Generated Types.h is not a home for handwritten operations. Unknown semantics and unreviewed paired writes cannot become setters by assumption. |
| [CMake](../../CMakeLists.txt) and [PlatformIO](../../platformio.ini) | Existing tests/package rules are the starting point. Python CTest registration is conditional; prompt verification must explicitly run required Python suites. Native IDF component registration is not a standalone IDF test application. |
| FieldCore [transaction contract](../../../FieldCore-node/include/TunnelMonitor/rs485/Rs485OwnerTransaction.h) | TX is eight bytes; format support and observation shape are limited. Reviewed ESS FC10 frames need greater capacity, and captured RX intervals/physical DE evidence need explicit integration. Preserve other devices' framing modes in that later change. |
| FieldCore [task](../../../FieldCore-node/src/rs485/Rs485Task.cpp) | Admission reserves an outstanding/result slot; per-device work and round-robin progress are bounded. Reuse reservation and exact identity ideas. Preserve one absolute request deadline through waits. Its request-prefix echo stripping cannot be copied for genuine FC06 echo acknowledgements. |
| FieldCore [runtime ingress](../../../FieldCore-node/src/rs485/Rs485RuntimeIngress.h) | RTOS ingress and owner admission have separate roles there. This cooperative reference needs one owner context; do not reproduce two queue layers or imply arbitrary-thread safety. |
| FieldCore [device measurement contract](../../../FieldCore-node/include/TunnelMonitor/contracts/DeviceMeasurement.h) | Probe/measure/read-last requests and float measurement payloads do not express typed motion intent, acknowledgement, completion or execution uncertainty. Administrative cancellation is not motor stop. |
| FieldCore [device binding](../../../FieldCore-node/include/TunnelMonitor/rs485/Rs485DeviceBinding.h) and [SHZK module](../../../FieldCore-node/src/rs485/ShzkDeviceModule.cpp) | Passive modules yield work to the owner. Keep this useful separation, but exclude sensor retry/backoff policies from uncertain motor writes. |
| FieldCore [Arduino backend](../../../FieldCore-node/src/rs485/ArduinoRs485Backend.cpp) | Batch reads and host flush calls alone do not supply MotorControl-RS's physical TX and RX timing evidence. The adapter needs qualification, not a wrapper that asserts timing guarantees. |
| FieldCore [diagnostics](../../../FieldCore-node/src/rs485/Rs485Diagnostics.cpp) and [CLI](../../../FieldCore-node/src/cli/owners/Rs485Cli.cpp) | The worker polls at 5 ms; copied diagnostic snapshots are passive. Larger runtime buffers use PSRAM while required task/control storage stays internal. Preserve these ownership principles and measure actual capture/service budgets. |
| FieldCore [CO2 board](../../../FieldCore-node/include/TunnelMonitor/board/co2control/Co2ControlS3Hw200Board.h) and [composition](../../../FieldCore-node/src/product/co2control/Co2ControlRuntimeComposition.cpp) | Physical E2 board naming is distinct from FieldCore's E2 bus abstraction. GPIO21/47/48 appear as inactive board pins in the inspected profile; the selected CO2 composition is not an already integrated motor UART owner. Keep the user-confirmed standalone wiring and later product integration separate. |

## Corrections made during independent review

Three reviewers checked transport/platform scope, motion/native coverage, and
FieldCore conventions. The lead inspected their findings and updated the set:

1. **Deadline and ownership precision:** 01/02 retain one absolute admitted
   request deadline, separate response timing from queue residence, and test
   expiry during TX/deferred work. Qualified frame closure includes the final
   idle gap; late application servicing is not itself a late physical frame.
   Owner APIs are cooperative unless tested synchronized ingress is needed.
2. **RTU limits:** 02 now records that bit-identical late responses can remain
   indistinguishable after recovery. No finite guard or host request ID is
   presented as a protocol guarantee.
3. **Console scope:** 03 routes motor transactions through the owner while
   preserving local load configuration, cached health/status and host controls.
4. **Units dependencies:** 07 only requires metadata needed by the requested
   conversion, frame, basis or limits. Native relative motion does not acquire
   an unrelated origin, gear or lead dependency.
5. **Ordered feature dependencies:** 14 uses already verified input configuration;
   15 revisits homing methods needing new assignments. Save-dependent
   communication activation from 20 is revisited with explicit persistence in
   21. No duplicate I/O setter or hidden save operation bridges the gap.
6. **Honest cleanup and qualification:** the shared contract requires bounded
   stop cleanup and retained unknown outcomes on link loss. Deliberate link-loss
   tests during continuous motion require an independent bench stop mechanism.
   Prompt 26 requires timing evidence applicable to each platform image.
7. **Verification and packaging:** required Python suites have explicit commands.
   Prompt 27 separates a clean repository verifier run from independent core
   source/install consumer builds; core packages need no private test scripts.
8. **Stale documentation:** the profile/CLI contracts now distinguish existing
   codecs/probe/load behavior from planned typed coverage. The profile file map
   distinguishes generated enums from future handwritten types. The backlog
   records the latest 0.6.0 bench image, and the capture CPU range typo is fixed.

Focused independent re-review accepted the motion and transport corrections;
the final idle-gap deadline clarification follows the existing runner contract
and was added to both the requirement and boundary tests.

## Preparation checks and remaining work

Preparation checks cover contiguous numbering, unique indexed entries, required
prompt sections, relative Markdown targets, source-reference paths and diff
whitespace. A repository-local Python check passed all 30 numbered entries and
258 local Markdown targets across 42 files; numbered prompts are 272 to 459
words each, plus the shared contract. Full implementation tests and hardware checks belong to dispatched
work; no firmware or motor settings were changed during preparation.

Unresolved model `0x4EEA`, register semantics, capture/cache behavior, electrical
measurements and missing physical fixtures remain visible in the roadmap and
coverage map. Their status did not change merely because a prompt now owns
them. Actual source APIs and evidence must be checked again before dispatch.

The next implementation block is
[01 — bounded admission and retained results](../prompts/ess_release/01_bus_admission_and_results.md),
using the existing runner and native fakes. It does not authorize running all
30 blocks in one turn or making changes in FieldCore.
