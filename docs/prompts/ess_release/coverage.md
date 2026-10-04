# Roadmap and requirement coverage

Prompt19 [host tuple handoff](../../reports/ess_release_19_2026-10-04.md)
maps the application callback, adapter and exclusive owner lease to native and
host-only hardware evidence. Sixteen setups and two mismatch/restore checks
PASS; alternate motor communication and electrical qualification remain open.
This adds no register-ledger write credit: device settings are unchanged.

The [fresh16/17 audit](../../reports/ess_release_16_17_audit_2026-10-04.md)
maps both complete prompts to actual public APIs, owner/CLI paths, tests and
separate bench dispositions. All48 indexed records now have checked read
evidence; late response certainty/help parity are corrected. Guarded wire forms,
shared-start failure and current/physical qualification stay open.

Prompt15 [I/O handoff](../../reports/ess_release_15_2026-10-04.md) delivers all
resolved typed assignments, explicit function-zero disable and checked masks
through the existing settings engine. Selected stored-register/readback/restore
bench evidence is separate from absent switch/load/electrical qualification;
custom2 and crossmapped actuation remain unavailable.

Prompt13 [implementation evidence](../../reports/ess_release_13_2026-10-04.md)
records seven typed single-word settings and limit-pair reads. Unreviewed pair
setters remain UNSUPPORTED; physical writes/restore and limit movement NOT RUN.
The ledger-linked inventory distinguishes settings enum WRITE choices from
motor ACTION choices and keeps native/hardware dispositions independent.
The [fresh audit](../../reports/ess_release_13_audit_2026-10-04.md) verifies
confirmed readback settlement and shared, configured-axis prerequisite caches;
it does not add a pair-write window or physical settings qualification.


This map assigns work to prompts; it is **not implementation evidence**.
Use the [index](README.md) for order/status and the named prompt's handoff for
actual code/tests/hardware proof. Existing units, catalogue, codecs, runner and
capture stay the starting point. The source register ledger remains the single
register transcription; prompt 05 extends operational coverage linked to it.

## Work owners

| Roadmap/contract obligation | Implementing prompts | Final cross-check |
| --- | --- | --- |
| Queue admission, copied request lifetime, result capacity and exact IDs | 01 | 03, 28 |
| Fairness, deadlines, cancellation, urgent/stop capacity, no automatic replay | 02, 08 | 03, 29 |
| Absolute-deadline runner evidence, recovery queue disposition and retained outcomes | 01–03 | 28–29 |
| Same-axis staging reservation and non-mutating rejected stop admission | 08–09 | 23, 28–29 |
| Standalone RS485 owner, responsive console, retained results and diagnostics | 03 | 23, 29 |
| Observation time versus delayed delivery, cache age and recovery guard | 03, 06 | 24, 28 |
| TX/RX/DE observations, interval/watermark truth, echo and cache policy | Existing runner/adapter; 04 | 26, 29 |
| Short/long normal/exception frames and each supported write shape | 01, 04; typed feature prompts | 29 |
| CPU, service gap, latency, internal RAM, PSRAM and stack budgets | 03–04 and every resource-changing block | 26, 29 |
| Profile/model/firmware capabilities and four separate coverage dimensions | 05; maintained by every feature prompt | 23, 28, 30 |
| Typed identity, version, address/DIP and configuration readback | 05 | 22–24 |
| Alarms/readiness, actual vs commanded state, unknown bits, field freshness | 06 | 23–24, 29 |
| Common steps/fullsteps/counts/turn/deg/rad/travel configuration | Existing Units; 07 | 10–11, 23 |
| Exact input, origins, reference generations, rounding and limits | 07, 10 | 09, 13–14, 28 |
| Enable/release, normal/emergency stop, alarm clear, uncertainty | 08 | 09, 11, 14, 29 |
| Relative/absolute/wrapped-angle movement and actual completion | 09–10 | 24, 29 |
| Velocity, physical acceleration/deceleration vs native ramp time | 11 | 24, 29 |
| Device position clear versus host origin and homing | 10, 14 | 23, 29 |
| Additional paired write forms, signedness/order/access without bypasses | 12 | Every dependent family; 28 |
| Direction, subdivision, word order, soft limits, over-limit/interruption | 13 | 23, 29 |
| Homing methods/options, rates, offsets, collision prerequisites | 14, related collision fields in 18 | 23, 29 |
| Optional X0–X3/Y0/Y1: unwired/disabled disposition, functions, polarity and custom outputs | 05–06, 15 | 08–09 admission; 23, fixture-dependent 29 |
| All stored position records, reserved words and external mode | 16 | 23, fixture-dependent 29 |
| All stored speed records, PV policy and shared PT/PV starting speeds | 16 | 23, fixture-dependent 29 |
| Algorithm/open-loop, encoder configuration, current and lock settings | 17 | 23, 29 |
| Filters, deviation/arrival criteria, current-loop/LA tuning, collision fields | 18 | 23, 29 |
| Host baud/format capability, admission/exclusivity, restore | 19 | 20, 22, 26 |
| Device address/baud/format commissioning, active vs pending semantics | 20 | 21–22, 29 |
| Explicit save/factory restore and field persistence uncertainty | 21 | 23, controlled cases in 29 |
| Minimal non-changing probe, identity confidence, bounded discovery | Existing probe; 05, 22 | 24, 29 |
| Full public API/CLI inventory, strict parsing, passive status/health | Each feature; 23 | 24, 26, 28 |
| Finite Python feature/stress/state checks and truthful evidence | Each feature; 24 | 26, 29 |
| Arduino and real native ESP-IDF example parity; S2 portability disposition | 25–26 | 27, 30 |
| Repeatable verification, CI, header isolation, clean package consumption | Existing CMake; 27 | 28, 30 |
| Platform-independent core/events; C++11 and C++17 without RTTI consumers | 05, 08, 27 | 30 |
| Future RS485 integration: preserve existing framing, tuple switching and request context | 30 (read-only handoff) | Separate later FieldCore integration |
| Whole-code audit, simplification, duplication/stale code removal | Each prompt; 28 | Fix review in 29–30 |
| Fault/endurance, independent physical motion/stop, reconnect/restart | Each feature; 29 | 30 |
| Supported release scope, exact evidence and later FieldCore adaptation map | 30 | Explicit candidate/release disposition |

## Gaps must remain visible

- All documented ESS native read/write/action obligations remain in coverage,
  including indexed records, meaningful bitfields and unsupported enum values.
  A raw codec, generic write command or one tested family does not complete it.
- Unknown model `0x4EEA`, unresolved signed encodings/ramp units, paired-write
  applicability, homing-offset order, collision-address conflicts and RW/RW-S
  application/persistence rules require evidence. Neither numeric defaults nor
  another manufacturer's behavior resolve them automatically.
- Qualification without a required sensor, output load, index, mechanical
  axis, power-control fixture or analyzer is NOT RUN. Pure conversions and
  native injections can still pass as software tests. Record external-input-only
  capabilities rather than inventing a serial command or missing hardware.
- Serial-only operation must not depend on external switches or loads. Mark
  unsupported disabled assignments and operation-specific input requirements
  explicitly; an unwired input is not automatically disabled. Physical checks
  requiring external fixtures remain separate from serial-motion coverage.
- Additional manufacturer implementations, CANopen, coordinated trajectories, speculative
  blending/streamed modes and edits to FieldCore are outside this sequence.
  Preserve explicit unsupported behavior for common optional operations.
- Firmware build success, device acknowledgement, completed motion, unchanged
  persistent settings and a healthy application are separate claims.

At 23 and 28 reconcile this map against every normative section of the current
contracts and the linked operation ledger. If a requirement has no owner or
evidence, add its real task/dependency before declaring coverage complete.
