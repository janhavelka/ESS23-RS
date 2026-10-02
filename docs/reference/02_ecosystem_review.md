# RS485 ecosystem review

Reviewed on 2026-10-02 to establish the ESS23-RS architecture before code is
implemented. Findings below describe inspected local source; ESS decisions
are in [architecture.md](../architecture.md) and the
[CLI contract](../cli_contract.md). This review does not qualify sibling or
ESS hardware behavior.

## Source scope and provenance

The direct standalone RS485 peers found in the current Projects directory
are SHZK-PT, VTN4xx and VibWire-108. FieldCore-node is the intended application
consumer. EE871-E2 uses GPIO E2, and LGClimateLink is UART/LIN application
firmware; neither defines the RS485 codec boundary. Archived projects were
excluded from the current baseline.

| Local repository | Inspected HEAD | Package version |
| --- | --- | --- |
| SHZK-PT | `361c47c0588fe967e9dc5507ce7a3d85a2f0eb6a` | `3.0.0` |
| VTN4xx | `6cc143307c824928c33e646fa6fd515403b97f98` | `3.0.0` |
| VibWire-108 | `9365df6a51800755f417a1fa251088ee42be9dc0` | `1.2.0` |
| FieldCore-node | `450e2b8272fb4a2d89af87ac5b37044fcf170acb` | Application, not a codec package |

Inspection used current working trees. The three codec repositories reported
no changes; FieldCore contained unrelated settings-transfer edits, which were
left untouched. Git reported an unreadable global ignore file. No sibling was
fetched, updated, built or edited. The links below navigate adjacent local
checkouts; they are not dependencies or portable links in a standalone clone.
Symbols are included so findings remain searchable as line numbers change.

## Core layout and names

All three peers use public `include/<namespace>/` headers, implementation in
`src/`, and example-only hardware helpers under `examples/common/`. Main APIs
are namespace free functions, not transport-owning device objects. They
build bounded frames into caller buffers and parse supplied replies without
performing I/O. `Config.h` contains protocol definitions; its name does not
imply a core runtime configuration/lifecycle object.

| Area | SHZK-PT | VTN4xx | VibWire-108 |
| --- | --- | --- | --- |
| Namespace and umbrella | `SHZK_PT`, `SHZK_PT.h` | `VTN4XX`, `VTN4XX.h` | `VibWire`, `VibWire.h` |
| Generic build | `buildReadRegisters`, `buildWriteSingleRegister`, `buildWriteMultipleRegisters` | `buildReadRegisters`, `buildWriteSingleRegister` | `buildReadRegisters` for FC04 |
| Generic parse | `parseRegister`, `parseRegisters`, `parseWriteSingleRegister` plus older checked names | `parseRegister`, `parseRegisters`, `parseWriteSingleRegister` | `parseRegister`, `parseRegisters`, `parseRegistersExact` |
| Request-specific read check | `validateReadResponseExpected` | Typed parsers add requested quantity checks | `parseRegistersExact` |
| Common utilities | `calcCrc16`, `expectedReadRegistersLen`, `isValidAddress` | Same naming | Same naming |
| Builder result | Length, zero on error | Length, zero on error | Length, zero on error, plus `validateReadRequest` |

Evidence: [SHZK public API](../../../SHZK-PT/include/SHZK_PT/SHZK_PT.h),
[VTN public API](../../../VTN4xx/include/VTN4XX/VTN4XX.h),
[VibWire public API](../../../VibWire-108/include/VibWire/VibWire.h).
SHZK's long generic names wrap its older `buildReadRegs`/`buildWriteReg`
names. ESS can use the long names directly without copying historical aliases.

## Error and output contracts differ

Every peer has `Status { Err code; int32_t detail; const char* msg; }`,
`isOk()`, namespace `Ok()`, static messages and `errToString()`. Their enums
and convenience methods are not interchangeable.

| Detail | SHZK-PT | VTN4xx and VibWire |
| --- | --- | --- |
| Enum storage | `uint16_t` | `uint8_t` |
| CRC label | `CRC_ERROR` | `CRC` |
| Frame labels | `FRAME_ERROR` | `BAD_LEN`, `BAD_RESP` |
| `ok()` | Boolean instance query | Static success factory |
| Boolean conversion | Explicit | Implicit |
| Extra errors | Reserved transport/application categories | Transport/application factories also present |

Evidence: [SHZK Status](../../../SHZK-PT/include/SHZK_PT/Status.h),
[VTN Status](../../../VTN4xx/include/VTN4XX/Status.h),
[VibWire Status](../../../VibWire-108/include/VibWire/Status.h).
ESS adopts the shared shape and `isOk()`, with one explicit error vocabulary.
Integration maps categories by name/meaning rather than casting enum values.

SHZK's `validateReadResponseExpected` validates the exact requested count and
returns structured `ReadResponseError`. Its generic `parseRegsChecked`
capacity alone is not the expected request quantity. It preserves aggregate
temperature/array outputs but resets some scalar/count outputs. See
[SHZK implementation](../../../SHZK-PT/src/SHZK_PT.cpp), symbols
`validateFrame`, `validateReadResponseExpected`, `parseRegChecked`,
`parseRegsChecked`, `parseWriteSingleExpectedChecked` and
`parseWriteMultipleExpectedChecked`.

VTN's `validateResponse` checks the basic frame/CRC/address/function, while
typed parsers add exact normal shape checks. `parseRegisters` has no requested
count argument. Some typed paths, including `parseRtc`, `parseNtcBeta` and
`parseDacOutput`, assign an output before their final semantic error check.
See [VTN implementation](../../../VTN4xx/src/VTN4XX.cpp).

VibWire's `parseRegistersExact` checks requested quantity before publishing
arrays; output count resets to zero. Its `validateResponse` enforces exact
normal and exception framing. See
[VibWire implementation](../../../VibWire-108/src/VibWire.cpp), symbols
`parseRegistersInternal`, `validateResponse` and `parseRegistersExact`.

ESS deliberately chooses unchanged payload outputs on any error, a separately
reset count, and mandatory request expectations. This is a clear new contract,
not a claim that every sibling already guarantees it.

## Device constants are not ecosystem conventions

SHZK's generic reads allow 125 registers and writes allow 123; VTN caps reads
at 32 and validates its own map. VibWire uses FC04 and device-specific maps.
ESS's documented FC03 limit is 16. Do not import sibling register ranges,
word order, sentinels, polling periods or default addresses.

Buffer constants also need independent derivation. VTN's core
`MAX_FRAME_SIZE` is 64, while its example RX buffer is 160 and a 32-register
read response requires 69 bytes. A familiar constant name is not evidence
that it bounds every operation. See the peers' `Config.h` and
`examples/common/SimpleModbusRtu.h` files.

## Example transport and lifecycle

Each peer has example `SerialPortOps`, `GpioOps` and `ClockOps` callbacks and a
`SimpleModbus::Rtu` state machine. Its `begin`, `tick`, `startTransaction`,
result access, recovery and buffer lifetimes belong to the example layer.
Those APIs must not become methods of the ESS codec.

Useful behavior includes bounded work, actual TX-drain handling, DE release,
RTU silence, explicit RX discard, wrapping deadline arithmetic and retained
pending-TX/error interlocks. A cooperative caller does not make a blocking
adapter callback bounded. The platform adapter needs its own verified timing
contract.

The helpers are not interchangeable:

- [SHZK RTU](../../../SHZK-PT/examples/common/SimpleModbusRtu.h) supports FC10
  and larger frames, with bus-activity silence and TX-drain requirements.
  Its comments explicitly discuss delayed replies and FC06 echo ambiguity.
- [VTN RTU](../../../VTN4xx/examples/common/SimpleModbusRtu.h) uses 64-byte TX
  and 160-byte RX storage and FC03/04/06 handling.
- [VibWire RTU](../../../VibWire-108/examples/common/SimpleModbusRtu.h)
  `startTransaction` rejects anything other than an eight-byte FC04 request.
  Its helper retry default differs from its example's zero-retry default.

Motor requests require a reviewed write policy; a timeout after transmission
does not establish failure to execute. No sensor retry default is suitable
as an implicit motor command policy.

## CLI and health conventions

The common vocabulary includes `help`, `version`, `config`, `status`, `health`,
`stats`, `probe`/`ping`, `host`, `useaddr`, `readreg`, `recover`, `flush` and
diagnostic tracing. Host-only changes and device writes have separate command
names. Fixed input buffers, `drv` diagnostics, static error labels and optional
frame traces recur across examples.

SHZK's [Commands.inc](../../../SHZK-PT/examples/01_basic_bringup_cli/Commands.inc)
drives help/dispatch and generated documentation. VTN and VibWire have large
hand-written dispatchers. ESS should inherit the command-table approach, not
the size of those consoles.

Health is application-owned. SHZK exposes a caller-updated `Stats` convenience
type and caller-supplied timestamps; VTN/VibWire hold `RuntimeStats` in their
examples. None needs a core motor-like lifecycle or public health service.
VibWire's diagnostics use `[PASS]`, `[WARN]`, `[FAIL]`, `[INFO]` and print error
label/message/detail; these are presentation checks, not codec result enums.

| Observed command | Actual behavior |
| --- | --- |
| SHZK/VTN Arduino `status` | Performs active probe/register reads |
| VibWire Arduino `status` | Active fingerprint/counter reads |
| VibWire native IDF `status` | Cached host/transport snapshot |
| Arduino `health` in the three peers | Active connection/device checks |
| SHZK/VTN/VibWire Arduino `reset` | Clears local statistics |
| VibWire native IDF `reset` | Alias of transport recovery/flush |
| SHZK `factory` | Clears CLI NVS, not factory-resetting the device |

Evidence: `cmdStatus`, `cmdHealth`, `cmdReset` and help/dispatch in the
[SHZK console](../../../SHZK-PT/examples/01_basic_bringup_cli/main.cpp),
[VTN console](../../../VTN4xx/examples/01_basic_bringup_cli/src/main.cpp),
[VibWire console](../../../VibWire-108/examples/01_basic_bringup_cli/src/main.cpp),
and [VibWire IDF console](../../../VibWire-108/examples/espidf_basic/main/main.cpp).
The ESS CLI contract resolves these ambiguities explicitly; it does not claim
identical historical behavior.

## FieldCore ownership and application contracts

[Rs485Task](../../../FieldCore-node/include/TunnelMonitor/rs485/Rs485Task.h)
owns the backend/UART, DE/RE, framing, fixed buffers, one active transaction,
bounded queues/results, recovery and heartbeat. Private
[ShzkDeviceModule](../../../FieldCore-node/src/rs485/ShzkDeviceModule.cpp)
and [VibWireDeviceModule](../../../FieldCore-node/src/rs485/VibWireDeviceModule.cpp)
call codecs and own device sequences, retries, cached results and presence/
health decisions. Ready bindings are scheduled round-robin; a device wait
can release the bus for another binding.

[Rs485DeviceBinding.h](../../../FieldCore-node/include/TunnelMonitor/rs485/Rs485DeviceBinding.h)
defines `bind`, `applyRuntimeConfig`, `start`, `poll`, `takeStep`,
`acceptObservation`, `takeResult`, `setEnabled`, `cancel`, `onBusInvalidated`,
`snapshot`, `readingCatalog`, `copyStatus`. These are adapter methods, not
library requirements. `acceptObservation` borrows RX bytes for the duration of
the call. `copyStatus` is passive and does not consume results or run hardware.

Foreground/CLI code submits copied typed work and reads published snapshots;
the RS485 worker calls modules and hardware. Accepted work reserves a future
result. Product composition selects concrete bindings without a dynamic plugin
framework. See [runtime composition](../../../FieldCore-node/src/product/tunnelmonitor/TunnelMonitorRuntimeComposition.cpp)
and [RS485 runtime publication](../../../FieldCore-node/src/rs485/Rs485Diagnostics.cpp).

Application state has several distinct dimensions:

| Dimension | Current values |
| --- | --- |
| Admission | `Accepted`, `Busy`, `Disabled`, `Unsupported`, `Rejected` |
| Terminal device result | `Ok`, `Disabled`, `Absent`, `Timeout`, `Stale`, `Cancelled`, `Failed` |
| Bus | `Idle`, `Transmitting`, `WaitingResponse`, `WaitingDevice`, `Fault` |
| Presence | `Unknown`, `Present`, `Absent`, `Disabled` |
| Health | `Initializing`, `Ok`, `Degraded`, `Fault`, `Unknown`, `Disabled` |

See [DeviceMeasurement.h](../../../FieldCore-node/include/TunnelMonitor/contracts/DeviceMeasurement.h),
[Rs485Devices.h](../../../FieldCore-node/include/TunnelMonitor/contracts/Rs485Devices.h)
and [Health.h](../../../FieldCore-node/include/TunnelMonitor/contracts/Health.h).
SHZK counts zero-byte response timeouts as absence evidence; malformed/partial
responses preserve presence and degrade health. ESS should likewise distinguish
communication evidence, motor alarms and usable/fresh telemetry, with its own
application policy rather than copied sensor thresholds.

FieldCore CLI uses `rs485 status`, `timing`, `discover`, `recover`, `trace`,
`result <request_id>`, plus selected device `probe`, `read`, `read last`.
`discover` probes configured instances. Device operations and recovery submit
typed work; status/timing use snapshots and trace drains retained records.
No route exposes raw transactions. See
[Rs485Cli.cpp](../../../FieldCore-node/src/cli/owners/Rs485Cli.cpp) and
[Rs485DeviceCommand.h](../../../FieldCore-node/src/cli/components/Rs485DeviceCommand.h).

## FieldCore integration gaps

These are observed limits in the inspected code, not changes made by this
architecture task.

| Current evidence | Consequence for ESS integration |
| --- | --- |
| `Rs485Task::processResponse` strips a received prefix equal to TX as local echo | A valid FC06 acknowledgement is identical to TX and can be discarded on a no-echo bus. Define topology-aware handling and test no echo, local echo, split echo, acknowledgement and echo-only ambiguity. |
| `kRs485TransactionTxCapacity = 8` | FC06 fits; FC10 needs at least 13 bytes for a two-register write. Increase capacity deliberately when supported operations are known. |
| `rs485SerialFormatValid` accepts only 8N1; backend configures baud only | Other ESS formats need owner/backend work. Existing compatible baud settings alone do not establish complete serial-format support. |
| `responseComplete` supports expected fixed length or suffix | A five-byte exception for a longer expected reply waits for a transport timeout before module parsing. SHZK can still recognize it then; latency must be considered for motor operation. |
| `DeviceRequestKind` contains only `Probe`, `Measure`, `ReadLast` with no control payload | Add a typed application motor command contract; do not hide writes in measurement requests. |
| Device result readings are floats | Preserve exact integer positions/counters/bitfields in a suitable motor result contract; floats cannot represent every signed 32-bit position exactly. |
| Module `cancel`, `setEnabled`, bus recovery are administrative operations | They do not respectively stop, enable or reset a physical motor. |
| Existing sensor retry and fair scheduling policies | Classify retry safety per motor operation and define bounded handling of urgent stops without taking ownership away from the bus owner. |

Primary evidence: [Rs485OwnerTransaction.h](../../../FieldCore-node/include/TunnelMonitor/rs485/Rs485OwnerTransaction.h),
[Rs485Task.cpp](../../../FieldCore-node/src/rs485/Rs485Task.cpp), symbols
`processResponse`, `responseComplete`,
[ArduinoRs485Backend.cpp](../../../FieldCore-node/src/rs485/ArduinoRs485Backend.cpp),
and [DeviceMeasurement.h](../../../FieldCore-node/include/TunnelMonitor/contracts/DeviceMeasurement.h).

FieldCore currently selects SHZK and VibWire, with no ESS or VTN module found
in `include`, `src` or `config`. Actual dependency pins are SHZK `v3.0.0` and
VibWire `v1.2.0` in
[build_profiles.json](../../../FieldCore-node/config/build_profiles.json).
Its [RS485 guideline](../../../FieldCore-node/docs/guidelines/rs485_devices.md)
still names SHZK `v2.3.0`, while source asserts version `30000`. Record that
drift rather than copying the stale pin; reconcile it in FieldCore before a
future integration. The selected board's TX47/RX48/DE21 pins are product
facts, not ESS defaults.

## Packaging and verification conventions

All peers use `library.json` as version source, generated `Version.h`,
Doxygen public API comments, a changelog, native tests and S2/S3 examples.
All use explicit PlatformIO source filters and a Windows `scripts/pio.cmd`
wrapper. Native environment names differ: SHZK `native`, VTN/VibWire
`test_native`; Arduino environments share `ex_bringup_s3`/`ex_bringup_s2`.

SHZK currently advertises Arduino packaging and has IDF port notes.
VTN/VibWire have root CMake/IDF component metadata and separate native IDF
examples. VibWire's IDF console is intentionally a subset. A token-contract
script is useful for advertised surface checks, but behavior also needs
tests. Keep package claims aligned with actual builds and clean consumers.

Useful future test precedents are SHZK's `test/test_parsers.cpp`,
`test/test_transport.h`, `test/test_cli.cpp`; VTN's `test/test_codec.cpp` and
`test/test_example_rtu.cpp`; VibWire's codec, transport and IDF console tests;
and FieldCore's `test/native/test_owner_rs485/test_rs485_task_fake.cpp` and
`test/native/test_component_shzk/test_shzk_module.cpp`. They cover strict
parsing, unchanged outputs, capacities, fake transport timing, retained
results, cancellation and fairness. They are precedents, not tests run in
this review and not evidence that motor control already works.

Some sibling AGENTS guidance mentions hypothetical stateful APIs, and some
source still contains historical defaults or aliases. Follow the current
stateless boundary and explicitly documented ESS decisions rather than
turning every historical detail into a new requirement.
