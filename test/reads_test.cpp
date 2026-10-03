// SPDX-License-Identifier: MIT
// Public typed reads, using independent literal FC03 requests/replies.
#include <MotorControlRS/profiles/ess_rs/Reads.h>
#include <cassert>
#include <cstring>
#include <limits>

using namespace MotorControlRS;
using namespace MotorControlRS::ESS_RS;
namespace {
const uint8_t IDENTITY[] = {1, 3, 8, 0x4E, 0xEA, 0x12, 0x34, 0, 1, 0xA5, 0x81, 3, 0xE3};
const uint8_t DIRECTION[] = {1, 3, 4, 0, 1, 6, 0x40, 0xA9, 0xA3};
const uint8_t SERIAL[] = {1, 3, 6, 0, 42, 0, 2, 0, 3, 0xD9, 0x72};
const uint8_t ORDER[] = {1, 3, 6, 0, 1, 0, 1, 0, 1, 0x8C, 0xB5};
const uint8_t INPUTS[] = {1, 3, 10, 0xA5, 0xF5, 0, 0, 0, 1, 0, 2, 0, 17, 0x55, 0x7C};
const uint8_t ENCODER[] = {1, 3, 4, 0, 2, 0x10, 0, 0x56, 0x33};
const uint8_t EXCEPTION[] = {1, 0x83, 0xE7, 1, 0x7A};
struct Fixture { uint16_t first, count; const uint8_t* frame; std::size_t length; uint8_t request[8]; };
const Fixture CONFIG[] = {
    {0x10, 2, DIRECTION, sizeof(DIRECTION), {1, 3, 0, 0x10, 0, 2, 0xC5, 0xCE}},
    {0x13, 3, SERIAL, sizeof(SERIAL), {1, 3, 0, 0x13, 0, 3, 0xF4, 0x0E}},
    {0x17, 3, ORDER, sizeof(ORDER), {1, 3, 0, 0x17, 0, 3, 0xB5, 0xCF}},
    {0x40, 5, INPUTS, sizeof(INPUTS), {1, 3, 0, 0x40, 0, 5, 0x84, 0x1D}},
    {0x100, 2, ENCODER, sizeof(ENCODER), {1, 3, 1, 0, 0, 2, 0xC5, 0xF7}}
};
ReadTarget target() { ReadTarget t; t.id = 7; t.address = 1; t.generation = 9; return t; }
ActiveSerialTuple active() {
    ActiveSerialTuple t; t.known = true; t.baud = 115200; t.dataBits = 8;
    t.parity = SerialParity::NONE; t.stopBits = 1; return t;
}
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& value) { std::memcpy(bytes, &value, sizeof(T)); }
    void check(const T& value) const { assert(std::memcmp(bytes, &value, sizeof(T)) == 0); }
};
ReadContext identity(uint64_t deadline = 1000) {
    ReadContext c; assert(prepareIdentity(c, target(), 12, 100, deadline, active())); return c;
}
ReadContext config(uint64_t deadline = 1000, const InputWiring* wiring = nullptr) {
    ReadContext c; assert(prepareConfig(c, target(), 12, 100, deadline, active(), wiring)); return c;
}
ReadEvent frame(const ReadContext& c, const uint8_t* bytes, std::size_t length,
                uint64_t earliest = 200, uint64_t latest = 220) {
    ReadEvent e; e.target = c.target; e.operationId = c.operationId; e.step = c.step;
    e.frame = bytes; e.length = length; e.qualified = true; e.txAccepted = 8;
    e.earliestUs = earliest; e.latestUs = latest; return e;
}
ReadEvent control(const ReadContext& c, ReadEventKind kind) {
    ReadEvent e; e.target = c.target; e.operationId = c.operationId; e.step = c.step; e.kind = kind; return e;
}
void consumeStep(ReadContext& c, unsigned index) {
    const uint64_t earliest = 200 + index * 100;
    assert(advanceRead(c, frame(c, CONFIG[index].frame, CONFIG[index].length, earliest, earliest + 20), earliest + 30));
}
ConfigObservation completedConfig() {
    ReadContext c = config(); for (unsigned i = 0; i < 5; ++i) consumeStep(c, i);
    ConfigObservation out; assert(getConfig(c, out)); return out;
}
// Independent CRC fixture generator is used only when testing future/raw codes.
void seal(uint8_t* bytes, std::size_t size) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size - 2; ++i) {
        crc ^= bytes[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xA001 : 0);
    }
    bytes[size - 2] = static_cast<uint8_t>(crc); bytes[size - 1] = static_cast<uint8_t>(crc >> 8);
}
void testIdentityAndOwnedProvenance() {
    ReadContext c = identity(); PreparedRead r;
    assert(nextRead(c, 100, r));
    const uint8_t expected[] = {1, 3, 0, 0, 0, 4, 0x44, 9};
    assert(r.first == 0 && r.count == 4 && r.length == 8 && r.deadlineUs == 1000);
    assert(r.operationId == 12 && r.step == 0 && r.target.id == 7 && r.target.generation == 9);
    assert(std::memcmp(r.bytes, expected, 8) == 0);
    const Saved<ReadContext> prepared(c); PreparedRead repeated;
    assert(nextRead(c, 150, repeated)); prepared.check(c);
    assert(std::memcmp(repeated.bytes, r.bytes, 8) == 0);
    uint8_t borrowed[sizeof(IDENTITY)]; std::memcpy(borrowed, IDENTITY, sizeof(borrowed));
    assert(advanceRead(c, frame(c, borrowed, sizeof(borrowed)), 230));
    std::memset(borrowed, 0, sizeof(borrowed)); // Context cannot retain caller frame memory.
    assert(c.state == ReadState::SUCCEEDED && c.outcome == ReadOutcome::SUCCESS && c.completedSteps == 1);
    IdentityObservation out; assert(getIdentity(c, out));
    assert(out.rawModel == 0x4EEA && out.rawVersion == 0x1234 && out.rawActiveNode == 1 && out.rawDip == 0xA581);
    assert(out.activeNodeKnown && out.activeNode == 1);
    assert(out.modelResolution == ReadResolution::MODEL_MAPPING_UNRESOLVED);
    assert(out.versionResolution == ReadResolution::VERSION_MAPPING_UNRESOLVED);
    assert(out.dipResolution == ReadResolution::DIP_MAPPING_UNRESOLVED);
    assert(out.activeSerial.known && out.activeSerial.baud == 115200);
    assert(out.provenance.length == sizeof(IDENTITY) && std::memcmp(out.provenance.raw, IDENTITY, sizeof(IDENTITY)) == 0);
    assert(out.provenance.earliestUs == 200 && out.provenance.latestUs == 220 && out.provenance.deliveredUs == 230);
    assert(out.provenance.txAccepted == 8 && !out.provenance.executionUnknown);
    const Saved<PreparedRead> noNext(r); assert(!nextRead(c, 240, r)); noNext.check(r);
    const Saved<ReadContext> terminal(c); assert(!advanceRead(c, frame(c, IDENTITY, sizeof(IDENTITY)), 240)); terminal.check(c);
}
void testConfigWindowsPublicationAndIndependentEvidence() {
    const InputWiring declaration[] = {InputWiring::UNCONNECTED, InputWiring::CONNECTED, InputWiring::UNKNOWN, InputWiring::UNCONNECTED};
    InputWiring borrowed[4]; std::memcpy(borrowed, declaration, sizeof(borrowed));
    ReadContext c = config(1000, borrowed); borrowed[0] = InputWiring::CONNECTED;
    ConfigObservation prior = completedConfig(); prior.raw.subdivision = 9999;
    const Saved<ConfigObservation> previous(prior);
    for (unsigned i = 0; i < 5; ++i) {
        PreparedRead r; assert(nextRead(c, c.servicedUs, r));
        assert(r.first == CONFIG[i].first && r.count == CONFIG[i].count && r.step == i && r.length == 8);
        assert(std::memcmp(r.bytes, CONFIG[i].request, 8) == 0 && isReadRangeValid(r.first, r.count));
        assert(r.count <= MAX_READ_REGISTERS && r.bytes[1] == 3 && r.deadlineUs == 1000);
        assert(!getConfig(c, prior)); previous.check(prior);
        consumeStep(c, i);
    }
    assert(c.state == ReadState::SUCCEEDED && c.completedSteps == 5 && getConfig(c, prior));
    assert(prior.raw.direction == 1 && prior.raw.subdivision == 1600 && prior.raw.customNode == 42);
    assert(prior.directionKnown && prior.direction == DefaultDirection::REVERSED);
    assert(prior.baudKnown && prior.baud == BaudRateCode::BAUD_19200 && prior.format == SerialFormatCode::FORMAT_8O1);
    assert(prior.activeSerial.baud == 115200 && prior.activeSerial.parity == SerialParity::NONE);
    assert(prior.raw.wordOrder == 1 && prior.wordOrderKnown && prior.wordOrder == WordOrder::LOW_WORD_FIRST);
    assert(prior.raw.encoderResolution == 4096 && prior.algorithmKnown && prior.algorithm == ControlAlgorithm::ALGORITHM_1);
    assert(prior.unknownPolarityBits == 0xA5F0);
    for (unsigned i = 0; i < 4; ++i) {
        assert(prior.wiring[i] == declaration[i] && !prior.inputLevelKnown[i]);
        assert(prior.inputFunctionKnown[i]); assert(prior.inputInverted[i] == (i == 0 || i == 2));
    }
    assert(prior.raw.inputFunctions[0] == 0); // An unconnected declaration did not rewrite an assignment.
    assert(prior.raw.inputFunctions[1] == 1 && prior.raw.inputFunctions[2] == 2 && prior.raw.inputFunctions[3] == 17);
    assert(prior.subdivisionResolution == ReadResolution::SCALE_UNRESOLVED);
    assert(prior.units.commandStepsPerMotorTurn.numerator == 0 && prior.units.commandStepsPerMotorTurn.source == ScaleSource::UNKNOWN);
    assert(prior.units.encoder.countsPerUnit.numerator == 4096 && prior.units.encoder.countsPerUnit.source == ScaleSource::READBACK);
    for (unsigned i = 0; i < 5; ++i) {
        assert(prior.provenance[i].first == CONFIG[i].first && prior.provenance[i].count == CONFIG[i].count);
        assert(prior.provenance[i].length == CONFIG[i].length);
        assert(std::memcmp(prior.provenance[i].raw, CONFIG[i].frame, CONFIG[i].length) == 0);
    }
    assert(!isReadRangeValid(0x10, 10) && !isReadRangeValid(0x13, 7)); // Documented gaps are never coalesced.
}
void testPreparationAndPublicationFailuresPreserveOutputs() {
    ReadContext out = identity(); const Saved<ReadContext> original(out);
    ReadTarget t = target();
    for (unsigned i = 0; i < 5; ++i) {
        t = target();
        if (i == 0) t.id = 0; else if (i == 1) t.address = 0; else if (i == 2) t.address = 248;
        else if (i == 3) t.generation = 0; else t.address = 255;
        assert(!prepareIdentity(out, t, 12, 100, 1000)); original.check(out);
        assert(!prepareConfig(out, t, 12, 100, 1000)); original.check(out);
    }
    assert(!prepareIdentity(out, target(), 0, 100, 1000)); original.check(out);
    assert(!prepareIdentity(out, target(), 12, 100, 100)); original.check(out);
    assert(!prepareConfig(out, target(), 12, 100, 99)); original.check(out);
    ActiveSerialTuple bad = active(); bad.baud = 0;
    assert(!prepareIdentity(out, target(), 12, 100, 1000, bad)); original.check(out);
    InputWiring wiring[4] = {}; wiring[2] = static_cast<InputWiring>(99);
    assert(!prepareConfig(out, target(), 12, 100, 1000, active(), wiring)); original.check(out);
    IdentityObservation identityOut; identityOut.rawModel = 0xA55A;
    const Saved<IdentityObservation> oldIdentity(identityOut);
    assert(!getIdentity(out, identityOut)); oldIdentity.check(identityOut);
    ConfigObservation configOut; configOut.raw.subdivision = 0xA55A;
    const Saved<ConfigObservation> oldConfig(configOut);
    assert(advanceRead(out, frame(out, IDENTITY, sizeof(IDENTITY)), 230));
    assert(!getConfig(out, configOut)); oldConfig.check(configOut);
    ReadContext empty; PreparedRead prepared;
    prepared.first = 0xA55A; const Saved<PreparedRead> oldPrepared(prepared);
    assert(!nextRead(empty, 100, prepared)); oldPrepared.check(prepared);
}
void testWrongEnvelopesAndSameLengthStepsRejectWithoutMutation() {
    ReadContext c = config();
    for (unsigned i = 0; i < 9; ++i) {
        ReadEvent e = frame(c, DIRECTION, sizeof(DIRECTION));
        if (i == 0) ++e.operationId; else if (i == 1) ++e.target.id;
        else if (i == 2) ++e.target.address; else if (i == 3) ++e.target.generation;
        else if (i == 4) ++e.step; else if (i == 5) e.kind = static_cast<ReadEventKind>(99);
        else if (i == 6) e.latestUs = e.earliestUs - 1;
        else if (i == 7) { e.frame = nullptr; e.length = sizeof(DIRECTION); }
        else { e.kind = ReadEventKind::CANCEL; }
        const Saved<ReadContext> unchanged(c);
        assert(!advanceRead(c, e, 230)); unchanged.check(c);
    }
    consumeStep(c, 0); consumeStep(c, 1);
    ReadEvent duplicate = frame(c, SERIAL, sizeof(SERIAL), 400, 420); duplicate.step = 1;
    const Saved<ReadContext> unchanged(c);
    assert(!advanceRead(c, duplicate, 430)); unchanged.check(c); // Same count as current order window.
    PreparedRead next; assert(nextRead(c, 430, next)); assert(next.first == 0x17 && next.count == 3);
    consumeStep(c, 2); consumeStep(c, 3);
    ReadEvent oldDirection = frame(c, DIRECTION, sizeof(DIRECTION), 600, 620); oldDirection.step = 0;
    const Saved<ReadContext> noMix(c);
    assert(!advanceRead(c, oldDirection, 630)); noMix.check(c); // Same count as current algorithm window.
    consumeStep(c, 4); assert(c.state == ReadState::SUCCEEDED);
}
void testPartialFailuresPreservePreviousObservation() {
    for (unsigned failedStep = 0; failedStep < 5; ++failedStep) {
        for (unsigned failure = 0; failure < 4; ++failure) {
            ReadContext c = config(); for (unsigned i = 0; i < failedStep; ++i) consumeStep(c, i);
            unsigned char earlierEvidence[sizeof(c.observations)];
            std::memcpy(earlierEvidence, c.observations, sizeof(c.observations));
            ConfigObservation previous = completedConfig(); previous.raw.subdivision = 9999;
            const Saved<ConfigObservation> saved(previous);
            uint8_t corrupt[READ_MAX_REPLY_BYTES]; std::memcpy(corrupt, CONFIG[failedStep].frame, CONFIG[failedStep].length);
            corrupt[CONFIG[failedStep].length - 1] ^= 1;
            const uint64_t earliest = 200 + failedStep * 100;
            ReadEvent event = failure == 0 ? frame(c, corrupt, CONFIG[failedStep].length, earliest, earliest + 20) :
                failure == 1 ? frame(c, EXCEPTION, sizeof(EXCEPTION), earliest, earliest + 20) :
                control(c, failure == 2 ? ReadEventKind::TRANSPORT_FAILURE : ReadEventKind::CANCEL);
            assert(advanceRead(c, event, earliest + 30));
            assert(c.state == ReadState::FAILED && c.completedSteps == failedStep);
            assert(c.outcome == (failure < 2 ? ReadOutcome::REPLY_ERROR : failure == 2 ? ReadOutcome::TRANSPORT_ERROR : ReadOutcome::CANCELLED));
            if (failure == 0) assert(c.status.code == Err::CRC_ERROR);
            if (failure == 1) assert(c.status.code == Err::EXCEPTION && c.status.detail == 0xE7);
            assert(!getConfig(c, previous)); saved.check(previous);
            PreparedRead noRetry; const Saved<PreparedRead> noWork(noRetry);
            assert(!nextRead(c, earliest + 40, noRetry)); noWork.check(noRetry);
            const Saved<ReadContext> terminal(c); assert(!advanceRead(c, event, earliest + 40)); terminal.check(c);
            assert(std::memcmp(earlierEvidence, c.observations,
                failedStep * sizeof(ReadStepObservation)) == 0);
            if (failure < 2) {
                assert(c.observations[failedStep].length == event.length);
                assert(std::memcmp(c.observations[failedStep].raw, event.frame, event.length) == 0);
            }
        }
    }
}
void testDeadlineUsesQualifiedClosureAndNeverRenewsBudget() {
    ReadContext c = identity(); assert(advanceRead(c, frame(c, IDENTITY, sizeof(IDENTITY), 980, 1000), 5000));
    IdentityObservation out; assert(getIdentity(c, out));
    assert(out.provenance.latestUs == 1000 && out.provenance.deliveredUs == 5000 && c.deadlineUs == 1000);
    for (unsigned kind = 0; kind < 3; ++kind) {
        c = identity(); ReadEvent e = frame(c, IDENTITY, sizeof(IDENTITY), 990, 1001);
        if (kind == 1) { e.qualified = false; e.earliestUs = e.latestUs = 0; }
        else if (kind == 2) e = control(c, ReadEventKind::DEADLINE);
        assert(advanceRead(c, e, 5000)); assert(c.state == ReadState::FAILED);
        assert(c.outcome == (kind == 1 ? ReadOutcome::TIMING_UNQUALIFIED : ReadOutcome::DEADLINE));
    }
    c = config(); assert(advanceRead(c, frame(c, DIRECTION, sizeof(DIRECTION), 980, 990), 1000));
    assert(c.state == ReadState::FAILED && c.outcome == ReadOutcome::DEADLINE);
    assert(c.completedSteps == 1 && c.observations[0].latestUs == 990 && c.deadlineUs == 1000);
    c = config(); PreparedRead pending; pending.first = 0xA55A; const Saved<PreparedRead> saved(pending);
    assert(!nextRead(c, 1000, pending)); saved.check(pending); assert(c.state == ReadState::ACTIVE);
    assert(advanceRead(c, control(c, ReadEventKind::DEADLINE), 1000)); assert(c.state == ReadState::FAILED);
    c = config(); for (unsigned i = 0; i < 4; ++i) consumeStep(c, i);
    assert(advanceRead(c, frame(c, ENCODER, sizeof(ENCODER), 980, 999), 5000));
    ConfigObservation configOut; assert(getConfig(c, configOut)); assert(configOut.provenance[4].deliveredUs == 5000);
}
void testUnknownEnumsAndZeroEncoderRemainRawEvidence() {
    ReadContext c = config();
    for (unsigned i = 0; i < 5; ++i) {
        uint8_t raw[READ_MAX_REPLY_BYTES]; std::memcpy(raw, CONFIG[i].frame, CONFIG[i].length);
        for (unsigned word = 0; word < CONFIG[i].count; ++word) { raw[3 + 2 * word] = 0xFF; raw[4 + 2 * word] = 0xFF; }
        if (i == 4) raw[5] = raw[6] = 0; // Zero is a read value, not a usable scale.
        seal(raw, CONFIG[i].length);
        assert(advanceRead(c, frame(c, raw, CONFIG[i].length, 200 + i * 100, 220 + i * 100), 230 + i * 100));
    }
    ConfigObservation out; assert(getConfig(c, out));
    assert(!out.directionKnown && !out.baudKnown && !out.formatKnown && !out.wordOrderKnown);
    assert(!out.overLimitStopKnown && !out.softLimitEnableKnown && !out.algorithmKnown);
    assert(out.raw.direction == 0xFFFF && out.raw.subdivision == 0xFFFF && out.raw.customNode == 0xFFFF);
    assert(out.raw.wordOrder == 0xFFFF && out.raw.algorithm == 0xFFFF && out.raw.encoderResolution == 0);
    for (unsigned i = 0; i < 4; ++i) assert(!out.inputFunctionKnown[i] && out.raw.inputFunctions[i] == 0xFFFF && !out.inputLevelKnown[i]);
    assert(out.unknownPolarityBits == 0xFFF0 && out.encoderResolution == ReadResolution::ZERO_ENCODER_SCALE);
    assert(out.units.commandStepsPerMotorTurn.numerator == 0 && out.units.encoder.countsPerUnit.numerator == 0);
    UnitConversion converted(99, 88); const Saved<UnitConversion> unchanged(converted);
    assert(!convertDisplacement(1, PositionUnit::ENCODER_COUNTS, PositionUnit::TURNS, out.units, converted)); unchanged.check(converted);
}
void testMalformedRepliesRetainFailureWithoutPublishing() {
    for (unsigned failure = 0; failure < 6; ++failure) {
        ReadContext c = identity();
        uint8_t reply[40] = {}; std::memcpy(reply, IDENTITY, sizeof(IDENTITY));
        std::size_t length = sizeof(IDENTITY);
        FrameError expected = FrameError::NONE;
        if (failure == 0) { reply[0] = 2; seal(reply, length); expected = FrameError::ADDRESS; }
        else if (failure == 1) { reply[1] = 4; seal(reply, length); expected = FrameError::FUNCTION; }
        else if (failure == 2) { reply[2] = 6; seal(reply, length); expected = FrameError::BYTE_COUNT; }
        else if (failure == 3) { --length; expected = FrameError::LENGTH; }
        else if (failure == 4) { length = 0; expected = FrameError::LENGTH; }
        else { length = sizeof(reply); expected = FrameError::LENGTH; }
        IdentityObservation previous; previous.rawModel = 0xA55A; const Saved<IdentityObservation> unchanged(previous);
        assert(advanceRead(c, frame(c, reply, length), 230));
        assert(c.state == ReadState::FAILED && c.outcome == ReadOutcome::REPLY_ERROR && c.completedSteps == 0);
        assert(c.observations[0].frameError == expected && c.observations[0].receivedLength == length);
        assert(c.observations[0].length == (length > READ_MAX_REPLY_BYTES ? READ_MAX_REPLY_BYTES : length));
        assert(!getIdentity(c, previous)); unchanged.check(previous);
    }
    ReadContext c = config(); consumeStep(c, 0);
    ReadEvent impossible = frame(c, SERIAL, sizeof(SERIAL), 150, 180);
    const Saved<ReadContext> unchanged(c);
    assert(!advanceRead(c, impossible, 330)); unchanged.check(c);
    PreparedRead next; const Saved<PreparedRead> unchangedRequest(next);
    assert(!nextRead(c, c.servicedUs - 1, next)); unchangedRequest.check(next);
    ReadEvent earlierDelivery = frame(c, SERIAL, sizeof(SERIAL), 240, 250);
    assert(!advanceRead(c, earlierDelivery, c.servicedUs - 1)); unchanged.check(c);
    ReadEvent transport = control(c, ReadEventKind::TRANSPORT_FAILURE); transport.transportDetail = -77;
    assert(advanceRead(c, transport, 330));
    assert(c.outcome == ReadOutcome::TRANSPORT_ERROR && c.observations[1].transportDetail == -77);
}
void testTransportProvenanceIsBoundedOwnedAndNotAcknowledgement() {
    ReadContext c = config(); consumeStep(c, 0);
    uint8_t prefix[40]; for (unsigned i = 0; i < sizeof(prefix); ++i) prefix[i] = static_cast<uint8_t>(i + 1);
    ReadEvent failed = control(c, ReadEventKind::TRANSPORT_FAILURE);
    failed.frame = prefix; failed.length = sizeof(prefix); failed.transportDetail = -123;
    failed.txAccepted = 5; failed.executionUnknown = true;
    ConfigObservation previous = completedConfig(); const Saved<ConfigObservation> retained(previous);
    assert(advanceRead(c, failed, 330));
    assert(c.state == ReadState::FAILED && c.outcome == ReadOutcome::TRANSPORT_ERROR && c.completedSteps == 1);
    const ReadStepObservation& evidence = c.observations[1];
    assert(evidence.receivedLength == 40 && evidence.length == 37 && evidence.transportDetail == -123);
    assert(evidence.txAccepted == 5 && evidence.executionUnknown && !evidence.qualified);
    std::memset(prefix, 0, sizeof(prefix));
    for (unsigned i = 0; i < evidence.length; ++i) assert(evidence.raw[i] == i + 1);
    assert(!getConfig(c, previous)); retained.check(previous);
    for (unsigned kind = 0; kind < 2; ++kind) {
        c = identity(); ReadEvent local = control(c, kind ? ReadEventKind::DEADLINE : ReadEventKind::CANCEL);
        local.txAccepted = 8; local.executionUnknown = true; local.transportDetail = 17;
        uint8_t partial[] = {1, 3, 8}; local.frame = partial; local.length = sizeof(partial);
        assert(advanceRead(c, local, kind ? 1000 : 230));
        assert(c.state == ReadState::FAILED && c.observations[0].txAccepted == 8 && c.observations[0].executionUnknown);
        assert(c.observations[0].transportDetail == 17 && c.observations[0].length == 3 && c.observations[0].receivedLength == 3);
        std::memset(partial, 0, sizeof(partial));
        assert(c.observations[0].raw[0] == 1 && c.observations[0].raw[1] == 3 && c.observations[0].raw[2] == 8);
        assert(!c.observations[0].qualified && c.observations[0].earliestUs == 0 && c.observations[0].latestUs == 0);
    }
    c = identity(); ReadEvent impossible = frame(c, IDENTITY, sizeof(IDENTITY)); impossible.txAccepted = 9;
    const Saved<ReadContext> unchanged(c); assert(!advanceRead(c, impossible, 230)); unchanged.check(c);
    impossible.txAccepted = 8; impossible.transportDetail = 99;
    assert(advanceRead(c, impossible, 230));
    assert(c.state == ReadState::SUCCEEDED && c.outcome == ReadOutcome::SUCCESS);
    assert(c.observations[0].txAccepted == 8 && c.observations[0].transportDetail == 99);
}
void testUnknownIdentityAndBoundaryTargetsArePreserved() {
    ReadContext c = identity(); uint8_t future[sizeof(IDENTITY)]; std::memcpy(future, IDENTITY, sizeof(future));
    std::memset(future + 3, 0xFF, 8); seal(future, sizeof(future));
    assert(advanceRead(c, frame(c, future, sizeof(future)), 230));
    IdentityObservation out; assert(getIdentity(c, out));
    assert(out.rawModel == 0xFFFF && out.rawVersion == 0xFFFF && out.rawDip == 0xFFFF);
    assert(out.rawActiveNode == 0xFFFF && !out.activeNodeKnown);
    ReadTarget boundary = target(); boundary.id = boundary.generation = std::numeric_limits<uint32_t>::max(); boundary.address = 247;
    assert(prepareIdentity(c, boundary, std::numeric_limits<uint32_t>::max(), 100, 1000));
    PreparedRead r; assert(nextRead(c, 100, r)); assert(r.bytes[0] == 247 && r.target.address == 247);
    assert(r.target.id == boundary.id && r.target.generation == boundary.generation && r.operationId == boundary.id);
    uint8_t reply[sizeof(IDENTITY)]; std::memcpy(reply, IDENTITY, sizeof(reply)); reply[0] = 247; seal(reply, sizeof(reply));
    assert(advanceRead(c, frame(c, reply, sizeof(reply)), 230) && getIdentity(c, out));
    assert(out.target.address == 247 && out.rawActiveNode == 1); // Reported active-node register remains independent evidence.
}
void testCapabilitiesAndFixedStorage() {
    const ReadCapabilities caps = readCapabilities();
    assert(caps.probe && caps.identity && caps.config && caps.maxSteps == 5 && caps.maxReplyBytes <= 37);
    static_assert(READ_MAX_STEPS == 5 && READ_INPUT_COUNT == 4 && READ_MAX_REPLY_BYTES == 37, "Reviewed read bounds");
    static_assert(sizeof(ReadContext) < 2048 && sizeof(ConfigObservation) < 2048, "Fixed bounded read storage");
}
}
int main() {
    testIdentityAndOwnedProvenance(); testConfigWindowsPublicationAndIndependentEvidence();
    testPreparationAndPublicationFailuresPreserveOutputs(); testWrongEnvelopesAndSameLengthStepsRejectWithoutMutation();
    testPartialFailuresPreservePreviousObservation(); testDeadlineUsesQualifiedClosureAndNeverRenewsBudget();
    testUnknownEnumsAndZeroEncoderRemainRawEvidence(); testMalformedRepliesRetainFailureWithoutPublishing();
    testTransportProvenanceIsBoundedOwnedAndNotAcknowledgement(); testUnknownIdentityAndBoundaryTargetsArePreserved();
    testCapabilitiesAndFixedStorage();
}
