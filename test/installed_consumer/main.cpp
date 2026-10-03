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
    return 0;
}
