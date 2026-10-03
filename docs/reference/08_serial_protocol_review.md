# Serial protocol review for the ESS codec block

Reviewed 2026-10-03. Five original manufacturer manuals are preserved in
[vendor/contrasts](../vendor/contrasts/README.md), with hashes and source URLs
in [serial_contrasts_sources.json](serial_contrasts_sources.json). All page
numbers below are physical PDF pages, starting at 1. None of these additional
profiles is implemented or hardware-qualified. This is a focused comparison
of contrasting designs, not a claim to cover every motor on the market.

## Decision for this block

Implement the ESS protocol directly, with small reusable RTU helpers for CRC,
byte packing and complete-frame validation. Keep register meaning, permitted
operations, address/count limits, word order, probe selection and motion
outcomes in the profile. Return raw protocol exception values separately from
decoded drive alarms. Retain the existing caller-owned transport boundary.

Do not make a generic Modbus register engine the public motion API. The sample
includes configurable byte order, object dictionaries, sparse command maps,
read-to-clear status and ASCII commands. Supporting those later should not
require weakening ESS validation or adding unused framework machinery now.

## Reviewed evidence

### Leadshine iEM-RS: the primary design contrast

[Official manual](https://www.leadshine.com/upfiles/downloads/3b01ef83987a2abfffcb4d4301a9cf4b_1689587677198.pdf)
and [local V2.0 snapshot](../vendor/contrasts/Leadshine_iEM-RS_User_Manual_V2.0.pdf).

- Pages 16-19 specify FC03/06/10, CRC low byte first, high-word-first parameter
  pairs and a 200-byte message ceiling. Do not inherit ESS transfer limits.
- Page 16 specifies slave IDs 1-31; page 21 gives the software ID parameter
  range as 0-127. The larger range needs clarification and qualification.
- A prospective presence probe is FC03, one word at `0x01FF` (version), page
  21. It is not conclusive model identification.
- Page 25 explicitly makes save-status `0x1901` read-to-clear. Being labelled
  read-only does not make a register suitable for discovery.
- Page 26 calls exception `0x08` a CRC error. Its meaning must remain specific
  to this profile rather than being assumed from the Modbus standard.
- Documentation defects: page 17 draws a two-byte FC03 response byte count,
  while its examples use one byte. Its six-word response CRC differs from the
  table on page 18. Standard framing and independently checked vectors are
  required; these defects are not permission to accept malformed responses.

### Oriental Motor AZ: ordinary RTU with configurable representation

[Official function manual](https://www.orientalmotor.com/products/pdfs/opmanuals/HM-60262E.pdf)
and [local snapshot](../vendor/contrasts/Oriental_Motor_AZ_HM-60262E.pdf).

- Pages 233-241 document FC03/06/08/10, up to 125 read words and 123 written
  words, normal write echoes and five-byte exception responses.
- Page 253 allows all four combinations of 32-bit word order and byte order;
  the manual's examples assume high word first and big-endian bytes. A profile
  must establish the active setting before interpreting multiword data.
- Page 240 documents FC08/subfunction `0x0000`, an eight-byte diagnostic echo
  suitable as a prospective non-changing communication check. This identifies
  a responder, not its manufacturer or model. It is not an ESS command.
- Page 241 warns that a failed FC10 operation can leave some registers written.
  An exception does not establish that a multi-register write had no effect.
- Pages 237 and 253 allow suppressing slave-error exceptions in favor of normal
  responses. A validated echo alone cannot establish successful application of
  all requested settings; commissioning and profile readback matter.

### Nanotec PD4-E: Modbus register access and object access coexist

[Official V1.6.0 manual](https://www.nanotec.com/fileadmin/files/Handbuecher/Plug_Drive/PD4-E/fir-v2213/PD4E_ModbusRTU_Technical-Manual_V1.6.0.pdf)
and [local snapshot](../vendor/contrasts/Nanotec_PD4E_ModbusRTU_V1.6.0.pdf), firmware FIR-v2213.

- Pages 104-108 specify IDs 1-247, FC03/04/06/10, 125 read words and 123 written
  words. FC17 and diagnostic/object-access extensions are also listed.
- Page 103 specifies big-endian regular Modbus data but little-endian object
  values inside FC2B/65/66. One global byte-order setting would be insufficient.
- Pages 115-128 describe FC2B/MEI0D object access and extended error envelopes;
  these are not the five-byte exception frames used by ordinary FC03/06/10.
- Pages 157-159 describe read-only identity object `0x1018`. This is an object
  index, not a directly interchangeable Modbus register address. Identity reads
  are a future candidate; a minimal probe has not been selected.
- Page 106 reverses the usual FC03/04 names in its heading, contradicting the
  function table on page 105. Its example response also repeats an incorrect
  CRC. Preserve exact function expectations and validate vectors independently.

### Makerbase SERVO42D/57D: a Modbus-shaped command protocol

[Manufacturer repository PDF](https://github.com/makerbase-motor/MKS-SERVO42D-57D/blob/master/User%20Manual/V1.0.9/MKS%20SERVO42%2657D_Modbus%20RTU%20User%20Manual%20V1.0.9.pdf)
and [local V1.0.9 snapshot](../vendor/contrasts/Makerbase_SERVO42_57D_Modbus_V1.0.9.pdf).

- Page 9 warns that the address map is not continuously readable/writable.
  Commands require their documented lengths; a generic contiguous scan is
  inappropriate. The stated address range extends to 255 and requires review
  against standard Modbus addressing before any profile support.
- Page 9 lists FC04/06/10 but later mentions FC03; concrete multiword read
  examples use FC04. Record the conflict rather than guessing.
- Page 11 returns signed 48-bit encoder data and contradicts itself about
  rotation polarity. Keep raw width and qualified direction explicit.
- Page 14 documents FC04 reading two words at `0x0040` for version information:
  a prospective non-changing identity read. A shorter one-word read is not
  established for this command.
- Page 9 describes configuration failures as FC06 value `0xFFFF` or FC10 count
  zero, instead of ordinary exception frames. Strict ESS echo checking must
  reject these; a future Makerbase profile may explain the rejection.

### Applied Motion SCL: RS485 without Modbus framing

[Official host command reference](https://applied-motion.s3.amazonaws.com/documents/Manuals/Host-Command-Reference_920-0002W.pdf)
and [local revision W snapshot](../vendor/contrasts/Applied_Motion_Host_Command_Reference_920-0002W.pdf).

- Pages 73 and 295-310 describe ASCII commands, character addresses and carriage
  return framing. Pages 205 and 307-310 describe optional, family-dependent
  checksums. Modbus CRC, numeric slave ranges and register word order do not
  apply to this command path.
- Page 184 identifies `MV` as an immediate read-only model/revision query,
  excluding BLu drives. An addressed query is a prospective probe for reviewed
  compatible models; protocol mode and checksum settings must already match.
- Page 304 distinguishes accepted/executed `%`, accepted/queued `*`, and
  rejected `?` replies. None replaces motion-completion evidence. Global reads
  can make several drives reply simultaneously; discovery must address nodes.
- This handbook spans stepper and servo families. It does not establish one
  common maximum command length or support on every listed drive. A future
  profile still needs its model hardware manual and command-compatibility audit.

## Concrete implementation consequences

1. **Share mechanical wire operations.** Keep helpers such as `crc16`, `read16`
   and `write16` small. Do not add a runtime protocol registry, global motor
   address type, or transport object for these unimplemented candidates.
2. **Keep parsers strict.** Require expected slave/function and exact response
   shape. Validate write echoes against the request. Reset output count on an
   error and leave payload outputs unchanged. Unknown remote exception values
   remain observable; they are not local CRC errors.
3. **Keep profile policy visible.** A bounded buffer capacity is not a drive's
   legal transfer count. Raw register words do not establish signedness, scale,
   byte/word order, an origin, or an atomic device snapshot.
4. **Do not equate reply and outcome.** Parser success concerns the response
   contract. Partial writes, queued commands and lost acknowledgements need
   separate operation rules. The codec should not retry or infer completion.
5. **Review each probe individually.** Use documented, addressed, non-changing
   operations and preserve the difference between presence and identity. Do not
   scan unknown registers or send cross-protocol guesses on a live mixed bus.

Future cross-profile tests should cover the behaviors above when those
profiles are implemented. The current block should test real ESS behavior;
unused speculative codecs and placeholder tests add maintenance cost.

## Evidence quality and reproduction

Ambiguous tables were inspected as rendered PDF pages, not only text extracts:
Leadshine 17, 18, 21, 22, 25; Oriental Motor 241, 253; Nanotec 103, 105, 106,
128; Makerbase 9, 11, 14; Applied Motion 184, 304. Other cited passages were
checked in their page-indexed text. The manifest records these page audits.

Independent CRC calculation found these transcription defects (bytes below
are wire order): Leadshine page 17 request payload `01 03 01 91 00 01` needs
`D4 1B`, not its printed `D3 1B`; its six-word response needs `B6 13`, agreeing
with page 17 but not page 18. Nanotec page 106 response payload
`05 03 04 02 40 00 00` needs `BF 9F`, not the printed `41 21`. These are
documentation checks, not observed firmware behavior.

Run `python scripts/prepare_serial_contrasts.py --check` to verify the five
snapshots offline. Add `--extract` for disposable searchable text. A normal
download run refuses changed source bytes and preserves original retrieval
metadata. Existing ESS/vendor snapshots were not replaced.

The ADI TMCM-1181 firmware manual was also readable through the web research
tool, but direct download failed during this review. It is not represented as
a preserved snapshot; the downloaded SCL manual supplies the non-Modbus
contrast. Additional families remain candidates, not advertised compatibility.
