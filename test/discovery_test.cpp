// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Discovery.h>
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <limits>

using namespace MotorControlRS;
namespace {
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& value) { std::memcpy(bytes, &value, sizeof(T)); }
    void check(const T& value) const { assert(!std::memcmp(bytes, &value, sizeof(T))); }
};
ReadTarget target() { ReadTarget t; t.id = 7; t.address = 1; t.generation = 9; return t; }
ActiveSerialTuple tuple() {
    ActiveSerialTuple t; t.known = true; t.baud = 115200; t.dataBits = 8;
    t.parity = SerialParity::NONE; t.stopBits = 1; return t;
}
ESS_RS::PreparedProbe prepared() {
    ESS_RS::PreparedProbe p;
    assert(MotorControlRS::prepareProbe(p, DriveProfile::ESS_RS, target(), 23, 100, 1000, tuple()));
    return p;
}
ReadEvent frame(const ESS_RS::PreparedProbe& p, const uint8_t* raw, std::size_t length) {
    ReadEvent e; e.target = p.target; e.operationId = p.operationId;
    e.frame = raw; e.length = length; e.qualified = true;
    e.earliestUs = 200; e.latestUs = 220; e.txAccepted = 8; return e;
}
ReadEvent local(const ESS_RS::PreparedProbe& p, ReadEventKind kind) {
    ReadEvent e; e.target = p.target; e.operationId = p.operationId; e.kind = kind; return e;
}
void seal(uint8_t* bytes, std::size_t size) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size - 2; ++i) {
        crc ^= bytes[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xA001 : 0);
    }
    bytes[size - 2] = static_cast<uint8_t>(crc); bytes[size - 1] = static_cast<uint8_t>(crc >> 8);
}
void inventoryAndPreparation() {
    assert(discoveryProfileCount() == 1);
    DiscoveryProfile profile; assert(getDiscoveryProfile(0, profile));
    assert(profile.manufacturer == Manufacturer::STEPPERONLINE && profile.profile == DriveProfile::ESS_RS);
    assert(!std::strcmp(profile.manufacturerId, "stepperonline") && !std::strcmp(profile.profileId, "ess_rs"));
    Saved<DiscoveryProfile> savedProfile(profile);
    assert(getDiscoveryProfile(1, profile).code == Err::UNSUPPORTED); savedProfile.check(profile);
    assert(getDiscoveryProfile(std::numeric_limits<std::size_t>::max(), profile).code == Err::UNSUPPORTED);
    DiscoveryCapabilities caps; assert(getDiscoveryCapabilities(DriveProfile::ESS_RS, caps));
    assert(caps.probe && caps.identity && caps.nonChanging && !caps.exactModel && !caps.firmware);
    assert(caps.minimumAddress == 1 && caps.maximumAddress == 247);
    assert(caps.probeFirst == 0 && caps.probeCount == 1 && caps.identityFirst == 0 && caps.identityCount == 4);
    assert(caps.probeRequestBytes == 8 && caps.probeReplyBytes == 7 && caps.exceptionReplyBytes == 5);
    Saved<DiscoveryCapabilities> savedCaps(caps);
    assert(getDiscoveryCapabilities(static_cast<DriveProfile>(1), caps).code == Err::UNSUPPORTED); savedCaps.check(caps);
    ESS_RS::PreparedProbe p = prepared(); Saved<ESS_RS::PreparedProbe> saved(p);
    const uint8_t expected[] = {1, 3, 0, 0, 0, 1, 0x84, 0x0A};
    assert(p.length == 8 && !std::memcmp(p.bytes, expected, 8));
    for (unsigned field = 0; field < 3; ++field) {
        ReadTarget bad = target();
        if (field == 0) bad.id = 0; else if (field == 1) bad.generation = 0; else bad.address = 0;
        assert(!MotorControlRS::prepareProbe(p, DriveProfile::ESS_RS, bad, 23, 100, 1000, tuple())); saved.check(p);
    }
    ReadTarget bad = target(); bad.address = 248;
    assert(!MotorControlRS::prepareProbe(p, DriveProfile::ESS_RS, bad, 23, 100, 1000)); saved.check(p);
    assert(!MotorControlRS::prepareProbe(p, static_cast<DriveProfile>(255), target(), 23, 100, 1000)); saved.check(p);
    assert(!MotorControlRS::prepareProbe(p, DriveProfile::ESS_RS, target(), 0, 100, 1000)); saved.check(p);
    assert(!MotorControlRS::prepareProbe(p, DriveProfile::ESS_RS, target(), 23, 100, 100)); saved.check(p);
    ActiveSerialTuple badTuple = tuple(); badTuple.parity = SerialParity::UNKNOWN;
    assert(!MotorControlRS::prepareProbe(p, DriveProfile::ESS_RS, target(), 23, 100, 1000, badTuple)); saved.check(p);
    assert(MotorControlRS::prepareProbe(p, DriveProfile::ESS_RS, target(), 23, 100, 1000)); assert(!p.activeSerial.known);
    for (unsigned address : {1U, 247U}) {
        ReadTarget t = target(); t.address = static_cast<uint8_t>(address);
        assert(MotorControlRS::prepareProbe(p, DriveProfile::ESS_RS, t, 23, 100, 1000));
        assert(p.bytes[0] == address && p.bytes[1] == 3 && p.bytes[2] == 0 && p.bytes[3] == 0 && p.bytes[5] == 1);
    }
}
void exactReplyAndUnknownIdentity() {
    const ESS_RS::PreparedProbe p = prepared();
    for (uint16_t code : {uint16_t(0), uint16_t(0x305), uint16_t(0x4EEA), uint16_t(0xFFFF)}) {
        uint8_t raw[] = {1, 3, 2, static_cast<uint8_t>(code >> 8), static_cast<uint8_t>(code), 0, 0}; seal(raw, sizeof(raw));
        ESS_RS::ProbeObservation out; assert(MotorControlRS::checkProbe(p, frame(p, raw, sizeof(raw)), 230, out));
        assert(out.outcome == ProbeOutcome::RESPONDER && out.rawModelKnown && out.rawModel == code);
        assert(out.confidence == ProbeConfidence::RESPONDER_MODEL_UNRESOLVED);
        assert(out.modelResolution == ESS_RS::ReadResolution::MODEL_MAPPING_UNRESOLVED);
        assert(out.target.id == 7 && out.target.generation == 9 && out.activeSerial.baud == 115200);
        assert(out.provenance.receivedLength == 7 && out.provenance.length == 7);
        assert(!std::memcmp(out.provenance.raw, raw, 7)); std::memset(raw, 0, sizeof(raw));
        assert(out.provenance.raw[1] == 3); // Frame lifetime ends at the call.
    }
    // Identity refinement remains the existing typed non-changing four-word read.
    ESS_RS::ReadContext identity;
    assert(ESS_RS::prepareIdentity(identity, target(), 24, 100, 1000, tuple()));
    ESS_RS::PreparedRead query; assert(ESS_RS::nextRead(identity, 100, query));
    const uint8_t expected[] = {1, 3, 0, 0, 0, 4, 0x44, 9};
    assert(query.length == 8 && !std::memcmp(query.bytes, expected, 8));
    uint8_t raw[] = {1, 3, 8, 0x4E, 0xEA, 0x12, 0x34, 0, 2, 0xA5, 0x81, 0, 0}; seal(raw, sizeof(raw));
    ReadEvent e = frame(p, raw, sizeof(raw)); e.operationId = 24;
    assert(ESS_RS::advanceRead(identity, e, 230));
    ESS_RS::IdentityObservation observed; assert(ESS_RS::getIdentity(identity, observed));
    assert(observed.rawModel == 0x4EEA && observed.rawVersion == 0x1234 && observed.rawActiveNode == 2);
    assert(observed.modelResolution == ESS_RS::ReadResolution::MODEL_MAPPING_UNRESOLVED);
    assert(observed.versionResolution == ESS_RS::ReadResolution::VERSION_MAPPING_UNRESOLVED);
    // Inconsistent active node remains observable for the application to report.
    assert(observed.activeNodeKnown && observed.activeNode != observed.target.address);
}
void rejectedEnvelopes() {
    const ESS_RS::PreparedProbe p = prepared();
    uint8_t raw[] = {1, 3, 2, 3, 5, 0, 0}; seal(raw, sizeof(raw));
    ESS_RS::ProbeObservation out; out.rawModel = 0xABCD; Saved<ESS_RS::ProbeObservation> saved(out);
    for (unsigned variant = 0; variant < 13; ++variant) {
        ReadEvent e = frame(p, raw, sizeof(raw));
        switch (variant) {
        case 0: ++e.target.id; break; case 1: ++e.target.address; break;
        case 2: ++e.target.generation; break; case 3: ++e.operationId; break;
        case 4: e.step = 1; break; case 5: e.txAccepted = 9; break;
        case 6: e.frame = nullptr; break; case 7: e.earliestUs = 99; break;
        case 8: e.latestUs = 199; break; case 9: e.latestUs = 231; break;
        case 10: e.qualified = false; break; case 11: e.kind = static_cast<ReadEventKind>(99); break;
        case 12: e.kind = ReadEventKind::CANCEL; break;
        }
        assert(!MotorControlRS::checkProbe(p, e, 230, out)); saved.check(out);
    }
    assert(!MotorControlRS::checkProbe(p, local(p, ReadEventKind::DEADLINE), 999, out)); saved.check(out);
    assert(!MotorControlRS::checkProbe(p, local(p, ReadEventKind::CANCEL), 99, out)); saved.check(out);
    ESS_RS::PreparedProbe changed = p; changed.bytes[1] = 6;
    assert(!MotorControlRS::checkProbe(changed, frame(p, raw, sizeof(raw)), 230, out)); saved.check(out);
}
void failureEvidence() {
    const ESS_RS::PreparedProbe p = prepared(); ESS_RS::ProbeObservation out;
    uint8_t raw[] = {1, 3, 2, 3, 5, 0, 0}; seal(raw, sizeof(raw));
    auto expect = [&](ReadEvent e, uint64_t now, ProbeOutcome outcome) {
        assert(MotorControlRS::checkProbe(p, e, now, out)); assert(out.outcome == outcome);
        assert(!out.rawModelKnown && out.confidence == ProbeConfidence::NONE);
        assert(!out.status && out.provenance.status.code == out.status.code);
    };
    uint8_t wrong[7]; std::memcpy(wrong, raw, 7); wrong[0] = 2; seal(wrong, 7);
    expect(frame(p, wrong, 7), 230, ProbeOutcome::MISMATCH);
    assert(out.provenance.frameError == ESS_RS::FrameError::ADDRESS);
    wrong[0] = 1; wrong[1] = 4; seal(wrong, 7);
    expect(frame(p, wrong, 7), 230, ProbeOutcome::MALFORMED);
    wrong[1] = 3; wrong[2] = 4; seal(wrong, 7);
    expect(frame(p, wrong, 7), 230, ProbeOutcome::MALFORMED);
    wrong[2] = 2; seal(wrong, 7); wrong[6] ^= 1;
    expect(frame(p, wrong, 7), 230, ProbeOutcome::MALFORMED); assert(out.status.code == Err::CRC_ERROR);
    expect(frame(p, raw, 6), 230, ProbeOutcome::MALFORMED);
    uint8_t longFrame[48] = {}; expect(frame(p, longFrame, sizeof(longFrame)), 230, ProbeOutcome::MALFORMED);
    assert(out.provenance.receivedLength == 48 && out.provenance.length == 37);
    uint8_t exception[] = {1, 0x83, 0xE7, 1, 0x7A};
    assert(MotorControlRS::checkProbe(p, frame(p, exception, 5), 230, out));
    assert(out.outcome == ProbeOutcome::EXCEPTION && out.confidence == ProbeConfidence::RESPONDER_ONLY);
    assert(out.status.code == Err::EXCEPTION && out.status.detail == 0xE7 && !out.rawModelKnown);
    ReadEvent e = frame(p, raw, 7); e.qualified = false; e.earliestUs = e.latestUs = 0;
    expect(e, 230, ProbeOutcome::TIMING_UNQUALIFIED);
    e = frame(p, raw, 7); e.latestUs = 1001; expect(e, 1200, ProbeOutcome::DEADLINE);
    e.latestUs = 1000; assert(MotorControlRS::checkProbe(p, e, 1200, out)); assert(out.outcome == ProbeOutcome::RESPONDER);
    expect(local(p, ReadEventKind::DEADLINE), 1000, ProbeOutcome::NO_RESPONSE);
    e = local(p, ReadEventKind::DEADLINE); e.frame = raw; e.length = 3; expect(e, 1000, ProbeOutcome::DEADLINE);
    e = local(p, ReadEventKind::TRANSPORT_FAILURE); e.frame = raw; e.length = 3;
    e.transportDetail = 918; e.txAccepted = 4; e.executionUnknown = true;
    expect(e, 230, ProbeOutcome::TRANSPORT_ERROR);
    assert(out.provenance.transportDetail == 918 && out.provenance.txAccepted == 4 && out.provenance.executionUnknown);
    assert(out.provenance.length == 3 && !std::memcmp(out.provenance.raw, raw, 3));
    expect(local(p, ReadEventKind::CANCEL), 230, ProbeOutcome::CANCELLED);
}
void commonAndNativeRoutes() {
    const ESS_RS::PreparedProbe common = prepared();
    ESS_RS::PreparedProbe native;
    assert(ESS_RS::prepareProbe(native, target(), 23, 100, 1000, tuple()));
    assert(native.length == common.length && !std::memcmp(native.bytes, common.bytes, common.length));
    assert(native.profile == common.profile && native.operationId == common.operationId);
    assert(native.target.id == common.target.id && native.target.address == common.target.address &&
        native.target.generation == common.target.generation);
    assert(native.startedUs == common.startedUs && native.deadlineUs == common.deadlineUs);
    assert(native.activeSerial.known && native.activeSerial.baud == common.activeSerial.baud &&
        native.activeSerial.dataBits == common.activeSerial.dataBits &&
        native.activeSerial.parity == common.activeSerial.parity && native.activeSerial.stopBits == common.activeSerial.stopBits);
    const DiscoveryCapabilities caps = ESS_RS::probeCapabilities();
    assert(caps.probe && caps.nonChanging && caps.probeRequestBytes == 8 && caps.probeReplyBytes == 7);

    // Both public routes consume the same independently sealed success, exception
    // and malformed frames, retain their provenance and never infer identity.
    uint8_t response[] = {1, 3, 2, 0x4E, 0xEA, 0, 0}; seal(response, sizeof(response));
    uint8_t exception[] = {1, 0x83, 2, 0, 0}; seal(exception, sizeof(exception));
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        if (scenario == 2) response[6] ^= 1;
        const auto event = scenario == 1 ? frame(common, exception, sizeof(exception)) :
            frame(common, response, sizeof(response));
        ESS_RS::ProbeObservation a, b;
        assert(MotorControlRS::checkProbe(common, event, 230, a));
        assert(ESS_RS::checkProbe(native, event, 230, b));
        const ProbeOutcome expected[] = {ProbeOutcome::RESPONDER, ProbeOutcome::EXCEPTION, ProbeOutcome::MALFORMED};
        assert(a.outcome == expected[scenario] && b.outcome == a.outcome);
        assert(a.confidence == b.confidence && a.rawModelKnown == b.rawModelKnown && a.rawModel == b.rawModel);
        assert(a.modelResolution == b.modelResolution && a.status.code == b.status.code && a.status.detail == b.status.detail);
        assert(a.provenance.frameError == b.provenance.frameError && a.provenance.length == b.provenance.length &&
            a.provenance.receivedLength == b.provenance.receivedLength &&
            a.provenance.attemptedUs == b.provenance.attemptedUs && a.provenance.earliestUs == b.provenance.earliestUs &&
            a.provenance.latestUs == b.provenance.latestUs && a.provenance.deliveredUs == b.provenance.deliveredUs &&
            a.provenance.txAccepted == b.provenance.txAccepted && a.provenance.qualified == b.provenance.qualified);
        assert(!std::memcmp(a.provenance.raw, b.provenance.raw, a.provenance.length));
    }
    native.profile = static_cast<DriveProfile>(255);
    ESS_RS::ProbeObservation unchanged; unchanged.rawModel = 0xA55A;
    Saved<ESS_RS::ProbeObservation> saved(unchanged);
    assert(ESS_RS::checkProbe(native, frame(native, response, sizeof(response)), 230, unchanged).code == Err::UNSUPPORTED);
    saved.check(unchanged);
    assert(MotorControlRS::checkProbe(native, frame(native, response, sizeof(response)), 230, unchanged).code == Err::UNSUPPORTED);
    saved.check(unchanged);
}
} // namespace
int main() { inventoryAndPreparation(); exactReplyAndUnknownIdentity(); rejectedEnvelopes(); failureEvidence(); commonAndNativeRoutes(); }
