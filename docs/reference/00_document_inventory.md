# Reference inventory

Collected on 2026-10-02 from STEPPERONLINE and the Modbus Organization.

The main STEPPERONLINE file endpoints returned HTTP 403. Manufacturer files
were retrieved from the official UK storefront. The exact requested and
successful URLs, SHA-256 hashes, retrieval times, sizes, and PDF page counts
are retained in [sources.json](sources.json).

| Downloaded file | Purpose | PDF pages | Bytes |
| --- | --- | ---: | ---: |
| [Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf) | Motor functions and ESS-RS register appendix | 90 | 2941615 |
| [ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf](../vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf) | Shared ESS23-RS10/RS20 wiring and electrical requirements | 12 | 503463 |
| [ESS23-RS20_Full_Datasheet.pdf](../vendor/ESS23-RS20_Full_Datasheet.pdf) | ESS23-RS20 electrical and mechanical specifications | 1 | 122645 |
| [ESS23-RS10_Full_Datasheet.pdf](../vendor/ESS23-RS10_Full_Datasheet.pdf) | ESS23-RS10 electrical and mechanical specifications | 1 | 121483 |
| [ESS23-RS20.STEP](../vendor/cad/ESS23-RS20.STEP) | Optional RS20 mechanical reference | — | 5555894 |
| [ESS23-RS10.STEP](../vendor/cad/ESS23-RS10.STEP) | Optional RS10 mechanical reference | — | 5539313 |
| [Y_Series_Stepper_Driver_Debug_Software_V1.2.7.zip](../vendor/software/Y_Series_Stepper_Driver_Debug_Software_V1.2.7.zip) | Tuning software linked from both official motor product pages | — | 32468652 |
| [Modbus_Application_Protocol_V1.1b3.pdf](../standards/Modbus_Application_Protocol_V1.1b3.pdf) | Supplemental Modbus function, frame and exception definitions | 50 | 932519 |
| [Modbus_Serial_Line_V1.02.pdf](../standards/Modbus_Serial_Line_V1.02.pdf) | Supplemental Modbus RTU framing, CRC and serial-line requirements | 44 | 264122 |

## Reading order

1. ESS23-RS10/RS20 hardware manual for terminals, power, and DIP switches.
2. Modbus function manual for commands, modes, and the ESS-RS register appendix.
3. RS20 datasheet for the initial target; RS10 datasheet for variant comparison.
4. Modbus standards for the underlying protocol and serial framing.

Use [implementation notes](01_implementation_reference.md) to navigate the
manuals and locate unresolved inconsistencies. Raw text is in
[pdf-extracted-md](../pdf-extracted-md/README.md); verify tables in the PDFs.

## Provenance and rights

The tuning archive is linked from the official product pages; it contains a
Windows executable, not reusable library source. STEP files are optional
mechanical references. No motor or tuning software has been run.

Manufacturer PDFs, software, CAD files, and Modbus standards retain their
original owners' rights; they are not relicensed as library code.
