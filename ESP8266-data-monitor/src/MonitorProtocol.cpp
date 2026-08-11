#include "MonitorProtocol.h"

#include <string.h>

namespace monitor_protocol {
namespace {

constexpr uint8_t kRequestPrefix[] = {0x7E, 0x00, 0x02, 0x02, 0x4B, 0x10};
constexpr uint8_t kDescriptors[] = {
    0x39, 0x00, 0x01,  0xF5, 0x00, 0x01,  0xC0, 0x00, 0x01,
    0xCB, 0x00, 0x01,  0x69, 0x00, 0x02,  0xEF, 0x00, 0x04,
    0x77, 0x00, 0x02,  0x71, 0x00, 0x02,  0xE0, 0x00, 0x02,
    0x73, 0x00, 0x02,  0xDC, 0x00, 0x02,  0xDE, 0x00, 0x01,
    0xDF, 0x00, 0x01,  0x5A, 0x00, 0x01,  0x6A, 0x04, 0x02,
    0x6C, 0x04, 0x02,  0xE8, 0x00, 0x01,  0xE9, 0x00, 0x01,
    0xEA, 0x00, 0x01,  0xEB, 0x00, 0x01,  0xEC, 0x00, 0x01,
    0xED, 0x00, 0x01,  0xEE, 0x00, 0x01,  0xF3, 0x00, 0x01,
    0xF4, 0x00, 0x01
};

static_assert(sizeof(kRequestPrefix) + sizeof(kDescriptors) + 3 == kRequestSize,
              "Captured request size changed");

uint16_t readU16(const uint8_t* value) {
    return static_cast<uint16_t>(value[0]) |
           (static_cast<uint16_t>(value[1]) << 8U);
}

int16_t readI16(const uint8_t* value) {
    return static_cast<int16_t>(readU16(value));
}

uint32_t readU32(const uint8_t* value) {
    return static_cast<uint32_t>(value[0]) |
           (static_cast<uint32_t>(value[1]) << 8U) |
           (static_cast<uint32_t>(value[2]) << 16U) |
           (static_cast<uint32_t>(value[3]) << 24U);
}

}  // namespace

uint16_t computeChecksum(const uint8_t* frame, size_t dataEndExclusive) {
    uint16_t sum = 0;
    for (size_t index = 1; index < dataEndExclusive; ++index) {
        sum = static_cast<uint16_t>(sum + frame[index]);
    }
    return static_cast<uint16_t>(0U - sum);
}

void buildBatchReadRequest(uint8_t (&output)[kRequestSize]) {
    memcpy(output, kRequestPrefix, sizeof(kRequestPrefix));
    memcpy(output + sizeof(kRequestPrefix), kDescriptors, sizeof(kDescriptors));

    constexpr size_t checksumOffset = kRequestSize - 3;
    const uint16_t checksum = computeChecksum(output, checksumOffset);
    output[checksumOffset] = static_cast<uint8_t>(checksum & 0xFFU);
    output[checksumOffset + 1] = static_cast<uint8_t>(checksum >> 8U);
    output[kRequestSize - 1] = kTerminator;
}

bool validateFrame(const uint8_t* frame, size_t length) {
    if (frame == nullptr || length < 9 || length > kMaximumFrameSize ||
        frame[0] != kHeader || frame[length - 1] != kTerminator) {
        return false;
    }

    const size_t expectedLength = static_cast<size_t>(frame[4]) + 9U;
    if (length != expectedLength) {
        return false;
    }

    const size_t checksumOffset = length - 3;
    const uint16_t expected = computeChecksum(frame, checksumOffset);
    const uint16_t actual = readU16(frame + checksumOffset);
    return expected == actual;
}

bool decodeResponse(const uint8_t* frame, size_t length, MonitorData& output) {
    if (!validateFrame(frame, length) || length != kResponseSize ||
        frame[1] != 0x00 || frame[2] != 0x02 || frame[3] != 0x82 ||
        frame[4] != kResponseDataSize || frame[5] != 0xA0) {
        return false;
    }

    const uint8_t* data = frame + 6;
    output.driveStateRaw = data[0];
    output.runningStateRaw = data[1];
    output.vehicleGearRaw = data[2];
    output.vehicleModeRaw = data[3];
    output.motorSpeedRpm = readI16(data + 4);
    output.faultCode = readU32(data + 6);
    output.dcCurrentRaw = readI16(data + 10);
    output.dcVoltageRaw = readU16(data + 12);
    output.phaseCurrentRaw = readI16(data + 14);
    output.motorPowerRaw = readI16(data + 16);
    output.throttleVoltageRaw = readU16(data + 18);
    output.mcuTemperatureRaw = data[20];
    output.motorTemperatureRaw = data[21];
    output.hallValue = data[22];
    output.p1 = readU16(data + 23);
    output.p2 = readU16(data + 25);
    output.brake = data[27] != 0;
    output.antiTheft = data[28] != 0;
    output.park = data[29] != 0;
    output.cruise = data[30] != 0;
    output.reverse = data[31] != 0;
    output.sideStand = data[32] != 0;
    output.boost = data[33] != 0;
    output.accelerateGear = data[34] != 0;
    output.decelerateGear = data[35] != 0;
    output.valid = true;
    return true;
}

ParseResult StreamParser::push(uint8_t value, MonitorData& output) {
    if (count_ == 0) {
        if (value != kHeader) {
            ++discardedBytes_;
            return ParseResult::None;
        }
        buffer_[count_++] = value;
        return ParseResult::None;
    }

    if (count_ >= kMaximumFrameSize) {
        reset();
        ++discardedBytes_;
        return ParseResult::Malformed;
    }

    buffer_[count_++] = value;
    if (count_ == 5) {
        expectedLength_ = static_cast<size_t>(buffer_[4]) + 9U;
        if (expectedLength_ < 9 || expectedLength_ > kMaximumFrameSize) {
            reset();
            return ParseResult::Malformed;
        }
    }

    if (expectedLength_ == 0 || count_ < expectedLength_) {
        return ParseResult::None;
    }

    if (count_ != expectedLength_ || buffer_[count_ - 1] != kTerminator) {
        reset();
        return ParseResult::Malformed;
    }

    if (!validateFrame(buffer_, count_)) {
        reset();
        return ParseResult::ChecksumError;
    }

    const bool decoded = decodeResponse(buffer_, count_, output);
    reset();
    return decoded ? ParseResult::Response : ParseResult::Malformed;
}

void StreamParser::reset() {
    count_ = 0;
    expectedLength_ = 0;
}

}  // namespace monitor_protocol
