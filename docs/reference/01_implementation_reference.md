# Implementation reference

This is a navigation aid for the ESS-RS profile of MotorControlRS, not a verified
hardware contract. Sources were downloaded on 2026-10-02. All page numbers below
are **physical PDF pages, counted from 1**; printed page numbers in both
manufacturer manuals are two lower. The complete first source transcription is
now in the [register catalogue](05_ess_register_catalog.md); pure conversions and
bench assumptions are in [encoder and units notes](06_encoder_units.md).
Checked raw wire codecs and a minimal probe are implemented; typed commands,
motion preparation and hardware qualification remain future work.
The [timing and gap audit](09_timing_and_gap_audit.md) records the subsequent
full appendix review, RTU timing rules and unresolved device deadlines.

## Source priority

Use the [ESS23-RS hardware manual](../vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf) for connections and switches, and the [Modbus function manual](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf) for commands and the **ESS-RS** register appendix. The [RS20 datasheet](../vendor/ESS23-RS20_Full_Datasheet.pdf) and [RS10 datasheet](../vendor/ESS23-RS10_Full_Datasheet.pdf) each have one drawing/specification page.

The [extracted function-manual text](../pdf-extracted-md/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.md) is searchable. Tables and drawings must be checked in the PDF: extraction can merge columns, lose spacing, or omit figures.

## Function-manual navigation

| Topic | PDF pages | Implementation evidence |
| --- | --- | --- |
| Applicability and protocol overview | 3-4 | Model scope and RTU client/server behavior |
| Physical bus and communication settings | 5-6 | Two-wire RS485; address, baud, serial-format registers |
| Frame format; functions 0x03, 0x06, 0x10 | 6-8 | Request/response layouts; device read limit |
| CRC and device communication errors | 8-12 | CRC order; vendor exception meanings and contradictions |
| Motion command register 0x0027 | 13-14 | Start, mode selection, interrupt, stop bits |
| Position movement | 14-16 | Parameter setup, relative/absolute motion, pulse count |
| Speed movement and jog | 17-18 | Signed speed, acceleration and deceleration |
| Homing configuration | 19-20 | Mode and sensor configuration |
| Multi-position / multi-speed modes | 20-24 | External input triggering and segment parameters |
| Stop / emergency stop | 25 | Examples and deceleration behavior |
| Auxiliary commands 0x002D | 26 | Release, enable, clear alarm/position, restore/save |
| Configurable I/O | 27-28 | Input/output functions and polarity |
| Software limits; fixed-length interruption | 28-29 | Homing prerequisite and input-triggered behavior |
| 32-bit register word order | 29-30 | Configuration register 0x0019 and affected register pairs |
| Homing-method appendix | 32-67 | Individual method diagrams |
| **Appendix 2: ESS-RS register table** | **68-79** | Register addresses, access, ranges, defaults |
| Appendix 3: DM-PR register table | 80-90 | Separate product family; do not use as the ESS23-RS map |

## Hardware-manual navigation

| Topic | PDF pages |
| --- | --- |
| Explicit ESS23-RS10 / ESS23-RS20 applicability | 3 |
| Electrical limits and installation | 5-6 |
| Wiring diagram, I/O, switch and connector definitions | 7-9 |
| I/O electrical diagrams and DIP selection | 10-11 |
| Motor alarms and reset behavior | 12 |

## Defaults and implementation questions

- The ESS-RS appendix lists 0x0014 default 0 = **115200 baud**, and 0x0015 default 0 = **8N1** (function PDF p69). Both settings require a power cycle to take effect (p6, p69). Read/check actual commissioning settings before assuming these defaults.
- Register 0x0013 lists default 0 and range 0-255, effective with address switches OFF (p69). Hardware p11 reserves address 0 and provides custom-address selection. Choose a valid unicast address explicitly; do not assume factory custom value 0 can identify one motor. The Modbus serial standard defines unicast 1-247 and broadcast 0 (standard PDF p8).
- Register 0x0019 lists default 0, high word first; value 1 reverses the two words (function PDF p30, p70). Verify its current value and signed 32-bit interpretation. Modbus bytes within a 16-bit register and device word order are separate concerns.
- Read Holding Registers is limited by the manufacturer to **16 registers per request** (p7, p12). Do not substitute the generic Modbus maximum; the write limit still needs confirmation.
- Position feedback is described as encoder feedback converted to subdivision units (p69). Subdivision register 0x0011 lists default 1000 (p69), while encoder-resolution register 0x0101 lists 4000 counts for a 1000-line encoder (p77). Confirm pulse, subdivision and encoder units before enabling rotations/degrees conversion for this profile; those units remain part of the common API design.
- Read status/configuration first during bring-up. Saving/restoring parameters is documented only while stopped (p26); determine which settings persist and which need an explicit save from actual firmware behavior.
- Multi-stage position and speed control are documented as external-input-triggered only (p13, checked in the original PDF). The native API must expose their serial parameter/I/O configuration without inventing a serial segment-start operation.
- The ESS appendix documents X0-X3 input assignments and Y0-Y1 output assignments (p73-74, checked in the original PDF). Generic prose mentioning a wider input-register range does not establish extra ESS23-RS terminals; use the relevant model/firmware mapping and preserve reserved bits.

## Verified document inconsistencies to resolve

- Function-manual applicability says `ESS23-RSx1` (p3), although the vendor supplied it with RS10/RS20. Hardware p3 explicitly lists RS10/RS20 but has an `ESS17-RS` header. Keep the exact model/firmware identification from the real motor.
- Hardware p11 prose describes addresses 0-15 / up to 15 drives, but its table and note assign **all four address switches ON to factory reset**, not address 15. The table lists ordinary DIP addresses 1-14. SW1 is termination; SW2-SW5 select address (p8, p11).
- Hardware electrical table says 24-48 V (p5), while overview, connector and wiring prose mention 24-50 V (p4, p7, p9). The RS10/RS20 product specifications state 24-48 V; use that range pending clarification.
- CRC-error prose says the frame is discarded without reply, but the example replies with exception 01 (function p10). Error-code names in p12 also differ from standard Modbus exception meanings. Preserve raw exception codes and verify device behavior.
- Error table p12 mentions only functions 03/06, while p8 documents 0x10 and p11 includes it in the supported-function discussion.
- Stop table p25 contains start/position descriptions beside stop bits; its examples write 0x0100 for stop and 0x0200 for emergency stop. Cross-check p13-14 and the ESS-RS appendix before implementing.
- ESS-RS default column p70 contains conflicting pairs such as acceleration `50 (100ms)` and starting speed `30 (60r/min)`. Its pulse-count range is also malformed. Do not infer scaling or signed limits from those entries alone; verify with readback and hardware.
- Function p8 prints two different CRCs for the same FC10 request: the diagram ends in `B9 56`, while the prose example ends in `FD 12`. Inspection of the original page and independent Modbus CRC calculation for `01 10 00 24 00 02 04 00 00 13 88` confirm `FD 12`. Do not copy printed example bytes into tests without checking them.
- Function p16's position request declares five words / ten payload bytes but prints eleven payload bytes. Its `98 EA` CRC is valid for that malformed frame. The corrected request for acceleration 100, deceleration 100, speed 60 and target 1000 is `01 10 00 21 00 05 0A 00 64 00 64 00 3C 00 00 03 E8 CF 66`. CRC validity alone does not establish a valid Modbus frame.
- Function p68 describes the DIP-status register as SW1-SW7, while the ESS23-RS hardware manual defines five switches (p8, p11). Preserve raw DIP status and confirm the actual firmware mapping before assigning model-specific switch labels.

## Implemented codec scope

The original function-PDF pages 6-12, 16, 18, 20, 29-30 and the full ESS appendix
68-79, plus hardware pages 8 and 11, were re-inspected as rendered pages on
2026-10-03. The following decisions
are implemented in [Codec.h](../../include/MotorControlRS/profiles/ess_rs/Codec.h):

| Concern | Implemented behavior and evidence |
| --- | --- |
| Address | Unicast 1-247 only, an explicit standard-compatible library policy despite the vendor's 0-255 parameter range. |
| FC03 | 1-16 words (p7/p12), all mapped and readable; no undefined, reserved, write-only or unspecified-access words. Raw pair halves may be read. |
| FC06 | Documented writable single words only. Rejecting paired halves is a library policy against unqualified split updates, not a claimed vendor prohibition. |
| FC10 | Four documented start/count windows: 0x0024/2 (p8), 0x0021/5 (p16), 0x001D/3 (p18) and 0x0031/6 (p20). Other windows return unsupported, including unproven subsets. General device maximum, other-pair acceptance and atomic application remain unresolved. |
| Raw values | Words are exactly 16 bits. Codecs do not validate typed ranges, motion preconditions, persistent effects or signed field semantics. |
| Word helpers | Explicit high/low word order; bytes inside each word are big-endian on the wire. Pure signed helpers use two's complement by contract, without resolving unknown ESS field encodings. |
| Homing offset | 0x0035/36 is omitted from the p30 configurable-order list. Its existing catalogue uncertainty remains; no typed offset encoder is supplied. |
| Replies | Exact slave/function/length/count/CRC and applicable write echo, including exact five-byte exceptions. Unknown exception bytes are preserved. |
| Outputs | No payload mutation on error; parsed word count resets to zero. Accessed input/output overlap is rejected. Diagnostic/count outputs must be separate storage. |

The complete appendix audit accounts for 242 documented words and 78
undocumented words in 15 gaps through 0x013F. Sixteen documented words are
explicitly reserved; 0x003B/0x003C have unspecified access. No missing named
register was found. The generated private access table derives from this
same ledger; unknown/reserved words remain unavailable to raw codecs.

**Minimal probe:** FC03 at read-only Driver Model register 0x0000, count1 (p68).
No consuming side effect is documented for this identity field. The eight-byte
request at address1 is `01 03 00 00 00 01 84 0A`; a synthetic raw model0x0305
reply is `01 03 02 03 05 78 B7`. Seven bytes is the smallest successful FC03
reply. `buildProbe`/`parseProbe` expose this path without I/O or retries.
A matching response establishes responsiveness and a raw code, not confirmed
manufacturer/model, readiness, freshness of other fields or motion completion.
Timing, watchdog interaction and firmware side effects remain unqualified.

The vendor exception meanings on pp11-12 differ from standard Modbus: 01 CRC,
02 instruction, 03 disallowed address, 04 outside map, 05 read quantity,
06 access direction and 07 value range. The codec returns neutral `EXCEPTION`
with the raw byte, never a guessed standard label. The p10 no-reply/CRC-error
contradiction remains unresolved. A remote CRC exception is distinct from a
locally detected bad reply CRC.

The complete register catalogue and typed enums are not a complete operational
API. Negative position encoding/range, ramp scaling, motion/stop semantics and
other existing uncertainties still require field-specific helpers and evidence.
Other manufacturers' different counts, consuming reads, data representation and
error envelopes are documented in the [serial review](08_serial_protocol_review.md);
they do not relax ESS validation.

## Architecture consequences

The [architecture](../architecture.md) and [CLI contract](../cli_contract.md)
keep protocol result, decoded motor condition, communication health and motion
completion separate. The original ESS status table (function p68) describes
bit 4 as **0 = enabled, 1 = released**; do not infer its polarity from the
"Motor enable bit" label. A valid alarm-bearing reply is successful decoding,
not a protocol failure. Preserve raw flags and alarm values for unknown
firmware behavior.

The [ecosystem review](02_ecosystem_review.md) records actual sibling APIs and
FieldCore limitations. Sibling timing, register limits, exception labels and
write retry policies are not device evidence for ESS23-RS.

The accepted [axis contract](../axis_contract.md) includes step, angle and
travel modes, but ESS conversion still requires resolved native units and
explicit axis configuration. Public API scope does not resolve the vendor
scaling contradictions. The [profile contract](../profile_contract.md) requires
coverage of the complete documented ESS command/register surface, with gaps
tracked rather than silently omitted.

For framing, CRC, timing and general function semantics, consult the [Modbus serial standard V1.02](../standards/Modbus_Serial_Line_V1.02.pdf) and [application standard V1.1b3](../standards/Modbus_Application_Protocol_V1.1b3.pdf). Device-specific addresses, limits and exception interpretation still come from the manufacturer and hardware validation.
