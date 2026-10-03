# ESS-RS source register catalogue

Generated from [ess_rs_registers.json](ess_rs_registers.json) by
[generate_ess_registers.py](../../scripts/generate_ess_registers.py).
Edit the ledger and regenerate; `--check` detects drift.

This catalogues the full ESS-RS appendix and named command choices. It
implements immutable metadata and bounded lookup only: no frame codec,
setter, motion preparation, transport, CLI or hardware support is implied.
The [profile contract](../profile_contract.md) remains the operational
coverage contract; source completeness is not operational completeness.

## Evidence and counts

The original [function PDF](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf)
physical pages 68-79 supply the ESS map; pages 13-30 and 32-67 were also
visually reviewed for commands, I/O, paired words and homing diagrams.
DM-PR pages 80-90 are excluded. The [hardware PDF](../vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf)
pages 3, 8, 11 and 12 establish model scope, terminals, DIP differences
and alarm labels. Exact hashes and page evidence are in the ledger.

- 221 logical descriptors covering 242 distinct 16-bit register words.
- 21 paired records: five ordinary pairs and sixteen position-segment pairs.
- Sixteen stored position segments, sixteen speed segments, sixteen shared start speeds.
- Sixteen explicitly reserved position-segment words retained as reserved metadata.
- 20 typed choice/bitfield groups with 135 named values, including 35 homing methods.

Every descriptor and returned string has static lifetime. `findRegister`
returns the containing logical record for either word of a pair; an
undocumented address returns null. Indexed helpers accept segments 1-16,
inputs X0-X3 and outputs Y0-Y1, and reject invalid indexes/field values.
HIGH/LOW constants use the manual's default-order labels; active word
order still must be established. No pointer or configuration is retained.

## Limits on interpretation

- Every register is 16 bits; paired records occupy two successive registers. Width does not settle signed encoding or range.
- Source RW/S notation is preserved; no automatic persistence or save semantics inferred. RW persistence likewise unspecified.
- All source defaults are metadata, not detected current values or library initialization writes.
- Undocumented address holes are not labelled reserved: only the 16 position-segment reserved words are explicitly reserved.
- Signed semantic values do not establish negative wire encoding. No setters/value acceptance functions are supplied.
- Source catalogue completeness is independent from resolved semantics, implemented operations and hardware qualification.
- Source text spacing is normalized for readability; contradictory numeric values and malformed ranges are retained and labelled.
- Four ESS inputs do not establish externally selectable sixteen-segment operation:
  pages 23-24 note a maximum of eight selected segments for four-input devices.
  The sixteen stored register sets are still fully catalogued.
- Position and speed segment execution requires external inputs; no serial segment-start command is invented.
- Negative homing modes are semantic IDs only. Their wire encoding and paths remain unresolved.
- The homing-offset pair is omitted from the page 30 configurable-word-order list; applicability stays unresolved.
- Source inconsistencies remain per-entry issue flags and notes; an empty issue mask is not hardware qualification.

## Register inventory

Ranges, units and defaults below are source metadata. Parenthesized
inconsistencies are retained; they are not silently converted to scaling.
RW/S is the vendor notation, with persistence/application unresolved.

| Address | Name | Words | Source access | Native unit | Source range | Source default | PDF pages | Issues |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| `0x0000` | `DRIVER_MODEL` | 1 | RO | model code | unspecified | 0x0305 | 68 | none recorded |
| `0x0001` | `DRIVER_VERSION` | 1 | RO | version code | unspecified | 0x0100 | 68 | none recorded |
| `0x0002` | `ACTIVE_NODE` | 1 | RO | slave address | unspecified | unspecified | 68 | none recorded |
| `0x0003` | `DIP_STATUS` | 1 | RO | bitfield | unspecified | unspecified | 68, 18 | MODEL_APPLICABILITY, SOURCE_CONFLICT |
| `0x0006` | `ERROR_CODE` | 1 | RO | alarm code | 0: normal; 1-5: error | unspecified | 68 | MODEL_APPLICABILITY |
| `0x0007` | `MOTION_STATUS` | 1 | RO | bitfield | unspecified | unspecified | 68 | none recorded |
| `0x0008` | `INPUT_STATUS` | 1 | RO | bitfield | unspecified | unspecified | 68, 69 | none recorded |
| `0x0009` | `OUTPUT_STATUS` | 1 | RO | bitfield | unspecified | unspecified | 69 | none recorded |
| `0x000A` | `CURRENT_POSITION` | 2 | RO | subdivision equivalent | unspecified | unspecified | 69, 30 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x000C` | `CURRENT_SPEED` | 1 | RO | unspecified | unspecified | unspecified | 69 | UNIT_UNSPECIFIED, SIGNED_ENCODING_UNRESOLVED |
| `0x0010` | `DEFAULT_DIRECTION` | 1 | RW | unspecified | 0-1 | 0 | 69 | none recorded |
| `0x0011` | `SUBDIVISION` | 1 | RW | subdivision setting | 400-51200 | 1000 | 69 | SCALE_UNRESOLVED |
| `0x0013` | `CUSTOM_NODE` | 1 | RW | slave address | 0-255 | 0 | 69 | MODEL_APPLICABILITY |
| `0x0014` | `BAUD_RATE` | 1 | RW | unspecified | 0-3 | 0 | 69 | none recorded |
| `0x0015` | `SERIAL_FORMAT` | 1 | RW | unspecified | 0-3 | 0 | 69 | none recorded |
| `0x0017` | `OVER_LIMIT_STOP` | 1 | RW | unspecified | 0-1 | 0 | 70 | SEMANTICS_UNRESOLVED |
| `0x0018` | `SOFT_LIMIT_ENABLE` | 1 | RW | unspecified | 0-1 | 0 | 70, 28, 29 | SOURCE_CONFLICT |
| `0x0019` | `WORD_ORDER` | 1 | RW | unspecified | 0-1 | 0 | 70, 30 | none recorded |
| `0x001D` | `JOG_SPEED` | 1 | RW | r/min (documented) | -3000 -3000 r/min (as printed) | 120 (5 r/min) | 70, 17, 18, 19 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED, SOURCE_CONFLICT |
| `0x001E` | `JOG_ACCELERATION_TIME` | 1 | RW | ms (documented) | 0-2000 ms | 50 (100 ms) | 70, 17 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x001F` | `JOG_DECELERATION_TIME` | 1 | RW | ms (documented) | 0-2000 ms | 50 (100 ms) | 70, 17, 25 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0020` | `POSITION_START_SPEED` | 1 | RW | r/min (documented) | 0-3000 r/min | 30 (60 r/min) | 70, 16 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0021` | `POSITION_ACCELERATION_TIME` | 1 | RW | ms (documented) | 0-2000 ms | 50 (100 ms) | 70, 15 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0022` | `POSITION_DECELERATION_TIME` | 1 | RW | ms (documented) | 0-2000 ms | 50 (100 ms) | 70, 15, 25 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0023` | `POSITION_SPEED` | 1 | RW | r/min (documented) | 0-3000 r/min | 60 (60 r/min) | 70, 15 | none recorded |
| `0x0024` | `POSITION_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFFF (as printed) | 5000 (combined pair) | 70, 14, 16, 30 | RANGE_MALFORMED, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0027` | `MOTION_COMMAND` | 1 | WO | bitfield | 0-65535 | - | 70, 71, 13, 14, 16, 18, 19, 25 | SEMANTICS_UNRESOLVED, SOURCE_CONFLICT |
| `0x002D` | `AUXILIARY_COMMAND` | 1 | WO | opcode | 0-65535 | - | 71, 26 | none recorded |
| `0x0030` | `HOMING_AUXILIARY` | 1 | RW | unspecified | 0-11 | 0 | 72 | none recorded |
| `0x0031` | `HOMING_METHOD` | 1 | RW | unspecified | 0-65535; listed choices: -1 through -4, 1-14, 17-30, 33-35 | 24 (printed twenty four) | 72, 19 | SIGNED_ENCODING_UNRESOLVED, SOURCE_CONFLICT |
| `0x0032` | `HOMING_SPEED` | 1 | RW | r/min (documented) | 5-3000 r/min | 120 (60 r/min) | 72, 19, 20 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0033` | `HOMING_QUERY_SPEED` | 1 | RW | r/min (documented) | 5-300 r/min | 60 (60 r/min) | 73, 19, 20 | none recorded |
| `0x0034` | `HOMING_RAMP_TIME` | 1 | RW | ms (documented) | 30-2000 ms | 50 (100 ms) | 73, 19, 20 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0035` | `HOMING_OFFSET` | 2 | RW | native position; scale unresolved | -0xFFFFFFF to 0xFFFFFFF | 0 (combined pair) | 73, 19, 30 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED, WORD_ORDER_UNRESOLVED |
| `0x0037` | `POSITIVE_SOFT_LIMIT` | 2 | RW | native position; scale unresolved | -0xFFFFFFF to 0xFFFFFFF | 0 (combined pair) | 73, 29, 30 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0039` | `NEGATIVE_SOFT_LIMIT` | 2 | RW | native position; scale unresolved | -0xFFFFFFF to 0xFFFFFFF | 0 (combined pair) | 73, 29, 30 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x003B` | `COLLISION_THRESHOLD_003B` | 1 | UNSPECIFIED | position error; unit unspecified | 50-4000 | 200 | 73 | ACCESS_UNSPECIFIED, DUPLICATE_MAPPING, UNIT_UNSPECIFIED |
| `0x003C` | `COLLISION_CURRENT_003C` | 1 | UNSPECIFIED | percent | 20-100 | 50 | 73 | ACCESS_UNSPECIFIED, DUPLICATE_MAPPING |
| `0x0040` | `INPUT_POLARITY` | 1 | RW | bitfield | 0-65535 | 0 | 73 | none recorded |
| `0x0041` | `INPUT_X0_FUNCTION` | 1 | RW | unspecified | 0-17 | 1 | 73, 74, 14, 27 | none recorded |
| `0x0042` | `INPUT_X1_FUNCTION` | 1 | RW | unspecified | 0-17 | 2 | 73, 74, 14, 27 | none recorded |
| `0x0043` | `INPUT_X2_FUNCTION` | 1 | RW | unspecified | 0-17 | 3 | 73, 74, 14, 27 | none recorded |
| `0x0044` | `INPUT_X3_FUNCTION` | 1 | RW | unspecified | 0-17 | 0 | 73, 74, 14, 27 | none recorded |
| `0x004B` | `OUTPUT_POLARITY` | 1 | RW | bitfield | 0-65535 | 0 | 74 | none recorded |
| `0x004C` | `OUTPUT_Y0_FUNCTION` | 1 | RW | unspecified | 0-11; values6, 7, 8 not described | 0 | 74, 28 | SEMANTICS_UNRESOLVED |
| `0x004D` | `OUTPUT_Y1_FUNCTION` | 1 | RW | unspecified | 0-11; values6, 7, 8 not described | 0 | 74, 28 | SEMANTICS_UNRESOLVED |
| `0x004F` | `CUSTOM_OUTPUT` | 1 | RW | bitfield | 0-65535 | 0 | 74, 28 | none recorded |
| `0x0050` | `EXTERNAL_POSITION_MODE` | 1 | RW | unspecified | 0-1 | 0 | 75, 23 | none recorded |
| `0x0051` | `PV_TRIGGER_MODE` | 1 | RW | unspecified | 0-1 | 0 | 75, 29 | none recorded |
| `0x0060` | `POSITION_SEGMENT_01_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | high 0; low 5000 | 75, 22, 23, 76 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0062` | `POSITION_SEGMENT_01_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | 120 (0/min) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0063` | `POSITION_SEGMENT_01_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0064` | `POSITION_SEGMENT_01_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0065` | `POSITION_SEGMENT_01_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | 0 | 75, 22, 23, 76 | none recorded |
| `0x0066` | `POSITION_SEGMENT_02_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 75, 22, 23, 76 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0068` | `POSITION_SEGMENT_02_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0069` | `POSITION_SEGMENT_02_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x006A` | `POSITION_SEGMENT_02_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x006B` | `POSITION_SEGMENT_02_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 75, 22, 23, 76 | none recorded |
| `0x006C` | `POSITION_SEGMENT_03_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 75, 22, 23, 76 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x006E` | `POSITION_SEGMENT_03_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x006F` | `POSITION_SEGMENT_03_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0070` | `POSITION_SEGMENT_03_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0071` | `POSITION_SEGMENT_03_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 75, 22, 23, 76 | none recorded |
| `0x0072` | `POSITION_SEGMENT_04_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 75, 22, 23, 76 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0074` | `POSITION_SEGMENT_04_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0075` | `POSITION_SEGMENT_04_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0076` | `POSITION_SEGMENT_04_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0077` | `POSITION_SEGMENT_04_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 75, 22, 23, 76 | none recorded |
| `0x0078` | `POSITION_SEGMENT_05_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 75, 22, 23, 76 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x007A` | `POSITION_SEGMENT_05_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x007B` | `POSITION_SEGMENT_05_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x007C` | `POSITION_SEGMENT_05_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x007D` | `POSITION_SEGMENT_05_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 75, 22, 23, 76 | none recorded |
| `0x007E` | `POSITION_SEGMENT_06_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 75, 22, 23, 76 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0080` | `POSITION_SEGMENT_06_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0081` | `POSITION_SEGMENT_06_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0082` | `POSITION_SEGMENT_06_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0083` | `POSITION_SEGMENT_06_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 75, 22, 23, 76 | none recorded |
| `0x0084` | `POSITION_SEGMENT_07_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 75, 22, 23, 76 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0086` | `POSITION_SEGMENT_07_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0087` | `POSITION_SEGMENT_07_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0088` | `POSITION_SEGMENT_07_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0089` | `POSITION_SEGMENT_07_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 75, 22, 23, 76 | none recorded |
| `0x008A` | `POSITION_SEGMENT_08_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 75, 22, 23, 76 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x008C` | `POSITION_SEGMENT_08_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x008D` | `POSITION_SEGMENT_08_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x008E` | `POSITION_SEGMENT_08_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 75, 22, 23, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x008F` | `POSITION_SEGMENT_08_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 75, 22, 23, 76 | none recorded |
| `0x0090` | `POSITION_SEGMENT_09_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 76, 22, 23, 75 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0092` | `POSITION_SEGMENT_09_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0093` | `POSITION_SEGMENT_09_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0094` | `POSITION_SEGMENT_09_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0095` | `POSITION_SEGMENT_09_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 76, 22, 23, 75 | none recorded |
| `0x0096` | `POSITION_SEGMENT_10_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 76, 22, 23, 75 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x0098` | `POSITION_SEGMENT_10_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0099` | `POSITION_SEGMENT_10_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x009A` | `POSITION_SEGMENT_10_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x009B` | `POSITION_SEGMENT_10_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 76, 22, 23, 75 | none recorded |
| `0x009C` | `POSITION_SEGMENT_11_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 76, 22, 23, 75 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x009E` | `POSITION_SEGMENT_11_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x009F` | `POSITION_SEGMENT_11_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00A0` | `POSITION_SEGMENT_11_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00A1` | `POSITION_SEGMENT_11_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 76, 22, 23, 75 | none recorded |
| `0x00A2` | `POSITION_SEGMENT_12_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 76, 22, 23, 75 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00A4` | `POSITION_SEGMENT_12_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00A5` | `POSITION_SEGMENT_12_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00A6` | `POSITION_SEGMENT_12_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00A7` | `POSITION_SEGMENT_12_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 76, 22, 23, 75 | none recorded |
| `0x00A8` | `POSITION_SEGMENT_13_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 76, 22, 23, 75 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00AA` | `POSITION_SEGMENT_13_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00AB` | `POSITION_SEGMENT_13_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00AC` | `POSITION_SEGMENT_13_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00AD` | `POSITION_SEGMENT_13_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 76, 22, 23, 75 | none recorded |
| `0x00AE` | `POSITION_SEGMENT_14_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 76, 22, 23, 75 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00B0` | `POSITION_SEGMENT_14_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00B1` | `POSITION_SEGMENT_14_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00B2` | `POSITION_SEGMENT_14_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00B3` | `POSITION_SEGMENT_14_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 76, 22, 23, 75 | none recorded |
| `0x00B4` | `POSITION_SEGMENT_15_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 76, 22, 23, 75 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00B6` | `POSITION_SEGMENT_15_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00B7` | `POSITION_SEGMENT_15_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00B8` | `POSITION_SEGMENT_15_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00B9` | `POSITION_SEGMENT_15_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 76, 22, 23, 75 | none recorded |
| `0x00BA` | `POSITION_SEGMENT_16_PULSES` | 2 | RW | pulse | -0xFFFFFFF to 0xFFFFFFF | not separately stated; first-segment template: high 0; low 5000 | 76, 22, 23, 75 | SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00BC` | `POSITION_SEGMENT_16_SPEED` | 1 | RW/S | r/min (documented) | 0-3000 r/min | not separately stated; first-segment template: 120 (0/min) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00BD` | `POSITION_SEGMENT_16_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00BE` | `POSITION_SEGMENT_16_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 22, 23, 75 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00BF` | `POSITION_SEGMENT_16_RESERVED` | 1 | RESERVED (source RW/S) | reserved | 0 | not separately stated; first-segment template: 0 | 76, 22, 23, 75 | none recorded |
| `0x00C0` | `SPEED_SEGMENT_01_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | 100 (0/min) | 76, 23, 24, 77 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00C1` | `SPEED_SEGMENT_01_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00C2` | `SPEED_SEGMENT_01_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00C3` | `SPEED_SEGMENT_02_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 76, 23, 24, 77 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00C4` | `SPEED_SEGMENT_02_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00C5` | `SPEED_SEGMENT_02_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00C6` | `SPEED_SEGMENT_03_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 76, 23, 24, 77 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00C7` | `SPEED_SEGMENT_03_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00C8` | `SPEED_SEGMENT_03_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00C9` | `SPEED_SEGMENT_04_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 76, 23, 24, 77 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00CA` | `SPEED_SEGMENT_04_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00CB` | `SPEED_SEGMENT_04_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00CC` | `SPEED_SEGMENT_05_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 76, 23, 24, 77 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00CD` | `SPEED_SEGMENT_05_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00CE` | `SPEED_SEGMENT_05_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00CF` | `SPEED_SEGMENT_06_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 76, 23, 24, 77 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00D0` | `SPEED_SEGMENT_06_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00D1` | `SPEED_SEGMENT_06_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00D2` | `SPEED_SEGMENT_07_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 76, 23, 24, 77 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00D3` | `SPEED_SEGMENT_07_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00D4` | `SPEED_SEGMENT_07_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00D5` | `SPEED_SEGMENT_08_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 76, 23, 24, 77 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00D6` | `SPEED_SEGMENT_08_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00D7` | `SPEED_SEGMENT_08_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 76, 23, 24, 77 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00D8` | `SPEED_SEGMENT_09_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 77, 23, 24, 76 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00D9` | `SPEED_SEGMENT_09_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00DA` | `SPEED_SEGMENT_09_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00DB` | `SPEED_SEGMENT_10_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 77, 23, 24, 76 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00DC` | `SPEED_SEGMENT_10_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00DD` | `SPEED_SEGMENT_10_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00DE` | `SPEED_SEGMENT_11_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 77, 23, 24, 76 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00DF` | `SPEED_SEGMENT_11_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00E0` | `SPEED_SEGMENT_11_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00E1` | `SPEED_SEGMENT_12_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 77, 23, 24, 76 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00E2` | `SPEED_SEGMENT_12_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00E3` | `SPEED_SEGMENT_12_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00E4` | `SPEED_SEGMENT_13_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 77, 23, 24, 76 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00E5` | `SPEED_SEGMENT_13_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00E6` | `SPEED_SEGMENT_13_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00E7` | `SPEED_SEGMENT_14_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 77, 23, 24, 76 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00E8` | `SPEED_SEGMENT_14_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00E9` | `SPEED_SEGMENT_14_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00EA` | `SPEED_SEGMENT_15_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 77, 23, 24, 76 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00EB` | `SPEED_SEGMENT_15_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00EC` | `SPEED_SEGMENT_15_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00ED` | `SPEED_SEGMENT_16_SPEED` | 1 | RW/S | r/min (documented) | -3000 -3000 r/min (as printed) | not separately stated; first-segment template: 100 (0/min) | 77, 23, 24, 76 | RANGE_MALFORMED, DEFAULT_CONFLICT, SIGNED_ENCODING_UNRESOLVED, SCALE_UNRESOLVED |
| `0x00EE` | `SPEED_SEGMENT_16_ACCELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x00EF` | `SPEED_SEGMENT_16_DECELERATION_TIME` | 1 | RW/S | ms (documented) | 0-2000 ms | not separately stated; first-segment template: 50 (0 ms) | 77, 23, 24, 76 | DEFAULT_CONFLICT, SCALE_UNRESOLVED |
| `0x0100` | `CONTROL_ALGORITHM` | 1 | RW/S | unspecified | 1-2 | 2 | 77 | none recorded |
| `0x0101` | `ENCODER_RESOLUTION` | 1 | RW/S | counts (four times encoder line count) | 0-65535 | 4000 | 77 | none recorded |
| `0x0102` | `MAXIMUM_CURRENT` | 1 | RW/S | mA | 0-5600 | 5600/2200 | 77 | DEFAULT_CONFLICT, MODEL_APPLICABILITY |
| `0x0103` | `CLOSED_LOOP_CURRENT_PERCENT` | 1 | RW/S | percent | 0-150 | 100 | 77 | SOURCE_CONFLICT |
| `0x0104` | `BASE_CURRENT_PERCENT` | 1 | RW/S | percent | 0-75 | 40 | 78 | SOURCE_CONFLICT |
| `0x0105` | `OPEN_LOOP_CURRENT_PERCENT` | 1 | RW/S | percent | 0-100 | 100 | 78 | SOURCE_CONFLICT |
| `0x0106` | `LOCK_CURRENT_PERCENT` | 1 | RW/S | percent | 0-100 | 100 | 78 | SOURCE_CONFLICT |
| `0x0107` | `LOCK_TIME` | 1 | RW/S | ms | 0-20000 | 4000 | 78 | none recorded |
| `0x0108` | `INPUT_FILTER` | 1 | RW/S | coefficient | 0-65535 | 2 | 78 | none recorded |
| `0x0109` | `PULSE_LOW_PASS_FILTER` | 1 | RW/S | coefficient | 0-1024 | 5 | 78 | none recorded |
| `0x010A` | `POSITION_ERROR_ALARM_THRESHOLD` | 1 | RW/S | unspecified | 1-65535 | 4000 | 78 | UNIT_UNSPECIFIED |
| `0x010B` | `POSITION_ARRIVAL_WINDOW` | 1 | RW/S | unspecified | 1-256 | 5 | 78 | UNIT_UNSPECIFIED |
| `0x010C` | `ARRIVAL_TIME` | 1 | RW/S | unspecified | 0-200 | 10 | 78 | UNIT_UNSPECIFIED |
| `0x010D` | `PULSE_MEAN_FILTER` | 1 | RW/S | coefficient | 0-512 | 512 | 78 | none recorded |
| `0x010E` | `CURRENT_LOOP_KP_MULTIPLIER` | 1 | RW/S | gain | 0-65535 | 4096 | 78 | none recorded |
| `0x010F` | `CURRENT_LOOP_KP` | 1 | RW/S | unspecified | 0-65535 | 1024 | 79 | UNIT_UNSPECIFIED |
| `0x0110` | `CURRENT_LOOP_KI` | 1 | RW/S | unspecified | 0-65535 | 28 | 79 | UNIT_UNSPECIFIED |
| `0x0111` | `CURRENT_LOOP_KC` | 1 | RW/S | unspecified | 0-65535 | 1228 | 79 | UNIT_UNSPECIFIED |
| `0x0112` | `LA_SPEED_KP1` | 1 | RW/S | unspecified | 0-65535 | 10 | 79 | UNIT_UNSPECIFIED |
| `0x0113` | `LA_SPEED_KV1` | 1 | RW/S | unspecified | 0-65535 | 32 | 79 | UNIT_UNSPECIFIED |
| `0x0114` | `LA_SPEED_NODE1` | 1 | RW/S | unspecified | 0-65535 | 320 | 79 | UNIT_UNSPECIFIED |
| `0x0115` | `LA_SPEED_KP2` | 1 | RW/S | unspecified | 0-65535 | 15 | 79 | UNIT_UNSPECIFIED |
| `0x0116` | `LA_SPEED_KV2` | 1 | RW/S | unspecified | 0-65535 | 33 | 79 | UNIT_UNSPECIFIED |
| `0x0117` | `LA_SPEED_NODE2` | 1 | RW/S | unspecified | 0-65535 | 320 | 79 | UNIT_UNSPECIFIED |
| `0x0118` | `LA_SPEED_FEEDFORWARD_KVF` | 1 | RW/S | unspecified | 0-65535 | 20 | 79 | UNIT_UNSPECIFIED |
| `0x0119` | `LA_POSITION_KI` | 1 | RW/S | unspecified | 0-65535 | 35 | 79 | UNIT_UNSPECIFIED |
| `0x0122` | `COLLISION_THRESHOLD_0122` | 1 | RW/S | position error; unit unspecified | 200-4000 | 200 | 79 | DUPLICATE_MAPPING, UNIT_UNSPECIFIED |
| `0x0123` | `COLLISION_CURRENT_0123` | 1 | RW/S | percent | 20-100 | 50 | 79 | DUPLICATE_MAPPING |
| `0x0130` | `SEGMENT_START_SPEED_01_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0131` | `SEGMENT_START_SPEED_02_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0132` | `SEGMENT_START_SPEED_03_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0133` | `SEGMENT_START_SPEED_04_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0134` | `SEGMENT_START_SPEED_05_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0135` | `SEGMENT_START_SPEED_06_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0136` | `SEGMENT_START_SPEED_07_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0137` | `SEGMENT_START_SPEED_08_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0138` | `SEGMENT_START_SPEED_09_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x0139` | `SEGMENT_START_SPEED_10_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x013A` | `SEGMENT_START_SPEED_11_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x013B` | `SEGMENT_START_SPEED_12_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x013C` | `SEGMENT_START_SPEED_13_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x013D` | `SEGMENT_START_SPEED_14_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x013E` | `SEGMENT_START_SPEED_15_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |
| `0x013F` | `SEGMENT_START_SPEED_16_VALUE` | 1 | RW/S | rpm (documented) | -180 to180 rpm | 0 rpm | 77 | SIGNED_ENCODING_UNRESOLVED |

## Undocumented address holes

Within the appendix span 0x0000-0x013F, the following holes have no ESS
entry. They are not declared readable, writable or reserved by inference.

78 undocumented words in 15 intervals, separately audited against the
appendix. These are distinct from 16 explicitly reserved words and
the documented 0x003B/0x003C entries with unspecified access.

| First | Last | PDF pages | Evidence |
| --- | --- | --- | --- |
| `0x0004` | `0x0005` | 68 | The read-only table skips these addresses between DIP status and error code. |
| `0x000D` | `0x000F` | 69 | No entries between current speed and the basic-control group. |
| `0x0012` | `0x0012` | 69 | No entry between subdivision and custom node. |
| `0x0016` | `0x0016` | 69, 70 | No entry between serial format and over-limit stop. |
| `0x001A` | `0x001C` | 70 | No entries between word order and jog speed. |
| `0x0026` | `0x0026` | 70 | No entry between the position pulse pair and motion command. |
| `0x0028` | `0x002C` | 70, 71 | No entries between motion command and auxiliary command. |
| `0x002E` | `0x002F` | 71, 72 | No entries between auxiliary command and homing auxiliary setting. |
| `0x003D` | `0x003F` | 73 | No entries between collision current and input polarity. |
| `0x0045` | `0x004A` | 74 | Generic prose mentions input functions 0x0045-0x0047, but the ESS appendix and hardware expose only X0-X3. None of this interval has an ESS descriptor. |
| `0x004E` | `0x004E` | 74 | Generic prose mentions a third output function, but the ESS appendix and hardware expose only Y0-Y1. |
| `0x0052` | `0x005F` | 75 | No entries between PV trigger mode and the first position segment. |
| `0x00F0` | `0x00FF` | 77 | No entries between the last speed segment and the performance group. |
| `0x011A` | `0x0121` | 79 | No entries between LA position Ki and the second collision-threshold row. |
| `0x0124` | `0x012F` | 77, 79 | No entries between the second collision-current row and segment starting speeds. |

The generator requires documented words and audited gaps to cover the
entire 0x0000-0x013F span without overlap. Its private 320-byte access
map lets the codec reject gaps without linking catalogue descriptions.

Generic references outside the ESS map:

- `0x0045-0x0047` (pages 13, 14, 17, 18, 20, 22, 24, 28): Generic input prose extends beyond ESS X0-X3; no ESS descriptors.
- `0x004E` (pages 28): Generic third output beyond ESS Y0-Y1; no ESS descriptor.
- `0x2042` (pages 77, 78): Current-percentage prose references outside-map base-current address; no guessed alias.

## Named values and bitfields

Bit enums contain masks; other enums contain semantic native choices.
These declarations do not validate arbitrary bit combinations or implement
operations. Preserve unknown raw values rather than inventing enum labels.

### `WordOrder`

Does not change Modbus byte order within a 16-bit register.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `HIGH_WORD_FIRST` | 0 | Lower address stores high 16 bits. | 30, 70 |
| `LOW_WORD_FIRST` | 1 | Lower address stores low 16 bits. | 30, 70 |

### `DefaultDirection`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `NORMAL` | 0 | Default movement direction. | 69 |
| `REVERSED` | 1 | Reverse default movement direction. | 69 |

### `BaudRateCode`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `BAUD_115200` | 0 | 115200 baud; power cycle required after changing. | 69 |
| `BAUD_38400` | 1 | 38400 baud; power cycle required after changing. | 69 |
| `BAUD_19200` | 2 | 19200 baud; power cycle required after changing. | 69 |
| `BAUD_9600` | 3 | 9600 baud; power cycle required after changing. | 69 |

### `SerialFormatCode`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `FORMAT_8N1` | 0 | 8N1; power cycle required after changing. | 69 |
| `FORMAT_8N2` | 1 | 8N2; power cycle required after changing. | 69 |
| `FORMAT_8E1` | 2 | 8E1; power cycle required after changing. | 69 |
| `FORMAT_8O1` | 3 | 8O1; power cycle required after changing. | 69 |

### `OverLimitStop`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `FREE_PARKING` | 0 | Source term; stopping/torque semantics unresolved. | 70 |
| `EMERGENCY_STOP` | 1 | Emergency stop selection. | 70 |

### `SoftLimitEnable`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `LIMITS_OFF` | 0 | Software limits disabled. | 29, 70 |
| `AFTER_HOMING` | 1 | Limits effective only after returning to zero. | 29, 70 |

### `PositionMode`

Used for external position mode register; motion-command bit 2 is a separate mask.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `RELATIVE` | 0 | Relative target. | 16, 75 |
| `ABSOLUTE` | 1 | Absolute target. | 16, 75 |

### `PvTriggerMode`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `LEVEL` | 0 | Active level trigger. | 29, 75 |
| `RISING_EDGE` | 1 | Rising-edge trigger; needed for mixed PT/PV fixed-length operation. | 29, 75 |

### `ControlAlgorithm`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `OPEN_LOOP` | 1 | Open-loop operation. | 77 |
| `ALGORITHM_1` | 2 | Closed-loop algorithm 1 per source. | 77 |

### `MotionCommandBit`

Bit masks only; combinations and physical completion are not validated. Unnamed bits remain unknown.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `START_POSITION` | 1 | Generate position start. | 13, 14, 16, 18, 19, 25, 70, 71 |
| `START_SPEED` | 2 | Generate serial speed start. | 13, 14, 16, 18, 19, 25, 70, 71 |
| `ABSOLUTE_POSITION` | 4 | 1: absolute; 0: relative. | 13, 14, 16, 18, 19, 25, 70, 71 |
| `INTERRUPT_POSITION` | 8 | 1: interrupt current positioning; 0: ignore new positioning command while busy. Valid only positioning per p16. | 13, 14, 16, 18, 19, 25, 70, 71 |
| `START_HOMING` | 16 | Generate homing start. | 13, 14, 16, 18, 19, 25, 70, 71 |
| `STOP` | 256 | Decelerate with preconfigured ramp. | 13, 14, 16, 18, 19, 25, 70, 71 |
| `EMERGENCY_STOP` | 512 | Source says direct stop without deceleration. | 13, 14, 16, 18, 19, 25, 70, 71 |

### `AuxiliaryCommand`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `INVALID` | 0 | No valid action. | 26, 71 |
| `RELEASE` | 17 | Release motor. | 26, 71 |
| `ENABLE` | 18 | Enable motor. | 26, 71 |
| `CLEAR_ALARM` | 33 | Clear a resettable alarm. | 26, 71 |
| `CLEAR_POSITION` | 49 | Clear current motor position. | 26, 71 |
| `RESTORE_FACTORY` | 65 | Restore factory parameters; ignored unless stopped. | 26, 71 |
| `SAVE_PARAMETERS` | 66 | Save all parameters; ignored unless stopped. | 26, 71 |

### `MotionStatusBit`

Bits 7-15 are undocumented, not normalized to false.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `IN_POSITION` | 1 | 1: in position; 0: not in position. | 68 |
| `HOMING_COMPLETE` | 2 | 1: homing complete; 0: not complete. | 68 |
| `RUNNING` | 4 | 1: running; 0: stationary. | 68 |
| `ALARM` | 8 | 1: alarm; 0: normal. | 68 |
| `RELEASED` | 16 | 1: released; 0: enabled; source label Motor enable bit has inverted polarity. | 68 |
| `POSITIVE_SOFT_LIMIT` | 32 | 1: positive limit overtravel flag valid. | 68 |
| `NEGATIVE_SOFT_LIMIT` | 64 | 1: negative limit overtravel flag valid. | 68 |

### `InputBit`

Bits 4-15 are reserved in corresponding ESS registers.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `X0` | 1 | Input X0; bit semantics depend on status versus polarity register. | 68, 69, 73 |
| `X1` | 2 | Input X1; bit semantics depend on status versus polarity register. | 68, 69, 73 |
| `X2` | 4 | Input X2; bit semantics depend on status versus polarity register. | 68, 69, 73 |
| `X3` | 8 | Input X3; bit semantics depend on status versus polarity register. | 68, 69, 73 |

### `OutputBit`

Bits 2-15 are reserved in corresponding ESS registers.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `Y0` | 1 | Output Y0; bit semantics depend on status, polarity or custom-output register. | 69, 74 |
| `Y1` | 2 | Output Y1; bit semantics depend on status, polarity or custom-output register. | 69, 74 |

### `DipStatusBit`

The function manual lists 7 switches; ESS23 hardware p8 defines 5. These names record source labels, not confirmed board mapping.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `SOURCE_SW1` | 1 | Source labels bit 0 as SW1; hardware mapping unqualified. | 68 |
| `SOURCE_SW2` | 2 | Source labels bit 1 as SW2; hardware mapping unqualified. | 68 |
| `SOURCE_SW3` | 4 | Source labels bit 2 as SW3; hardware mapping unqualified. | 68 |
| `SOURCE_SW4` | 8 | Source labels bit 3 as SW4; hardware mapping unqualified. | 68 |
| `SOURCE_SW5` | 16 | Source labels bit 4 as SW5; hardware mapping unqualified. | 68 |
| `SOURCE_SW6` | 32 | Source labels bit 5 as SW6; hardware mapping unqualified. | 68 |
| `SOURCE_SW7` | 64 | Source labels bit 6 as SW7; hardware mapping unqualified. | 68 |

### `InputFunction`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `UNDEFINED` | 0 | No function. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `ORIGIN` | 1 | Origin sensor. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `POSITIVE_LIMIT` | 2 | Positive-limit sensor. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `NEGATIVE_LIMIT` | 3 | Negative-limit sensor. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `RELEASE_MOTOR` | 4 | Valid level releases motor. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `STOP` | 5 | Valid level forces stop. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `EMERGENCY_STOP` | 6 | Valid level forces urgent stop. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `POSITION_MOVE` | 7 | Position start; edge valid. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `SPEED_MOVE` | 8 | Speed mode; level valid; speed sign sets direction. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `JOG_POSITIVE` | 9 | Level-valid motion always positive regardless speed sign. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `JOG_NEGATIVE` | 10 | Level-valid motion always negative regardless speed sign. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `START_HOMING` | 11 | Homing start; edge valid. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `PT_TRIGGER` | 12 | Position-segment trigger; edge valid. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `PV_TRIGGER` | 13 | Speed-segment trigger; level or configured rising edge. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `SEGMENT_PIN0` | 14 | Least significant segment selector. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `SEGMENT_PIN1` | 15 | Segment selector bit 1. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `SEGMENT_PIN2` | 16 | Segment selector bit 2. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |
| `SEGMENT_PIN3` | 17 | Most significant segment selector. | 14, 18, 19, 22, 23, 24, 27, 73, 74 |

### `OutputFunction`

Values 6-8 lack descriptions and are deliberately not enum members.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `UNDEFINED` | 0 | No function. | 28, 74 |
| `ALARM` | 1 | Valid level when alarm. | 28, 74 |
| `RUNNING` | 2 | Valid level when drive moves. | 28, 74 |
| `HOMING_COMPLETE` | 3 | Valid level after homing. | 28, 74 |
| `IN_POSITION` | 4 | Valid level when correct position reached. | 28, 74 |
| `BRAKE` | 5 | Valid when motor locked; invalid when released. | 28, 74 |
| `CUSTOM_0` | 9 | Custom output controlled through 0x004F. | 28, 74 |
| `CUSTOM_1` | 10 | Custom output controlled through 0x004F. | 28, 74 |
| `CUSTOM_2` | 11 | Listed choice; mapping to only two ESS outputs unresolved. | 28, 74 |

### `HomingAuxiliary`

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `MOVE_OFFSET_SET_OFFSET` | 0 | Run offset; resulting coordinate is offset value. | 72 |
| `MOVE_OFFSET_SET_ZERO` | 1 | Run offset; resulting coordinate is zero. | 72 |
| `MOVE_OFFSET_SET_NEGATIVE_OFFSET` | 2 | Run offset; resulting coordinate is negative offset value. | 72 |
| `MOVE_OFFSET_KEEP_ACTUAL` | 3 | Run offset; resulting coordinate is actual value. | 72 |
| `MOVE_OFFSET_ADD_OFFSET` | 4 | Run offset; resulting coordinate is actual value plus offset. | 72 |
| `MOVE_OFFSET_SUBTRACT_OFFSET` | 5 | Run offset; resulting coordinate is actual value minus offset. | 72 |
| `KEEP_POSITION_SET_OFFSET` | 6 | Do not run offset; resulting coordinate is offset value. | 72 |
| `KEEP_POSITION_SET_ZERO` | 7 | Do not run offset; resulting coordinate is zero. | 72 |
| `KEEP_POSITION_SET_NEGATIVE_OFFSET` | 8 | Do not run offset; resulting coordinate is negative offset value. | 72 |
| `KEEP_POSITION_KEEP_ACTUAL` | 9 | Do not run offset; resulting coordinate is actual value. | 72 |
| `KEEP_POSITION_ADD_OFFSET` | 10 | Do not run offset; resulting coordinate is actual value plus offset. | 72 |
| `KEEP_POSITION_SUBTRACT_OFFSET` | 11 | Do not run offset; resulting coordinate is actual value minus offset. | 72 |

### `HomingMethod`

Semantic method numbers, NOT a negative-value wire encoder. Modes 1-14, 33, 34 require closed-loop motor Z index. Modes 15, 16, 31, 32, 0 are not listed.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `COLLISION_MINUS_4` | -4 | Collision mode listed in ESS table; trajectory and negative wire encoding unresolved. | 72, 73 |
| `COLLISION_MINUS_3` | -3 | Collision mode listed in ESS table; trajectory and negative wire encoding unresolved. | 72, 73 |
| `COLLISION_MINUS_2` | -2 | Collision mode listed in ESS table; trajectory and negative wire encoding unresolved. | 72, 73 |
| `COLLISION_MINUS_1` | -1 | Collision mode listed in ESS table; trajectory and negative wire encoding unresolved. | 72, 73 |
| `METHOD_1` | 1 | Method 1: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 32 |
| `METHOD_2` | 2 | Method 2: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 33 |
| `METHOD_3` | 3 | Method 3: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 34 |
| `METHOD_4` | 4 | Method 4: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 34, 35 |
| `METHOD_5` | 5 | Method 5: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 35, 36 |
| `METHOD_6` | 6 | Method 6: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 36, 37 |
| `METHOD_7` | 7 | Method 7: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 37, 38, 39 |
| `METHOD_8` | 8 | Method 8: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 39, 40 |
| `METHOD_9` | 9 | Method 9: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 40, 41, 42 |
| `METHOD_10` | 10 | Method 10: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 42, 43 |
| `METHOD_11` | 11 | Method 11: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 44, 45 |
| `METHOD_12` | 12 | Method 12: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 45, 46, 47 |
| `METHOD_13` | 13 | Method 13: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 47, 48, 49 |
| `METHOD_14` | 14 | Method 14: reference motor Z index; use cited diagrams for starting-state-dependent paths. | 49, 50 |
| `METHOD_17` | 17 | Method 17: reference negative limit; use cited diagrams for starting-state-dependent paths. | 50, 51 |
| `METHOD_18` | 18 | Method 18: reference positive limit; use cited diagrams for starting-state-dependent paths. Figure labels negative limit conflict with positive-limit prose; retain discrepancy. | 51 |
| `METHOD_19` | 19 | Method 19: reference origin input; use cited diagrams for starting-state-dependent paths. | 52 |
| `METHOD_20` | 20 | Method 20: reference origin input; use cited diagrams for starting-state-dependent paths. | 52, 53 |
| `METHOD_21` | 21 | Method 21: reference origin input; use cited diagrams for starting-state-dependent paths. | 53, 54 |
| `METHOD_22` | 22 | Method 22: reference origin input; use cited diagrams for starting-state-dependent paths. | 54, 55 |
| `METHOD_23` | 23 | Method 23: reference origin input; use cited diagrams for starting-state-dependent paths. | 55, 56, 57 |
| `METHOD_24` | 24 | Method 24: reference origin input; use cited diagrams for starting-state-dependent paths. | 57, 58 |
| `METHOD_25` | 25 | Method 25: reference origin input; use cited diagrams for starting-state-dependent paths. | 58, 59, 60 |
| `METHOD_26` | 26 | Method 26: reference origin input; use cited diagrams for starting-state-dependent paths. | 60, 61 |
| `METHOD_27` | 27 | Method 27: reference origin input; use cited diagrams for starting-state-dependent paths. | 61, 62, 63 |
| `METHOD_28` | 28 | Method 28: reference origin input; use cited diagrams for starting-state-dependent paths. | 63, 64 |
| `METHOD_29` | 29 | Method 29: reference origin input; use cited diagrams for starting-state-dependent paths. | 64, 65, 66 |
| `METHOD_30` | 30 | Method 30: reference origin input; use cited diagrams for starting-state-dependent paths. | 66, 67 |
| `METHOD_33` | 33 | Method 33: negative low-speed search for motor Z index; no slowdown input. | 67 |
| `METHOD_34` | 34 | Method 34: positive low-speed search for motor Z index; no slowdown input. | 67 |
| `METHOD_35` | 35 | Method 35: source says set current position as origin; not a sensor-search motion. | 67 |

### `AlarmCode`

Labels for 1, 2, 3, 5 additionally from hardware manual p12; no label invented for 4. Unknown values must be retained.

| Name | Value | Meaning | Function PDF pages |
| --- | ---: | --- | --- |
| `NORMAL` | 0 | Function manual p68 normal. | 68 |
| `OVERCURRENT` | 1 | Hardware p12 phase overcurrent or short; re-power reset. | 68 |
| `OVERVOLTAGE` | 2 | Hardware p12 supply voltage high; automatic reset. | 68 |
| `UNDERVOLTAGE` | 3 | Hardware p12 supply voltage low; automatic reset. | 68 |
| `POSITION_TOLERANCE` | 5 | Hardware p12 out of tolerance; re-power reset. | 68 |

## Verification boundary

The native catalogue tests check address coverage, access separation,
paired lookup, indexed bounds, selected independent manual values,
homing gaps and unresolved metadata. Generator checking prevents stale
code/doc tables. These are software/source checks, not protocol,
persistence, physical motion or stopping qualification.
