# Roadmap and requirement coverage

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
| Real E2 owner, responsive console, retained results and diagnostics | 03 | 23, 29 |
| TX/RX/DE observations, interval/watermark truth, echo and cache policy | Existing runner/adapter; 04 | 26, 29 |
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
| X0–X3/Y0/Y1 functions, polarity, actual state and custom outputs | 06, 15 | 23, fixture-dependent 29 |
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
- Additional manufacturers, CANopen, coordinated trajectories, speculative
  blending/streamed modes and edits to FieldCore are outside this sequence.
  Preserve explicit unsupported behavior for common optional operations.
- Firmware build success, device acknowledgement, completed motion, unchanged
  persistent settings and a healthy application are separate claims.

At 23 and 28 reconcile this map against every normative section of the current
contracts and the linked operation ledger. If a requirement has no owner or
evidence, add its real task/dependency before declaring coverage complete.
