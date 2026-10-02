# Implementation reference

This is a navigation aid for the future ESS23-RS library, not an implemented API or a verified hardware contract. Sources were downloaded on 2026-10-02. All page numbers below are **physical PDF pages, counted from 1**; printed page numbers in both manufacturer manuals are two lower.

## Source priority

Use the [ESS23-RS hardware manual](../vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf) for connections and switches, and the [Modbus function manual](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf) for commands and the **ESS-RS** register appendix. The [RS20 datasheet](../vendor/ESS23-RS20_Full_Datasheet.pdf) and [RS10 datasheet](../vendor/ESS23-RS10_Full_Datasheet.pdf) each have one drawing/specification page.

The [extracted function-manual text](../pdf-extracted-md/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.md) is searchable. Tables and drawings must be checked in the PDF: extraction can merge columns, lose spacing, or omit figures.

## Function-manual navigation

| Topic | PDF pages | What to extract when implementation begins |
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
- Position feedback is described as encoder feedback converted to subdivision units (p69). Subdivision register 0x0011 lists default 1000 (p69), while encoder-resolution register 0x0101 lists 4000 counts for a 1000-line encoder (p77). Confirm pulse, subdivision and encoder units before offering rotations or degrees.
- Read status/configuration first during bring-up. Saving/restoring parameters is documented only while stopped (p26); determine which settings persist and which need an explicit save from actual firmware behavior.

## Verified document inconsistencies to resolve

- Function-manual applicability says `ESS23-RSx1` (p3), although the vendor supplied it with RS10/RS20. Hardware p3 explicitly lists RS10/RS20 but has an `ESS17-RS` header. Keep the exact model/firmware identification from the real motor.
- Hardware p11 prose describes addresses 0-15 / up to 15 drives, but its table and note assign **all four address switches ON to factory reset**, not address 15. The table lists ordinary DIP addresses 1-14. SW1 is termination; SW2-SW5 select address (p8, p11).
- Hardware electrical table says 24-48 V (p5), while overview, connector and wiring prose mention 24-50 V (p4, p7, p9). The RS10/RS20 product specifications state 24-48 V; use that range pending clarification.
- CRC-error prose says the frame is discarded without reply, but the example replies with exception 01 (function p10). Error-code names in p12 also differ from standard Modbus exception meanings. Preserve raw exception codes and verify device behavior.
- Error table p12 mentions only functions 03/06, while p8 documents 0x10 and p11 includes it in the supported-function discussion.
- Stop table p25 contains start/position descriptions beside stop bits; its examples write 0x0100 for stop and 0x0200 for emergency stop. Cross-check p13-14 and the ESS-RS appendix before implementing.
- ESS-RS default column p70 contains conflicting pairs such as acceleration `50 (100ms)` and starting speed `30 (60r/min)`. Its pulse-count range is also malformed. Do not infer scaling or signed limits from those entries alone; verify with readback and hardware.

For framing, CRC, timing and general function semantics, consult the [Modbus serial standard V1.02](../standards/Modbus_Serial_Line_V1.02.pdf) and [application standard V1.1b3](../standards/Modbus_Application_Protocol_V1.1b3.pdf). Device-specific addresses, limits and exception interpretation still come from the manufacturer and hardware validation.
