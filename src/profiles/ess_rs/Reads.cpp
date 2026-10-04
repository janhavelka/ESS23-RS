// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Reads.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>

namespace MotorControlRS { namespace ESS_RS {
namespace {
Status invalid(ReadError reason, const char* message) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(reason), message);
}
Status failure(ReadError reason, const char* message) {
    return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(reason), message);
}
bool sameTarget(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
bool validTuple(const ActiveSerialTuple& tuple) {
    return !tuple.known || (tuple.baud && tuple.dataBits >= 5 && tuple.dataBits <= 8 &&
        tuple.parity >= SerialParity::NONE && tuple.parity <= SerialParity::ODD &&
        (tuple.stopBits == 1 || tuple.stopBits == 2));
}
uint8_t steps(ReadKind kind) { return kind == ReadKind::IDENTITY ? 1 : kind == ReadKind::STATE ? 3 : 5; }
bool window(ReadKind kind, uint8_t step, uint16_t& first, uint16_t& count) {
    if (kind == ReadKind::IDENTITY && step == 0) {
        first = Registers::DRIVER_MODEL; count = 4; return true;
    }
    if (kind == ReadKind::STATE) {
        switch (step) {
        case 0: first = Registers::ERROR_CODE; count = 2; return true;
        case 1: first = Registers::INPUT_STATUS; count = 2; return true;
        case 2: first = Registers::CURRENT_POSITION; count = 3; return true;
        default: return false;
        }
    }
    if (kind != ReadKind::CONFIG) return false;
    switch (step) {
    case 0: first = Registers::DEFAULT_DIRECTION; count = 2; return true;
    case 1: first = Registers::CUSTOM_NODE; count = 3; return true;
    case 2: first = Registers::OVER_LIMIT_STOP; count = 3; return true;
    case 3: first = Registers::INPUT_POLARITY; count = 5; return true;
    case 4: first = Registers::CONTROL_ALGORITHM; count = 2; return true;
    default: return false;
    }
}
Status prepare(ReadContext& output, ReadKind kind, const ReadTarget& target,
               uint32_t operationId, uint64_t nowUs, uint64_t deadlineUs,
               const ActiveSerialTuple& tuple, const InputWiring* wiring) {
    if (!target.id || !target.generation || !isValidAddress(target.address))
        return invalid(ReadError::INVALID_TARGET, "invalid read target");
    if (!operationId) return invalid(ReadError::INVALID_OPERATION, "zero operation id");
    if (deadlineUs <= nowUs) return invalid(ReadError::INVALID_DEADLINE, "expired read deadline");
    if (!validTuple(tuple)) return invalid(ReadError::INVALID_TUPLE, "invalid observed serial tuple");
    if (wiring) for (std::size_t i = 0; i < READ_INPUT_COUNT; ++i)
        if (wiring[i] > InputWiring::CONNECTED)
            return invalid(ReadError::INVALID_WIRING, "invalid input wiring declaration");
    ReadContext prepared;
    prepared.target = target; prepared.operationId = operationId; prepared.kind = kind;
    prepared.state = ReadState::ACTIVE; prepared.startedUs = prepared.servicedUs = nowUs;
    prepared.deadlineUs = deadlineUs; prepared.activeSerial = tuple;
    if (wiring) for (std::size_t i = 0; i < READ_INPUT_COUNT; ++i) prepared.wiring[i] = wiring[i];
    output = prepared;
    return Ok();
}
void finish(ReadContext& context, ReadOutcome outcome, Status status) {
    context.outcome = outcome; context.status = status;
    context.state = outcome == ReadOutcome::SUCCESS ? ReadState::SUCCEEDED : ReadState::FAILED;
}
Status publishable(const ReadContext& context, ReadKind kind) {
    if (context.kind != kind) return invalid(ReadError::WRONG_KIND, "wrong read operation kind");
    if (context.state != ReadState::SUCCEEDED || context.completedSteps != steps(kind))
        return invalid(ReadError::NOT_COMPLETE, "read observation is incomplete");
    return Ok();
}
Status words(const ReadContext& context, uint8_t step, uint16_t* output) {
    const ReadStepObservation& observation = context.observations[step];
    std::size_t count = 0;
    return parseRegisters(observation.raw, observation.length, context.target.address,
        observation.count, output, 5, count);
}
} // namespace

ReadCapabilities readCapabilities() noexcept {
    ReadCapabilities result;
    result.probe = result.identity = result.config = result.state = true;
    result.maxSteps = READ_MAX_STEPS; result.maxReplyBytes = READ_MAX_REPLY_BYTES;
    return result;
}
Status prepareIdentity(ReadContext& output, const ReadTarget& target, uint32_t operationId,
                       uint64_t nowUs, uint64_t deadlineUs,
                       const ActiveSerialTuple& activeSerial) noexcept {
    return prepare(output, ReadKind::IDENTITY, target, operationId, nowUs, deadlineUs, activeSerial, nullptr);
}
Status prepareConfig(ReadContext& output, const ReadTarget& target, uint32_t operationId,
                     uint64_t nowUs, uint64_t deadlineUs,
                     const ActiveSerialTuple& activeSerial, const InputWiring* wiring) noexcept {
    return prepare(output, ReadKind::CONFIG, target, operationId, nowUs, deadlineUs, activeSerial, wiring);
}
Status prepareState(ReadContext& output, const ReadTarget& target, uint32_t operationId,
                    uint64_t nowUs, uint64_t deadlineUs,
                    const ActiveSerialTuple& activeSerial, const ConfigObservation* config) noexcept {
    if (config && (!sameTarget(config->target, target) || !config->operationId))
        return invalid(ReadError::WRONG_CORRELATION, "state configuration context mismatch");
    if (config && ((config->wordOrderKnown && config->wordOrder > WordOrder::LOW_WORD_FIRST) ||
        (config->algorithmKnown && config->algorithm != ControlAlgorithm::OPEN_LOOP &&
         config->algorithm != ControlAlgorithm::ALGORITHM_1)))
        return invalid(ReadError::INVALID_EVENT, "invalid decoded configuration context");
    ReadContext prepared;
    const Status status = prepare(prepared, ReadKind::STATE, target, operationId, nowUs, deadlineUs, activeSerial, nullptr);
    if (!status) return status;
    if (config) {
        prepared.configOperationId = config->operationId;
        prepared.stateWordOrderKnown = config->wordOrderKnown; prepared.stateWordOrder = config->wordOrder;
        prepared.stateAlgorithmKnown = config->algorithmKnown; prepared.stateAlgorithm = config->algorithm;
    }
    output = prepared;
    return Ok();
}
Status nextRead(const ReadContext& context, uint64_t nowUs, PreparedRead& output) noexcept {
    if (context.state != ReadState::ACTIVE) return invalid(ReadError::INVALID_STATE, "read is not active");
    if (nowUs < context.servicedUs) return invalid(ReadError::CLOCK_ERROR, "read clock moved backwards");
    if (nowUs >= context.deadlineUs) return failure(ReadError::DEADLINE_EXPIRED, "read deadline expired");
    PreparedRead next;
    if (!window(context.kind, context.step, next.first, next.count))
        return invalid(ReadError::INVALID_STATE, "invalid read step");
    next.target = context.target; next.operationId = context.operationId; next.step = context.step;
    next.deadlineUs = context.deadlineUs;
    next.length = buildReadRegisters(context.target.address, next.first, next.count, next.bytes, sizeof(next.bytes));
    if (!next.length) return invalid(ReadError::INVALID_STATE, "invalid reviewed read window");
    output = next;
    return Ok();
}
Status advanceRead(ReadContext& context, const ReadEvent& event, uint64_t nowUs) noexcept {
    if (context.state != ReadState::ACTIVE) return invalid(ReadError::INVALID_STATE, "read is not active");
    if (!sameTarget(context.target, event.target) || context.operationId != event.operationId || context.step != event.step)
        return invalid(ReadError::WRONG_CORRELATION, "read event correlation mismatch");
    if (nowUs < context.servicedUs) return invalid(ReadError::CLOCK_ERROR, "read clock moved backwards");
    if (event.kind > ReadEventKind::DEADLINE)
        return invalid(ReadError::INVALID_EVENT, "unknown read event");
    if (event.txAccepted > READ_REQUEST_LEN)
        return invalid(ReadError::INVALID_EVENT, "invalid transport evidence envelope");
    if (event.kind == ReadEventKind::FRAME) {
        if (!event.frame || (event.qualified && (event.earliestUs < context.servicedUs ||
            event.earliestUs > event.latestUs || event.latestUs > nowUs)) ||
            (!event.qualified && (event.earliestUs || event.latestUs)))
            return invalid(ReadError::INVALID_EVENT, "invalid frame event envelope");
    } else if ((!event.frame && event.length) || event.qualified || event.earliestUs || event.latestUs ||
               (event.kind == ReadEventKind::DEADLINE && nowUs < context.deadlineUs)) {
        return invalid(ReadError::INVALID_EVENT, "invalid local read event envelope");
    }
    ReadStepObservation observation;
    if (!window(context.kind, context.step, observation.first, observation.count))
        return invalid(ReadError::INVALID_STATE, "invalid read step");
    observation.event = event.kind; observation.attemptedUs = context.servicedUs; observation.deliveredUs = nowUs;
    observation.transportDetail = event.transportDetail;
    observation.txAccepted = event.txAccepted; observation.executionUnknown = event.executionUnknown;
    observation.qualified = event.qualified;
    observation.earliestUs = event.earliestUs; observation.latestUs = event.latestUs;
    if (event.frame) {
        observation.receivedLength = event.length;
        observation.length = event.length < READ_MAX_REPLY_BYTES ? event.length : READ_MAX_REPLY_BYTES;
        if (observation.length) std::memcpy(observation.raw, event.frame, observation.length);
        if (event.kind == ReadEventKind::FRAME)
            observation.status = validateReadResponseExpected(event.frame, event.length, context.target.address,
                observation.count, &observation.frameError);
    }
    if (event.kind == ReadEventKind::TRANSPORT_FAILURE) {
        observation.status = failure(ReadError::TRANSPORT_FAILURE, "read transport failed");
    } else if (event.kind == ReadEventKind::CANCEL) {
        observation.status = failure(ReadError::CANCELLED, "read locally cancelled");
    } else if (event.kind == ReadEventKind::DEADLINE) {
        observation.status = failure(ReadError::DEADLINE_EXPIRED, "read deadline expired");
    }
    context.observations[context.step] = observation;
    context.servicedUs = nowUs;
    if (event.kind == ReadEventKind::CANCEL) finish(context, ReadOutcome::CANCELLED, observation.status);
    else if (event.kind == ReadEventKind::DEADLINE) finish(context, ReadOutcome::DEADLINE, observation.status);
    else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) finish(context, ReadOutcome::TRANSPORT_ERROR, observation.status);
    else if (!event.qualified) finish(context, ReadOutcome::TIMING_UNQUALIFIED,
        failure(ReadError::TIMING_UNQUALIFIED, "read closure timing is unqualified"));
    else if (event.latestUs > context.deadlineUs) finish(context, ReadOutcome::DEADLINE,
        failure(ReadError::DEADLINE_EXPIRED, "read closure exceeds deadline"));
    else if (!observation.status) finish(context, ReadOutcome::REPLY_ERROR, observation.status);
    else {
        ++context.completedSteps; ++context.step;
        if (context.completedSteps == steps(context.kind)) finish(context, ReadOutcome::SUCCESS, Ok());
        else if (nowUs >= context.deadlineUs) finish(context, ReadOutcome::DEADLINE,
            failure(ReadError::DEADLINE_EXPIRED, "read budget exhausted before next step"));
    }
    return Ok();
}
Status getIdentity(const ReadContext& context, IdentityObservation& output) noexcept {
    const Status ready = publishable(context, ReadKind::IDENTITY); if (!ready) return ready;
    uint16_t values[5] = {};
    const Status parsed = words(context, 0, values); if (!parsed) return parsed;
    IdentityObservation identity;
    identity.target = context.target; identity.operationId = context.operationId;
    identity.rawModel = values[0]; identity.rawVersion = values[1];
    identity.rawActiveNode = values[2]; identity.rawDip = values[3];
    identity.activeNodeKnown = values[2] >= 1 && values[2] <= 247;
    if (identity.activeNodeKnown) identity.activeNode = static_cast<uint8_t>(values[2]);
    identity.dipIssues = static_cast<RegisterIssue>(static_cast<uint32_t>(RegisterIssue::MODEL_APPLICABILITY) |
        static_cast<uint32_t>(RegisterIssue::SOURCE_CONFLICT));
    identity.activeSerial = context.activeSerial; identity.provenance = context.observations[0];
    output = identity;
    return Ok();
}
Status getConfig(const ReadContext& context, ConfigObservation& output) noexcept {
    const Status ready = publishable(context, ReadKind::CONFIG); if (!ready) return ready;
    ConfigObservation config;
    config.target = context.target; config.operationId = context.operationId;
    config.activeSerial = context.activeSerial;
    uint16_t values[5] = {};
    for (uint8_t step = 0; step < READ_MAX_STEPS; ++step) {
        const Status parsed = words(context, step, values); if (!parsed) return parsed;
        config.provenance[step] = context.observations[step];
        switch (step) {
        case 0: config.raw.direction = values[0]; config.raw.subdivision = values[1]; break;
        case 1: config.raw.customNode = values[0]; config.raw.baud = values[1]; config.raw.format = values[2]; break;
        case 2: config.raw.overLimitStop = values[0]; config.raw.softLimitEnable = values[1]; config.raw.wordOrder = values[2]; break;
        case 3:
            config.raw.inputPolarity = values[0];
            for (std::size_t i = 0; i < READ_INPUT_COUNT; ++i) config.raw.inputFunctions[i] = values[i + 1];
            break;
        case 4: config.raw.algorithm = values[0]; config.raw.encoderResolution = values[1]; break;
        }
    }
    config.directionKnown = config.raw.direction <= 1;
    if (config.directionKnown) config.direction = static_cast<DefaultDirection>(config.raw.direction);
    config.baudKnown = config.raw.baud <= 3;
    if (config.baudKnown) config.baud = static_cast<BaudRateCode>(config.raw.baud);
    config.formatKnown = config.raw.format <= 3;
    if (config.formatKnown) config.format = static_cast<SerialFormatCode>(config.raw.format);
    config.overLimitStopKnown = config.raw.overLimitStop <= 1;
    if (config.overLimitStopKnown) config.overLimitStop = static_cast<OverLimitStop>(config.raw.overLimitStop);
    config.softLimitEnableKnown = config.raw.softLimitEnable <= 1;
    if (config.softLimitEnableKnown) config.softLimitEnable = static_cast<SoftLimitEnable>(config.raw.softLimitEnable);
    config.wordOrderKnown = config.raw.wordOrder <= 1;
    if (config.wordOrderKnown) config.wordOrder = static_cast<WordOrder>(config.raw.wordOrder);
    config.algorithmKnown = config.raw.algorithm >= 1 && config.raw.algorithm <= 2;
    if (config.algorithmKnown) config.algorithm = static_cast<ControlAlgorithm>(config.raw.algorithm);
    config.unknownPolarityBits = config.raw.inputPolarity & 0xFFF0;
    for (std::size_t i = 0; i < READ_INPUT_COUNT; ++i) {
        config.inputFunctionKnown[i] = config.raw.inputFunctions[i] <= 17;
        if (config.inputFunctionKnown[i]) config.inputFunctions[i] = static_cast<InputFunction>(config.raw.inputFunctions[i]);
        config.inputInverted[i] = (config.raw.inputPolarity & (1U << i)) != 0;
        config.wiring[i] = context.wiring[i];
    }
    if (config.raw.encoderResolution) {
        config.encoderResolution = ReadResolution::RESOLVED;
        config.units.encoder.countsPerUnit = UnitScale(config.raw.encoderResolution, 1, ScaleSource::READBACK);
    }
    output = config;
    return Ok();
}
Status getStateBlock(const ReadContext& context, uint8_t block, StateObservation& output) noexcept {
    if (context.kind != ReadKind::STATE) return invalid(ReadError::WRONG_KIND, "wrong read operation kind");
    if (block >= STATE_BLOCK_COUNT || block >= context.completedSteps)
        return invalid(ReadError::NOT_COMPLETE, "state block is unavailable");
    uint16_t values[5] = {};
    const Status parsed = words(context, block, values); if (!parsed) return parsed;
    StateObservation state;
    state.target = context.target; state.operationId = context.operationId;
    state.configOperationId = context.configOperationId; state.block = static_cast<StateBlock>(block);
    state.provenance = context.observations[block];
    if (block == static_cast<uint8_t>(StateBlock::MOTION)) {
        state.rawAlarm = values[0]; state.rawMotion = values[1];
        state.alarmKnown = values[0] <= 3 || values[0] == 5;
        if (state.alarmKnown) state.alarm = static_cast<AlarmCode>(values[0]);
        state.unknownMotionBits = values[1] & 0xFF80;
        state.inPosition = (values[1] & static_cast<uint16_t>(MotionStatusBit::IN_POSITION)) != 0;
        state.homingComplete = (values[1] & static_cast<uint16_t>(MotionStatusBit::HOMING_COMPLETE)) != 0;
        state.running = (values[1] & static_cast<uint16_t>(MotionStatusBit::RUNNING)) != 0;
        state.alarmFlag = (values[1] & static_cast<uint16_t>(MotionStatusBit::ALARM)) != 0;
        state.released = (values[1] & static_cast<uint16_t>(MotionStatusBit::RELEASED)) != 0;
        state.enabled = !state.released;
        state.positiveSoftLimit = (values[1] & static_cast<uint16_t>(MotionStatusBit::POSITIVE_SOFT_LIMIT)) != 0;
        state.negativeSoftLimit = (values[1] & static_cast<uint16_t>(MotionStatusBit::NEGATIVE_SOFT_LIMIT)) != 0;
    } else if (block == static_cast<uint8_t>(StateBlock::IO)) {
        state.rawInputs = values[0]; state.rawOutputs = values[1];
        state.unknownInputBits = values[0] & 0xFFF0; state.unknownOutputBits = values[1] & 0xFFFC;
        for (uint8_t i = 0; i < READ_INPUT_COUNT; ++i) state.inputs[i] = (values[0] & (1U << i)) != 0;
        for (uint8_t i = 0; i < 2; ++i) state.outputs[i] = (values[1] & (1U << i)) != 0;
    } else {
        state.rawPositionWords[0] = values[0]; state.rawPositionWords[1] = values[1]; state.rawSpeed = values[2];
        if (context.stateWordOrderKnown) {
            const Status decoded = decodeUint32(values, 2, context.stateWordOrder, state.rawPosition);
            if (!decoded) return decoded;
            state.pairKnown = true; state.wordOrderResolution = ReadResolution::RESOLVED;
        }
        if (context.stateAlgorithmKnown) {
            state.positionSource = context.stateAlgorithm == ControlAlgorithm::OPEN_LOOP ?
                PositionSource::COMMAND_GIVEN : PositionSource::SUBDIVISION_EQUIVALENT_FEEDBACK;
            state.positionSourceResolution = ReadResolution::RESOLVED;
        }
    }
    output = state;
    return Ok();
}
}} // namespace MotorControlRS::ESS_RS
