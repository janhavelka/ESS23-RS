# ESS homing methods and prerequisites

This table records the 35 documented choices in the preserved
[ESS function manual](vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf).
Physical PDF pages 19–20, 32–67 and 72–73 were visually inspected for prompt 14.
The [register ledger](reference/ess_rs_registers.json) remains the source of
generated enums and catalogue descriptors. A listed enum is not an executable
capability. No methods 0, 15, 16, 31 or 32 are documented.

`H` and `L` mean the diagram's high search and low return/query rates, rather
than measured speeds. `+` and `−` are the manual's forward/reverse directions;
their physical shaft interpretation must be qualified with the actual driver
direction setting. Rising/falling below mean effective signal transitions,
after the configured terminal polarity. An active initial sensor often changes
the initial direction and omits the high-speed search.

| Method | Required reference / external signals | Initial inactive / active sensor path | Final reference approach | Physical PDF pages |
| --- | --- | --- | --- | --- |
| −4 | Collision mechanics; unresolved | Unresolved | Unresolved | 72–73 |
| −3 | Collision mechanics; unresolved | Unresolved | Unresolved | 72–73 |
| −2 | Collision mechanics; unresolved | Unresolved | Unresolved | 72–73 |
| −1 | Collision mechanics; unresolved | Unresolved | Unresolved | 72–73 |
| 1 | Internal Z + negative limit | −H / +L | +L, first Z after negative-limit falling | 32 |
| 2 | Internal Z + positive limit | +H / −L | −L, first Z after positive-limit falling | 33 |
| 3 | Internal Z + origin | +H / −L | −L, first Z after origin falling | 34 |
| 4 | Internal Z + origin | +L / −H then +L | +L, first Z after origin rising | 34–35 |
| 5 | Internal Z + origin | −H / +L | +L, first Z after origin falling | 35–36 |
| 6 | Internal Z + origin | −L / +H then −L | −L, first Z after origin rising | 36–37 |
| 7 | Internal Z + origin + positive limit | +H / −L | −L, first Z after origin falling | 37–39 |
| 8 | Internal Z + origin + positive limit | +H / −L then +L | +L, first Z after origin rising | 39–40 |
| 9 | Internal Z + origin + positive limit | +H / +L then −L | −L, first Z after origin rising | 40–42 |
| 10 | Internal Z + origin + positive limit | +H / +L | +L, first Z after origin falling | 42–43 |
| 11 | Internal Z + origin + negative limit | −H / +L | +L, first Z after origin falling | 44–45 |
| 12 | Internal Z + origin + negative limit | −H / +L then −L | −L, first Z after origin rising | 45–47 |
| 13 | Internal Z + origin + negative limit | −H / −L then +L | +L, first Z after origin rising | 47–49 |
| 14 | Internal Z + origin + negative limit | −H / −L | −L, first Z after origin falling | 49–50 |
| 17 | Negative limit | −H / +L | +L, negative-limit falling | 50–51 |
| 18 | Positive limit; conflicting figure labels | +H / −L | −L, positive-limit falling in prose | 51 |
| 19 | Origin | +H / −L | −L, origin falling | 52 |
| 20 | Origin | +L / −H then +L | +L, origin rising | 52–53 |
| 21 | Origin | −H / +L | +L, origin falling | 53–54 |
| 22 | Origin | −L / +H then −L | −L, origin rising | 54–55 |
| 23 | Origin + positive limit | +H / −L | −L, origin falling | 55–57 |
| 24 | Origin + positive limit | +H / −L then +L | +L, origin rising | 57–58 |
| 25 | Origin + positive limit | +H / +L then −L | −L, origin rising | 58–60 |
| 26 | Origin + positive limit | +H / +L | +L, origin falling | 60–61 |
| 27 | Origin + negative limit | −H / +L | +L, origin falling | 61–63 |
| 28 | Origin + negative limit | −H / +L then −L | −L, origin rising | 63–64 |
| 29 | Origin + negative limit | −H / −L then +L | +L, origin rising | 64–66 |
| 30 | Origin + negative limit | −H / −L | −L, origin falling | 66–67 |
| 33 | Qualified internal Z; no external input | −L; no slowdown input | First Z in negative direction | 67 |
| 34 | Qualified internal Z; no external input | +L; no slowdown input | First Z in positive direction | 67 |
| 35 | Current position; no sensor | No sensor-search trajectory | Current position becomes mechanical origin | 67 |

For 7–14 and 23–30, the table covers every documented starting-location branch:
if the origin is initially inactive on the far side, the indicated limit causes
a high-speed reversal before origin acquisition. It is therefore part of the
method's prerequisites, even if a favourable starting point can reach the
origin without encountering it. The original diagrams define intermediate
decelerations/reversals; the compact table does not replace those paths.
Several `c)` captions incorrectly say the origin is inactive while depicting
it active. Method 18 labels the figures “negative limit” despite a positive
heading/prose; that discrepancy remains unresolved.

The implemented subset is 33, 34 and 35, with zero offset and auxiliary value 7.
Methods requiring external switches remain unimplemented until their complete
edge/return sequencing and verified input prerequisites are provided. Method 18
and the four collision methods report unresolved source semantics separately.
Unwired or disabled optional external inputs do not exclude 33–35. Internal Z
is a motor capability requiring qualification, not an external input terminal.
Software implementation establishes no physical homing qualification.

## Parameters, auxiliary policy and unresolved fields

The p19/p20 setup uses method `0x0031`, search rate `0x0032`, return/query rate
`0x0033`, shared acceleration/deceleration time `0x0034` and offset pair
`0x0035–0x0036`. The serial trigger is `0x0027` bit 4 (`0x0010`); external
homing enable is input function 11 and is unnecessary for a serial trigger.

| Field | Documented range | Remaining interpretation |
| --- | --- | --- |
| Search `0x0032` | 5–3000 r/min, p72 | Default 120 is annotated 60 r/min; p20 writes 60 for 60 r/min. Physical encoding remains unresolved. |
| Return `0x0033` | 5–300 r/min, p73 | Default 60 is annotated 60 r/min; p20 writes 30 for 30 r/min. Reuse only qualified native words. |
| Ramp `0x0034` | 30–2000 ms, p73 | Default 50 is annotated 100 ms; p20 writes 100 for 100 ms. Physical encoding remains unresolved. |
| Offset `0x0035/2` | `−0xFFFFFFF` to `0xFFFFFFF`, p73 | Word-order applicability, negative encoding and native scale remain unresolved. |

Rates/ramp are explicit native register words bound to caller qualification;
these ranges do not establish a physical conversion or permission to assume
the annotated defaults. The only reviewed complete homing write is FC10
`0x0031/6`. No `0x0035/2` or split FC06 offset write is permitted. Zero in both
offset words is invariant under order/sign/scale; every nonzero offset fails
before staging. See the [pair disposition](ess_pair_writes.md).

Auxiliary `0x0030` is a choice 0–11, not independently combinable bit flags:

| Values | Physical offset motion | Resulting coordinate in order |
| --- | --- | --- |
| 0–5 | Run offset | Offset; zero; negative offset; actual; actual + offset; actual − offset |
| 6–11 | Keep position | Offset; zero; negative offset; actual; actual + offset; actual − offset |

The implemented value 7 keeps position after reference acquisition and requests
coordinate zero. It does not remove the index-search movement of 33/34. An
acknowledged write or an old homed flag establishes no new reference. Require
correlated, fresh post-trigger completion evidence and checked zero feedback;
missed completion transitions, stop, alarm, cancellation and uncertainty leave
the new reference unavailable. Device homing, explicit device-position clear
and host-origin setting remain separate actions.

Collision modes −1 through −4 appear only in the ESS appendix. Their direction,
trajectory and negative wire encoding are not defined by the positive-method
diagrams. P73's `0x003B/0x003C` lack access notation; p79's `0x0122/0x0123`
have RW/S and a conflicting threshold minimum. They remain distinct registers,
without aliases, writable permission or an invented collision procedure.

## Prompt 15 admission and fixture handoff

Prompt 15 must reuse its typed terminal setters, then bind fresh X0–X3
function/polarity readback and declared wiring to the exact target and
configuration generation. Origin requires function 1, positive limit function
2 and negative limit function 3. Function 0 disables a function; unconnected
wiring alone does not. No method preparation silently changes an assignment,
polarity, stop or limit function.

Before admitting a switch-dependent method, prove the effective sensor levels,
initial starting-location branch, required rising/falling edges, return path,
shaft direction and bounded mechanical travel. Test initially active and
inactive sensors, both sides of the origin for limit-assisted methods, stuck
or absent transitions, alarm/stop preemption and loss of acknowledgement.
Methods using Z additionally require the closed-loop index capability and
its direction relationship. Independent stop and external observations must
be available for physical fixture tests; a missing switch or measurement is
NOT RUN, not simulated physical proof. Current free-shaft availability alone
provides none of the external-switch or collision fixtures.

The current FieldCore RS485 sources were checked read-only: its sensor modules
yield bounded transactions/waits and accept owner observations. This library
keeps that workflow, its own larger requests, explicit write uncertainty and
checked response provenance. FieldCore's eight-byte TX capacity, request-prefix
echo removal and sensor retry/measurement policy do not qualify motor writes
or carry the 21-byte homing request. No FieldCore types or edits are introduced.
