#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MonitorData.h"

namespace monitor_protocol {

constexpr uint8_t kHeader = 0x7E;
constexpr uint8_t kTerminator = 0x0D;
constexpr size_t kRequestSize = 84;
constexpr size_t kResponseSize = 45;
constexpr size_t kResponseDataSize = 36;
constexpr size_t kMaximumFrameSize = 96;
constexpr size_t kIdentificationRequestSize = 10;
constexpr size_t kIdentificationResponseSize = 23;
constexpr size_t kVerificationProbeCount = 6;

enum class ParseResult : uint8_t {
    None,
    Response,
    ChecksumError,
    Malformed
};

uint16_t computeChecksum(const uint8_t* frame, size_t dataEndExclusive);
void buildBatchReadRequest(uint8_t (&output)[kRequestSize]);
const uint8_t* verificationProbe(size_t index, size_t& length);
void buildIdentificationRequest(uint8_t (&output)[kIdentificationRequestSize]);
bool decodeIdentification(const uint8_t* frame, size_t length, char* output, size_t outputSize);
bool validateFrame(const uint8_t* frame, size_t length);
bool decodeResponse(const uint8_t* frame, size_t length, MonitorData& output);

class StreamParser {
public:
    ParseResult push(uint8_t value, MonitorData& output);
    void reset();
    uint32_t discardedBytes() const { return discardedBytes_; }

private:
    uint8_t buffer_[kMaximumFrameSize]{};
    size_t count_ = 0;
    size_t expectedLength_ = 0;
    uint32_t discardedBytes_ = 0;
};

}  // namespace monitor_protocol
