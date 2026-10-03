#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace MotorControlRS::ESS_RS;

// These intervals are independently transcribed from ESS appendix pages 68-79.
// Adjacent pairs count as words, not two independently writable quantities.
static bool documentedWord(uint16_t address) {
    struct Interval { uint16_t first; uint16_t last; };
    const Interval intervals[] = {
        {0x0000, 0x0003}, {0x0006, 0x000C}, {0x0010, 0x0011},
        {0x0013, 0x0015}, {0x0017, 0x0019}, {0x001D, 0x0025},
        {0x0027, 0x0027}, {0x002D, 0x002D}, {0x0030, 0x003C},
        {0x0040, 0x0044}, {0x004B, 0x004D}, {0x004F, 0x0051},
        {0x0060, 0x00EF}, {0x0100, 0x0119}, {0x0122, 0x0123},
        {0x0130, 0x013F}
    };
    for (std::size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); ++i) {
        if (address >= intervals[i].first && address <= intervals[i].last) return true;
    }
    return false;
}

static void testCoverageAndLookup() {
    assert(registerCount() == 221);
    assert(registerAt(registerCount()) == nullptr);
    assert(registerAt(static_cast<std::size_t>(-1)) == nullptr);
    std::size_t words = 0;
    std::size_t pairs = 0;
    std::size_t reserved = 0;
    uint32_t previousEnd = 0;
    for (std::size_t i = 0; i < registerCount(); ++i) {
        const RegisterDescriptor* entry = registerAt(i);
        assert(entry != nullptr);
        assert(entry->wordCount == 1 || entry->wordCount == 2);
        assert(i == 0 || entry->address >= previousEnd);
        assert(entry->name && entry->name[0]);
        assert(entry->unit && entry->sourceRange && entry->sourceDefault && entry->sourceAccess && entry->notes);
        assert(entry->sourcePage >= 68 && entry->sourcePage <= 79);
        assert(findRegister(entry->address) == entry);
        if (entry->wordCount == 2) {
            ++pairs;
            assert(findRegister(static_cast<uint16_t>(entry->address + 1)) == entry);
        }
        words += entry->wordCount;
        if (entry->access == RegisterAccess::RESERVED) ++reserved;
        previousEnd = static_cast<uint32_t>(entry->address) + entry->wordCount;
    }
    assert(words == 242 && pairs == 21 && reserved == 16);
    for (uint32_t word = 0; word <= 0xFFFF; ++word) {
        const uint16_t address = static_cast<uint16_t>(word);
        assert((findRegister(address) != nullptr) == documentedWord(address));
    }
}

static void testAccessAndUncertainty() {
    assert(findRegister(0x0000)->access == RegisterAccess::READ_ONLY);
    assert(findRegister(0x000A)->access == RegisterAccess::READ_ONLY);
    assert(findRegister(0x0027)->access == RegisterAccess::WRITE_ONLY);
    assert(findRegister(0x002D)->access == RegisterAccess::WRITE_ONLY);
    assert(findRegister(0x0011)->access == RegisterAccess::READ_WRITE);
    assert(findRegister(0x003B)->access == RegisterAccess::UNSPECIFIED);
    assert(findRegister(0x003C)->access == RegisterAccess::UNSPECIFIED);
    assert(findRegister(0x0122)->access == RegisterAccess::READ_WRITE);
    assert(findRegister(0x003B) != findRegister(0x0122));
    assert(hasRegisterIssue(*findRegister(0x003B), RegisterIssue::DUPLICATE_MAPPING));
    assert(hasRegisterIssue(*findRegister(0x003C), RegisterIssue::ACCESS_UNSPECIFIED));
    assert(hasRegisterIssue(*findRegister(0x001D), RegisterIssue::RANGE_MALFORMED));
    assert(hasRegisterIssue(*findRegister(0x0021), RegisterIssue::DEFAULT_CONFLICT));
    assert(hasRegisterIssue(*findRegister(0x0024), RegisterIssue::SIGNED_ENCODING_UNRESOLVED));
    assert(hasRegisterIssue(*findRegister(0x000C), RegisterIssue::UNIT_UNSPECIFIED));
    assert(!hasRegisterIssue(*findRegister(0x0000), RegisterIssue::NONE));
    assert(findRegister(0x0035)->wordOrder == RegisterWordOrder::UNRESOLVED);
    assert(findRegister(0x0037)->wordOrder == RegisterWordOrder::CONFIGURABLE);
    assert(std::strcmp(findRegister(0x0021)->sourceDefault, "50 (100 ms)") == 0);
    assert(std::strcmp(findRegister(0x0101)->sourceAccess, "RW/S") == 0);
    assert(!hasRegisterIssue(*findRegister(0x0101), RegisterIssue::SCALE_UNRESOLVED));
    assert(findRegister(0x2042) == nullptr); // Misplaced current-base reference is not an alias.
}

static void testCodecAccessMap() {
    // The compact codec policy must match catalogue access/width throughout the
    // address space, without treating undefined holes as reserved/readable words.
    for (uint32_t address = 0; address <= 0xFFFF; ++address) {
        const uint16_t word = static_cast<uint16_t>(address);
        const RegisterDescriptor* entry = findRegister(word);
        const bool readable = entry && (entry->access == RegisterAccess::READ_ONLY ||
                                        entry->access == RegisterAccess::READ_WRITE);
        const bool singleWrite = entry && entry->wordCount == 1 &&
            (entry->access == RegisterAccess::WRITE_ONLY || entry->access == RegisterAccess::READ_WRITE);
        assert(isReadRangeValid(word, 1) == readable);
        assert(validateWriteSingleRegisterRequest(1, word, 0).isOk() == singleWrite);
    }
    for (uint16_t start = 0; start <= 0x013F; ++start) {
        for (uint16_t count = 1; count <= 16; ++count) {
            bool readable = true;
            for (uint16_t offset = 0; offset < count; ++offset) {
                const RegisterDescriptor* entry = findRegister(start + offset);
                readable = readable && entry && (entry->access == RegisterAccess::READ_ONLY ||
                                                  entry->access == RegisterAccess::READ_WRITE);
            }
            assert(isReadRangeValid(start, count) == readable);
        }
    }
}

static void testIndexedFields() {
    assert(positionSegmentRegister(1, PositionSegmentField::PULSES)->address == 0x0060);
    assert(positionSegmentRegister(16, PositionSegmentField::PULSES)->address == 0x00BA);
    assert(positionSegmentRegister(16, PositionSegmentField::DECELERATION_TIME)->address == 0x00BE);
    assert(positionSegmentRegister(16, PositionSegmentField::RESERVED)->address == 0x00BF);
    assert(speedSegmentRegister(1, SpeedSegmentField::SPEED)->address == 0x00C0);
    assert(speedSegmentRegister(16, SpeedSegmentField::SPEED)->address == 0x00ED);
    assert(speedSegmentRegister(16, SpeedSegmentField::DECELERATION_TIME)->address == 0x00EF);
    assert(segmentStartSpeedRegister(1)->address == 0x0130);
    assert(segmentStartSpeedRegister(16)->address == 0x013F);
    for (uint8_t segment = 1; segment <= 16; ++segment) {
        assert(positionSegmentRegister(segment, PositionSegmentField::PULSES)->wordCount == 2);
        assert(positionSegmentRegister(segment, PositionSegmentField::RESERVED)->access == RegisterAccess::RESERVED);
        assert(speedSegmentRegister(segment, SpeedSegmentField::SPEED)->signedness == RegisterSignedness::SIGNED_ENCODING_UNRESOLVED);
    }
    assert(positionSegmentRegister(0, PositionSegmentField::PULSES) == nullptr);
    assert(positionSegmentRegister(17, PositionSegmentField::PULSES) == nullptr);
    assert(positionSegmentRegister(1, static_cast<PositionSegmentField>(1)) == nullptr);
    assert(positionSegmentRegister(1, static_cast<PositionSegmentField>(255)) == nullptr);
    assert(speedSegmentRegister(0, SpeedSegmentField::SPEED) == nullptr);
    assert(speedSegmentRegister(17, SpeedSegmentField::SPEED) == nullptr);
    assert(speedSegmentRegister(1, static_cast<SpeedSegmentField>(3)) == nullptr);
    assert(segmentStartSpeedRegister(0) == nullptr);
    assert(segmentStartSpeedRegister(17) == nullptr);
    assert(inputFunctionRegister(0)->address == 0x0041);
    assert(inputFunctionRegister(3)->address == 0x0044);
    assert(inputFunctionRegister(4) == nullptr);
    assert(outputFunctionRegister(0)->address == 0x004C);
    assert(outputFunctionRegister(1)->address == 0x004D);
    assert(outputFunctionRegister(2) == nullptr);
}

static void testNamedChoices() {
    // Independent values from source command tables, not frame-generation oracles.
    assert(static_cast<uint16_t>(MotionCommandBit::START_POSITION) == 0x0001);
    assert(static_cast<uint16_t>(MotionCommandBit::START_SPEED) == 0x0002);
    assert(static_cast<uint16_t>(MotionCommandBit::START_HOMING) == 0x0010);
    assert(static_cast<uint16_t>(MotionCommandBit::STOP) == 0x0100);
    assert(static_cast<uint16_t>(MotionCommandBit::EMERGENCY_STOP) == 0x0200);
    assert(static_cast<uint16_t>(AuxiliaryCommand::RELEASE) == 0x0011);
    assert(static_cast<uint16_t>(AuxiliaryCommand::ENABLE) == 0x0012);
    assert(static_cast<uint16_t>(AuxiliaryCommand::CLEAR_ALARM) == 0x0021);
    assert(static_cast<uint16_t>(AuxiliaryCommand::CLEAR_POSITION) == 0x0031);
    assert(static_cast<uint16_t>(AuxiliaryCommand::RESTORE_FACTORY) == 0x0041);
    assert(static_cast<uint16_t>(AuxiliaryCommand::SAVE_PARAMETERS) == 0x0042);
    assert(static_cast<uint16_t>(MotionStatusBit::RELEASED) == 0x0010);
    assert(static_cast<uint16_t>(InputFunction::JOG_POSITIVE) == 9);
    assert(static_cast<uint16_t>(InputFunction::JOG_NEGATIVE) == 10);
    assert(static_cast<uint16_t>(OutputFunction::CUSTOM_0) == 9);
    assert(static_cast<uint16_t>(WordOrder::HIGH_WORD_FIRST) == 0);
    assert(static_cast<uint16_t>(WordOrder::LOW_WORD_FIRST) == 1);
    assert(homingMethodCount() == 35);
    for (int16_t method = -5; method <= 36; ++method) {
        const bool listed = (method >= -4 && method <= -1) ||
            (method >= 1 && method <= 14) || (method >= 17 && method <= 30) ||
            (method >= 33 && method <= 35);
        const HomingMethodDescriptor* entry = findHomingMethod(method);
        assert((entry != nullptr) == listed);
        if (entry) {
            assert(entry->method == method);
            assert(entry->requiresMotorIndex == ((method >= 1 && method <= 14) || method == 33 || method == 34));
            if (method < 0) assert(entry->semanticsUnresolved);
        }
    }
    assert(findHomingMethod(-32768) == nullptr);
    assert(findHomingMethod(32767) == nullptr);
}

int main() {
    testCoverageAndLookup();
    testAccessAndUncertainty();
    testCodecAccessMap();
    testIndexedFields();
    testNamedChoices();
    return 0;
}
