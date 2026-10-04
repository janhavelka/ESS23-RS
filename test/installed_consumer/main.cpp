// SPDX-License-Identifier: MIT
// This target sees only the installed package's exported include directories.
#include <MotorControlRS/ReadOperation.h>
#include <MotorControlRS/profiles/ess_rs/Reads.h>

int main() {
    using namespace MotorControlRS;
    using namespace MotorControlRS::ESS_RS;
    ReadTarget target; target.id = 1; target.address = 1; target.generation = 2;
    ReadContext operation;
    if (!prepareIdentity(operation, target, 3, 100, 1000)) return 1;
    PreparedRead request;
    if (!nextRead(operation, 100, request) || request.length != 8 || request.first != 0 || request.count != 4) return 2;
    const uint8_t response[] = {1, 3, 8, 0x4E, 0xEA, 0x12, 0x34, 0, 1, 0xA5, 0x81, 3, 0xE3};
    ReadEvent event; event.target = target; event.operationId = 3; event.step = 0;
    event.frame = response; event.length = sizeof(response); event.qualified = true;
    event.txAccepted = 8;
    event.earliestUs = 200; event.latestUs = 220;
    if (!advanceRead(operation, event, 230)) return 3;
    IdentityObservation observation;
    if (!getIdentity(operation, observation) || observation.rawModel != 0x4EEA || observation.rawVersion != 0x1234) return 4;
    if (observation.activeSerial.known || observation.modelResolution != ReadResolution::MODEL_MAPPING_UNRESOLVED) return 5;
    if (!readCapabilities().config || !prepareConfig(operation, target, 4, 300, 1000)) return 6;
    if (!nextRead(operation, 300, request) || request.first != 0x10 || request.count != 2) return 7;
    ReadEvent cancelled; cancelled.target = target; cancelled.operationId = 4; cancelled.kind = ReadEventKind::CANCEL;
    if (!advanceRead(operation, cancelled, 310) || operation.outcome != ReadOutcome::CANCELLED) return 8;
    if (!readCapabilities().state || !prepareState(operation, target, 5, 400, 2000)) return 9;
    if (!nextRead(operation, 400, request) || request.first != 6 || request.count != 2) return 10;
    // Decode a completed block while the multi-read operation is still active.
    // This proves the installed consumer has the actual public state API.
    uint8_t motion[] = {1, 3, 4, 0, 0, 0, 0x10, 0, 0};
    const uint16_t crc = calcCrc16(motion, sizeof(motion) - 2);
    motion[7] = static_cast<uint8_t>(crc); motion[8] = static_cast<uint8_t>(crc >> 8);
    event = ReadEvent(); event.target = target; event.operationId = 5;
    event.frame = motion; event.length = sizeof(motion); event.qualified = true;
    event.earliestUs = 500; event.latestUs = 520; event.txAccepted = 8;
    if (!advanceRead(operation, event, 530)) return 11;
    StateObservation state;
    if (!getStateBlock(operation, 0, state) || !state.released || state.enabled || !state.alarmKnown) return 12;
    if (getStateBlock(operation, 2, state) || state.block != StateBlock::MOTION) return 13;
    return 0;
}
