// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Segments.h"
namespace MotorControlRS { namespace ESS_RS {
namespace {
bool segmentGroup(DriverGroup group) {
    return group >= DriverGroup::POSITION_SEGMENT && group <= DriverGroup::SEGMENT_START_SPEED;
}
Status invalid(const char* message) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(DriverError::INVALID_CANDIDATE), message);
}
}
Status prepareSegmentRead(DriverContext& output, const ReadTarget& target, uint32_t id,
    uint32_t generation, uint64_t now, uint64_t deadline, SegmentKind kind, uint8_t index) noexcept {
    if (kind > SegmentKind::START_SPEED) return invalid("invalid segment kind");
    const DriverGroup groups[] = {DriverGroup::POSITION_SEGMENT, DriverGroup::SPEED_SEGMENT, DriverGroup::SEGMENT_START_SPEED};
    return prepareDriverRead(output, target, id, generation, now, deadline, groups[static_cast<uint8_t>(kind)], index);
}
Status prepareSegmentSettings(DriverContext& output, const ReadTarget& target, uint32_t id,
    const DriverRequest& request, const DriverPrerequisites& prerequisites, uint64_t now, uint64_t deadline) noexcept {
    if (!segmentGroup(request.group)) return invalid("segment settings require indexed segment group");
    return prepareDriverSettings(output, target, id, request, prerequisites, now, deadline);
}
Status getSegment(const DriverContext& context, SegmentObservation& output, bool qualifiedOrder, WordOrder order) noexcept {
    if (!segmentGroup(context.group)) return invalid("complete segment read required");
    if (qualifiedOrder && order > WordOrder::LOW_WORD_FIRST) return invalid("invalid qualified segment word order");
    DriverObservation raw;
    const Status status = getDriver(context, raw);
    if (!status) return status;
    SegmentObservation value;
    value.kind = context.group == DriverGroup::POSITION_SEGMENT ? SegmentKind::POSITION :
        context.group == DriverGroup::SPEED_SEGMENT ? SegmentKind::SPEED : SegmentKind::START_SPEED;
    value.index = raw.segmentIndex; value.target = raw.target;
    value.operationId = raw.operationId; value.configurationGeneration = raw.configurationGeneration;
    value.knownFields = raw.knownFields; value.provenance = raw.provenance[0];
    if (value.kind == SegmentKind::START_SPEED) value.startSpeed = raw.raw[0];
    else { value.speed = raw.raw[0]; value.acceleration = raw.raw[1]; value.deceleration = raw.raw[2]; }
    if (value.kind == SegmentKind::POSITION) {
        value.pulseWords[0] = raw.raw[3]; value.pulseWords[1] = raw.raw[4];
        value.pairOrderKnown = qualifiedOrder;
        if (qualifiedOrder) {
            const Status decoded = decodeUint32(value.pulseWords, 2, order, value.pulseBits);
            if (!decoded) return decoded;
        }
    }
    output = value; return Ok();
}
}} // namespace MotorControlRS::ESS_RS
