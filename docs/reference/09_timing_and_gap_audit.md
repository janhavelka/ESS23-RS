# ESS timing and remaining evidence gaps

Reviewed 2026-10-03 for MotorControl-RS. This audit separates manufacturer
statements, standard requirements, calculated wire times and application policy.
No measurements were taken on COM13. Page numbers are physical PDF pages,
starting at 1.

## Sources and limits

- [ESS function manual V1.0](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf),
  especially pp6-8, 15-18, 23-26, 68-79.
- [ESS23-RS10/20 hardware manual](../vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf),
  especially pp9-12.
- [Modbus serial guide V1.02](../standards/Modbus_Serial_Line_V1.02.pdf),
  pp7-14, and [application specification V1.1b3](../standards/Modbus_Application_Protocol_V1.1b3.pdf).
- Preserved [Leadshine iEM-RS V2.0](../vendor/contrasts/Leadshine_iEM-RS_User_Manual_V2.0.pdf),
  pp16 and 21, and [Oriental Motor AZ HM-60262E](../vendor/contrasts/Oriental_Motor_AZ_HM-60262E.pdf),
  pp232 and 253, provide timing contrasts only.

The register transcription and source conflicts belong in the
[JSON catalogue](ess_rs_registers.json), its generated
[register reference](05_ess_register_catalog.md), and the
[implementation reference](01_implementation_reference.md). This note supplies
the timing conclusions needed before implementing transport and workflows.

The register audit accounts for the documented words and intervening holes;
it does not assign invented meanings to gaps or resolve unspecified access.
The complete interval 0x0000-0x013F contains 242 documented words and 78
undocumented words in 15 gaps. The documented words include 16 explicitly
reserved words and two with unspecified access (0x003B/0x003C). The generator
checks this complete coverage and derives the codec access policy from it.
The reviewed FC10 examples now cover `0x0024/count2`, `0x0021/count5`,
`0x001D/count3` and `0x0031/count6`. Their existence does not establish a
device-wide write limit or atomic application. In particular, p16's printed
FC10 request contains an extra payload byte: its documented ramp values are
useful evidence, but its full printed frame is not a valid test vector.

An additional primary-source web search found no ESS-specific timing supplement
that resolves the gaps below. A [StepperOnline PR-series example](https://help.omc-stepperonline.com/hc/s/articles/pr-series-stepper-drive-rs485-communication-operation-example-code)
uses familiar addresses but targets another family and contains malformed FC10
examples. It is not sufficient evidence to extend ESS commands or timing rules.
Preserved ESS originals were not replaced.

## RTU framing and actual character length

The serial standard p12 uses **11 bits per RTU character**: one start bit, eight
data bits, parity and one stop bit; without parity it specifies two stop bits.
ESS explicitly offers **8N1**, and lists it as the default (function pp6, 69).
That setting occupies **10 bits per character**, including the start bit.
The other documented ESS formats, 8N2, 8E1 and 8O1, occupy 11 bits.
Configure the actual drive format; do not silently substitute 8N2 for 8N1.

Standard p13 distinguishes two intervals:

- `t1.5`: a silence longer than 1.5 character times inside a frame makes the
  frame incomplete. Transmit the entire request without inserted gaps.
- `t3.5`: at least 3.5 character times of silence separates RTU frames.
  This is a framing interval, not a drive's command-processing deadline.

For baud rates **at or below 19200**, use character-based timing. For rates
**above 19200**, the standard recommends fixed `t1.5 = 750 us` and
`t3.5 = 1750 us`. Its recommendation is not the much shorter result of simply
multiplying 3.5 by a character time at 115200 baud.

For a selected format, `character_us = bits_per_character * 1000000 / baud`.
The table shows calculated microseconds, rounded upward for integer timers:

| Baud | ESS format | Bits/character | Character us | t1.5 us | t3.5 us |
| --- | --- | ---: | ---: | ---: | ---: |
| 9600 | 8N1 | 10 | 1042 | 1563 | 3646 |
| 9600 | 8N2 / 8E1 / 8O1 | 11 | 1146 | 1719 | 4011 |
| 19200 | 8N1 | 10 | 521 | 782 | 1823 |
| 19200 | 8N2 / 8E1 / 8O1 | 11 | 573 | 860 | 2006 |
| 38400 | 8N1 | 10 | 261 | 750 | 1750 |
| 38400 | 8N2 / 8E1 / 8O1 | 11 | 287 | 750 | 1750 |
| 115200 | 8N1 | 10 | 87 | 750 | 1750 |
| 115200 | 8N2 / 8E1 / 8O1 | 11 | 96 | 750 | 1750 |

The 10-bit rows below 19201 baud are calculations for the vendor's 8N1 format,
not a claim that the standard specifies 8N1 or that ESS firmware uses those
exact receive thresholds. An application can conservatively wait at least the
11-bit `t3.5` value before transmitting at those rates. Receive gap detection
and any additional pacing still need qualification against the actual drive.
Calculate each timer from the original fraction; avoid accumulated rounding
from a previously rounded character value.

## What an ESS response timeout can mean

The reviewed ESS manuals do **not** specify a maximum response-processing
delay, a minimum host request period, or a post-reply receive-recovery delay.
The generic serial guide p10 discusses application-dependent timeouts; its
illustrative seconds at 9600 baud are not an ESS timing requirement.

Calculated wire durations help size a timeout but cannot finish that decision:

| Transaction component | Bytes | 115200 baud, 8N1 wire time |
| --- | ---: | ---: |
| FC03 request, including minimal probe | 8 | 0.694 ms |
| One-word FC03 response | 7 | 0.608 ms |
| Sixteen-word FC03 response | 37 | 3.212 ms |
| FC06 request or successful response | 8 | 0.694 ms |
| FC10 request writing two words | 13 | 1.128 ms |
| FC10 request writing three words | 15 | 1.302 ms |
| FC10 request writing five words | 19 | 1.649 ms |
| FC10 request writing six words | 21 | 1.823 ms |
| Successful FC10 response | 8 | 0.694 ms |
| Exception response | 5 | 0.434 ms |

These are continuous serial bits divided by baud, with no gaps, processing,
host scheduling or cable delay included. A one-word probe cycle with both a
1.750 ms request-to-response gap and a 1.750 ms response-to-next-request gap
already occupies about **4.802 ms**, before extra processing and scheduling.
It is a calculation for those assumptions, not measured throughput or a
recommended polling period.

For the future adapter, define the timeout's start and end explicitly. If it
starts when the UART finishes physical TX and ends on a complete response,
the budget needs request-to-response silence, drive processing, complete reply
wire time, any receive-frame completion wait, and scheduling margin. If it
starts when the application queues TX, it also needs the TX queue and request
duration. A first-byte timeout alone does not bound a truncated frame.

Choose an adjustable conservative bench timeout in the example and label it
as host policy. Measure idle and moving response distributions before claiming
a maximum service rate or stop latency. The byte codec itself has no clock and
cannot enforce `t1.5`, `t3.5`, frame boundaries, response deadlines or freshness.

## DE, receive framing and late responses

The future application-owned adapter must satisfy these requirements:

1. Wait for an idle bus before a request. Assert DE with the setup time required
   by the board's actual transceiver, then send the complete request.
2. Wait for the last stop bit to leave the UART before releasing DE. A software
   queue or FIFO becoming empty is not by itself evidence of physical TX
   completion. Apply any transceiver hold time, then receive promptly.
3. Do not keep DE asserted throughout the inter-frame waiting period; this can
   prevent the motor from replying. ESS documentation supplies no numeric DE
   setup/hold values for the host board.
4. Use UART framing/error information and sufficiently accurate byte/gap
   timing. Buffer arrival in one software callback does not prove bytes arrived
   continuously on the wire. A CRC-valid byte sequence is not proof of valid
   RTU timing.
5. Track only one outstanding transaction per shared bus and settle a timed-out
   one before associating new traffic with a later request.

The last point is essential: an FC03 reply has **no register start address or
transaction identifier**. Its byte count, address, function and CRC can all be
correct for a late reply to a different read of the same size. Request
expectations in a parser cannot recover that missing wire information. An FC06
local echo can also be byte-for-byte identical to its expected successful
response; the transport must handle its own electrical/software echo behavior.

Draining stale bytes and waiting for an idle gap are necessary recovery tools,
but an idle gap alone cannot rule out an arbitrarily late response from a
still-processing device. The application needs a qualified response bound or
an explicit recovery policy that preserves uncertain execution. Do not replay
a timed-out move simply because the next frame parses correctly.

Stop priority therefore includes bus scheduling. Stop can replace pending
application work, but cannot safely truncate bytes already on the shared bus
or assume an in-flight request has disappeared. Report that distinction; the
manual supplies no guaranteed total latency from host stop request to rest.

## Motion and configuration timing facts

| Concern | Documented evidence | Consequence or remaining gap |
| --- | --- | --- |
| Ramp registers | Function pp15-18 call the ramp values milliseconds; p16 and p18 examples write `0x0064` for 100 ms. Appendix p70 instead shows defaults such as `50 (100ms)`. | Examples support a direct-ms interpretation, but the contradiction is not resolved for actual ESS firmware. Do not adopt a factor of two from the default column. |
| Acceleration versus ramp time | Function p15 draws a ramp from starting speed to the selected speed and shortened motion that may not reach that speed. | A common acceleration in steps/s^2 or rad/s^2 is not simply a register value. A future conversion needs verified ramp semantics, applicable start/target speed, range and quantization. No typed ramp encoder is established by this audit. |
| Normal stop | Function p25 says to use deceleration configured before motion; a changed ramp after starting does not replace that established stop ramp. | Preconfigure and retain the applicable ramp. Do not promise arbitrary fresh deceleration with one stop command. |
| Emergency stop | Function p25 describes a stop without the normal deceleration ramp. | This does not establish motor release, bounded physical stopping time or a safety-rated stop. The damaged bit-description table remains cross-checked against command examples. |
| Baud / serial format | Function pp6 and 69 require power-on again after changing `0x0014` / `0x0015`. | Readback of a stored setting does not prove the current UART has switched. Save requirements, downtime and restart readiness require qualification. |
| Custom address | Hardware p11 requires custom addressing to be set and saved, with address switches OFF. | Activation instant and acknowledgment address during a change are unspecified; do not treat this as an ordinary retryable setting write. |
| Save / factory restore | Function p26 requires the motor to be stopped; otherwise the operation is ignored. | No operation-complete flag, worst-case duration, flash endurance, safe power-removal delay or post-restore communication sequence is specified. An echo is not evidence that persistence finished. |
| X0-X3 pulses | Hardware p9 prose requires greater than 10 ms; its figure says 10 ms or more. | Use greater than 10 ms plus application margin. This applies to physical inputs, not RS485 request pacing. |
| Segment selection | Function p23 requires selection inputs stable for 5 ms before and after the PT trigger. | Preserve setup/hold in external-input workflows. The PV section does not independently state that timing; do not silently promote it into a verified PV rule. |
| Inputs after power-up | Hardware p9 says X0-X3 start unassigned and need configuration. | Qualification must check actual saved-I/O behavior before relying on limits or stop inputs after restart. No numeric boot-ready delay is given. |
| Lock delay `0x0107` | Function p78 gives milliseconds, range 0-20000, default 4000, after movement stops. | This is a motor lock-state delay, not a communication timeout or motion-completion guarantee. |
| Input filter `0x0108` | Function p78 gives a coefficient, range 0-65535, default 2. | No conversion from coefficient to time is supplied. |
| Arrival/end time `0x010C` | Function p78 gives range 0-200 and default 10. | Neither a unit nor an exact qualification rule is stated; do not infer milliseconds from neighboring entries. |
| Communication loss | No watchdog or refresh interval is documented in the reviewed ESS function material through Appendix 2 or the hardware manual. | Do not advertise automatic communication-loss stopping, and do not claim firmware lacks such a feature. Its presence, settings and behavior remain unqualified. |

## Why timing stays profile-specific

Leadshine's p16 table gives timing at four baud rates, but does not specify the
tested request/response lengths or a worst-case guarantee. Its 115200 row lists
2.44, 0.64 and 0.6 ms with a total of 3.08 ms; those three numbers sum to 3.68 ms.
Do not turn this inconsistent table into an ESS deadline or a universal delay.
Leadshine also has a communication-bit-delay parameter `0x01C4`, range 0-100,
default 35 bits (p21); ESS has no equivalent documented parameter.

Oriental Motor AZ p232 specifies different behavior: automatic silent intervals
of at least 4 ms at 9600 and 2.5 ms at the listed higher rates, with master frame
interval examples of at least 5 ms and 3 ms respectively. Its reply wait includes
silence, processing and configurable transmission wait. Its communication
timeout watches bus traffic, including valid messages addressed to **other**
slaves, rather than proving that this axis is still receiving intended updates.
Page 253 permits timeout monitoring to be disabled and lists it disabled by
default. A general library must represent the verified device semantics;
neither a valid probe nor other bus traffic is universally a motion watchdog.

These concrete differences support the existing design: share byte operations
and vocabulary, keep timing requirements and command outcomes in the profile,
and let the application own the actual timers and bus.

## Qualification work before motion workflows

- Capture DE, TX and RX to establish physical TX completion, local echo,
  response onset, reply gaps and recovery after deliberately truncated reads.
- Check startup and active settings with addressed non-changing reads. Keep
  response-time measurements separate from boot readiness and motor readiness.
- Qualify ramp meaning against readback and short motion, including whether
  ramps are latched at start and the effect of a normal stop during acceleration.
- Establish arrival criteria, raw position units/sign and whether paired
  feedback reads are coherent. A valid multiword frame alone proves none of
  those motion semantics.
- Qualify save/restore, address/baud changes and restart behavior as separate
  commissioning operations. These must not be part of presence discovery.
- Record communication-loss behavior explicitly before relying on a watchdog
  or selecting a control update deadline.

Source images inspected for this timing audit: function pp6, 15, 16, 18, 23,
25, 26, 70 and 78; hardware p9; serial guide pp10, 12 and 13; Leadshine p16;
Oriental Motor pp232 and 253. Other cited facts were checked in page-indexed
text, with register-field review recorded separately in the catalogue.
