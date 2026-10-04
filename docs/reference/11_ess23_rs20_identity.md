# Confirmed bench model and encoder evidence

Checked on 2026-10-04 following the user's confirmation that the attached motor
is ESS23-RS20. This supplements the historical bench reports; it does not change
their raw observations or qualify motion.

| Fact | Evidence / scope |
| --- | --- |
| Attached model: ESS23-RS20 | User confirmation, 2026-10-04; independent of decoding a wire model code |
| Holding torque 2.2 Nm; supply 24–48 VDC | [Official product page](https://www.omc-stepperonline.com/ess-series-2-2nm-311-55oz-in-nema-23-integrated-rs485-closed-loop-stepper-servo-motor-24-48vdc-1000ppr-ess23-rs20), description/electrical specifications |
| Encoder: incremental, differential, three signal channels, 1000 PPR | Same product page, encoder electrical specifications; documented product specification |
| Full motor step: 1.80 degrees, hence 200 full steps/revolution | [Preserved RS20 datasheet](../vendor/ESS23-RS20_Full_Datasheet.pdf), physical p1, drawing A4573; visually rechecked |
| Encoder count convention: four times the encoder line value | [Function manual](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf), physical p77 (printed p75); visually rechecked. Nominal 1000 PPR therefore means 4000 decoded counts/revolution, or 0.09 degrees/count |
| Configured encoder resolution 4000; subdivision 1000 | Existing [typed COM13 readback](../reports/ess_release_05_2026-10-04.md), repeated in later reports; configuration evidence, not a new measurement |
| Raw model `0x4EEA`, version `0x0029`, algorithm 3 | Same readback. These values are now associated with the user-confirmed bench SKU; this does not establish a universal code mapping or undocumented algorithm semantics |

The product page links the same three PDF filenames already preserved in
[sources.json](sources.json). Their local SHA-256 hashes match that manifest.
Fresh download attempts on both the main and UK storefront returned HTTP403;
current remote PDF bytes could not be compared. Original vendor files were
not replaced. The datasheet and function-manual pages above were read directly
from the preserved PDFs, rather than inferred from extracted text.

The vendor's [common encoder article](https://help.omc-stepperonline.com/hc/s/articles/common-closed-loop-stepper-motors1000ppr4000cpr-encoder-parameters)
also explains 1000 PPR / 4000 CPR, but describes other named motor suffixes and
both optical and magnetic encoders. It does not identify the ESS23-RS20 sensor
technology or exact part. Those details are unnecessary for the implemented
unit arithmetic. Three advertised channels alone do not establish ESS-specific
index behavior or an externally accessible encoder connector.

The remaining conversion/completion questions concern the actual firmware:
physical p77 lists algorithm values 1/2, whereas this motor returns 3. Physical
p69 (printed p67) describes current position as commanded position in open loop
and subdivision-equivalent encoder feedback in closed loop. It is not a raw
encoder-count register. Product-level closed-loop capability does not resolve
that runtime algorithm discrepancy or the paired feedback's signed encoding.
Nominal encoder resolution is documented; shaft accuracy, physical direction
and motion completion still require their own evidence. No encoder chip ID or
additional label photograph is a prerequisite for independent preparation.

This correction changes documentation only. No motor setting, firmware, host
axis configuration or generated descriptor was changed; no new hardware run
was performed. Prompt08 was not executed by this documentation correction;
its subsequent [action/stop disposition](../reports/ess_release_08_2026-10-04.md)
recorded the then-current timing/echo gates. Later
[regular-API integration](../reports/regular_api_sniff_2026-10-04.md) replaces
those admission gates with the declared wiring and checked software receive
contract; this does not change the identity-source facts above.
