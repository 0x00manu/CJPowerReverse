#include "MonitorProtocol.h"

#include <string.h>

namespace monitor_protocol {
namespace {

// Exact request bytes recovered from screen_shot_live.pcapng. Each descriptor is
// address-low, address-high, size; the 25 sizes total 36 response-data bytes.
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

// Exact protocol-verification frames recovered from HandShake.pcapng. Their
// detailed semantics remain unknown, so do not synthesize or alter their fields.
constexpr uint8_t kProbe0[] = {0x7E, 0x00, 0x02, 0x08, 0x04, 0xC0, 0x90, 0x00, 0x20, 0x00, 0x82, 0xFE, 0x0D};
constexpr uint8_t kProbe1[] = {0x7E, 0x00, 0x03, 0x01, 0x02, 0xE0, 0xC0, 0xC0, 0x9A, 0xFD, 0x0D};
constexpr uint8_t kProbe5[] = {0x7E, 0x00, 0x03, 0x02, 0x04, 0xC0, 0x30, 0x00, 0x20, 0x00, 0xE7, 0xFE, 0x0D};
constexpr uint8_t kIdentificationRequest[] = {0x5A, 0xA5, 0x03, 0x05, 0x80, 0x10, 0xC5, 0x2E, 0x0D, 0x0A};
constexpr RegisterDescriptor kDashboardRegisters[] = {
    {57, 1}, {245, 1}, {192, 1}, {203, 1}, {105, 2}, {239, 4},
    {119, 2}, {113, 2}, {224, 2}, {115, 2}, {220, 2}, {222, 1},
    {223, 1}, {90, 1}, {1130, 2}, {1132, 2}, {232, 1}, {233, 1},
    {234, 1}, {235, 1}, {236, 1}, {237, 1}, {238, 1}, {243, 1}, {244, 1}
};

static_assert(sizeof(kRequestPrefix) + sizeof(kDescriptors) + 3 == kRequestSize,
              "Captured request size changed");
static_assert(sizeof(kDashboardRegisters) / sizeof(kDashboardRegisters[0]) ==
                  kDashboardRegisterCount,
              "Dashboard register count changed");

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

uint16_t computeCrc(const uint8_t* data, size_t length) {
    // CRC-16/CCITT-FALSE used only by 5A A5 frames: poly 0x1021, init 0xFFFF,
    // no reflection, no final XOR. CRC bytes are transmitted little-endian.
    uint16_t crc = 0xFFFF;
    for (size_t index = 0; index < length; ++index) {
        crc ^= static_cast<uint16_t>(data[index]) << 8U;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
        }
    }
    return crc;
}

}  // namespace

uint16_t computeChecksum(const uint8_t* frame, size_t dataEndExclusive) {
    // 7E frames use a two's-complement byte sum, not CRC. Skip the 7E header;
    // dataEndExclusive points at the first checksum byte.
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

const uint8_t* verificationProbe(size_t index, size_t& length) {
    if (index == 0) { length = sizeof(kProbe0); return kProbe0; }
    if (index >= 1 && index <= 4) { length = sizeof(kProbe1); return kProbe1; }
    if (index == 5) { length = sizeof(kProbe5); return kProbe5; }
    length = 0;
    return nullptr;
}

void buildIdentificationRequest(uint8_t (&output)[kIdentificationRequestSize]) {
    memcpy(output, kIdentificationRequest, sizeof(kIdentificationRequest));
}

bool decodeIdentification(const uint8_t* frame, size_t length, char* output, size_t outputSize) {
    // Captured response: LEN=16 consists of address 0x8005 plus a 14-byte ASCII
    // identity. The complete frame is therefore 23 bytes including framing.
    constexpr size_t kIdentitySize = 14;
    if (frame == nullptr || output == nullptr || outputSize < kIdentitySize + 1 ||
        length != kIdentificationResponseSize || frame[0] != 0x5A || frame[1] != 0xA5 ||
        frame[2] != 0x10 || frame[3] != 0x05 || frame[4] != 0x80 ||
        frame[21] != 0x0D || frame[22] != 0x0A) {
        return false;
    }
    // LEN counts the encoded address and identity body. CRC bytes begin at
    // frame[3 + LEN] and must not be included in the CRC calculation.
    const uint16_t expected = computeCrc(frame + 3, frame[2]);
    if (readU16(frame + 19) != expected) {
        return false;
    }
    memcpy(output, frame + 5, kIdentitySize);
    output[kIdentitySize] = '\0';
    return true;
}

const RegisterDescriptor& dashboardRegister(size_t index) {
    return kDashboardRegisters[index];
}

void buildRegisterReadRequest(uint16_t address, uint8_t size,
                              uint8_t (&output)[kRegisterRequestSize]) {
    const uint16_t encodedAddress = static_cast<uint16_t>(address | 0x8000U);
    output[0] = 0x5A; output[1] = 0xA5; output[2] = 0x03;
    output[3] = static_cast<uint8_t>(encodedAddress & 0xFFU);
    output[4] = static_cast<uint8_t>(encodedAddress >> 8U);
    output[5] = size;
    const uint16_t crc = computeCrc(output + 3, 3);
    output[6] = static_cast<uint8_t>(crc & 0xFFU);
    output[7] = static_cast<uint8_t>(crc >> 8U);
    output[8] = 0x0D; output[9] = 0x0A;
}

bool decodeRegisterResponse(const uint8_t* frame, size_t length,
                            const RegisterDescriptor& expected, MonitorData& output) {
    const size_t expectedLength = static_cast<size_t>(expected.size) + 9U;
    const uint16_t encodedAddress = static_cast<uint16_t>(expected.address | 0x8000U);
    if (frame == nullptr || expected.size == 0 || expected.size > 4 ||
        length != expectedLength || frame[0] != 0x5A || frame[1] != 0xA5 ||
        frame[2] != expected.size + 2U || readU16(frame + 3) != encodedAddress ||
        frame[length - 2] != 0x0D || frame[length - 1] != 0x0A ||
        readU16(frame + 5 + expected.size) != computeCrc(frame + 3, expected.size + 2U)) {
        return false;
    }
    const uint8_t* value = frame + 5;
    switch (expected.address) {
        case 57: output.driveStateRaw = value[0]; break;
        case 245: output.runningStateRaw = value[0]; break;
        case 192: output.vehicleGearRaw = value[0]; break;
        case 203: output.vehicleModeRaw = value[0]; break;
        case 105: output.motorSpeedRpm = readI16(value); break;
        case 239: output.faultCode = readU32(value); break;
        case 119: output.dcCurrentRaw = readI16(value); break;
        case 113: output.dcVoltageRaw = readU16(value); break;
        case 224: output.phaseCurrentRaw = readI16(value); break;
        case 115: output.motorPowerRaw = readI16(value); break;
        case 220: output.throttleVoltageRaw = readU16(value); break;
        case 222: output.mcuTemperatureRaw = value[0]; break;
        case 223: output.motorTemperatureRaw = value[0]; break;
        case 90: output.hallValue = value[0]; break;
        case 1130: output.p1 = readU16(value); break;
        case 1132: output.p2 = readU16(value); break;
        case 232: output.brake = value[0] != 0; break;
        case 233: output.antiTheft = value[0] != 0; break;
        case 234: output.park = value[0] != 0; break;
        case 235: output.cruise = value[0] != 0; break;
        case 236: output.reverse = value[0] != 0; break;
        case 237: output.sideStand = value[0] != 0; break;
        case 238: output.boost = value[0] != 0; break;
        case 243: output.accelerateGear = value[0] != 0; break;
        case 244: output.decelerateGear = value[0] != 0; break;
        default: return false;
    }
    return true;
}

bool validateFrame(const uint8_t* frame, size_t length) {
    if (frame == nullptr || length < 9 || length > kMaximumFrameSize ||
        frame[0] != kHeader || frame[length - 1] != kTerminator) {
        return false;
    }

    // A 7E response has six prefix bytes, payload length frame[4], two checksum
    // bytes and one terminator: payload length + 9 bytes total.
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

    // Values are concatenated in the same order as kDescriptors. Multibyte
    // integers are little-endian; signed values use two's complement.
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
    // Resynchronize by discarding bytes until a 7E header appears. Once byte 4
    // is known, the declared data size determines the complete frame length.
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

ParseResult RegisterStreamParser::push(uint8_t value, const RegisterDescriptor& expected,
                                       MonitorData& output) {
    if (count_ == 0) {
        if (value != 0x5A) { ++discardedBytes_; return ParseResult::None; }
        buffer_[count_++] = value;
        return ParseResult::None;
    }
    if (count_ == 1 && value != 0xA5) {
        reset(); ++discardedBytes_; return ParseResult::Malformed;
    }
    if (count_ >= sizeof(buffer_)) { reset(); return ParseResult::Malformed; }
    buffer_[count_++] = value;
    if (count_ == 3) {
        expectedLength_ = static_cast<size_t>(buffer_[2]) + 7U;
        if (expectedLength_ < 9 || expectedLength_ > sizeof(buffer_)) {
            reset(); return ParseResult::Malformed;
        }
    }
    if (expectedLength_ == 0 || count_ < expectedLength_) return ParseResult::None;
    const bool decoded = decodeRegisterResponse(buffer_, count_, expected, output);
    reset();
    return decoded ? ParseResult::Response : ParseResult::ChecksumError;
}

void RegisterStreamParser::reset() {
    count_ = 0;
    expectedLength_ = 0;
}

}  // namespace monitor_protocol
