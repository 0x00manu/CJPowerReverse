#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MonitorData.h"

namespace monitor_protocol {

// The firmware intentionally exposes only the captured read-only live-monitor
// exchange and identification handshake. It has no write/commit/reset API.
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

// Calculates the 7E two's-complement additive checksum. dataEndExclusive is the
// offset of the first checksum byte; the leading 7E is excluded internally.
uint16_t computeChecksum(const uint8_t* frame, size_t dataEndExclusive);

// Builds the exact 84-byte, 25-descriptor request captured from the application.
void buildBatchReadRequest(uint8_t (&output)[kRequestSize]);

// Returns one of the six immutable captured verification probes. The returned
// pointer references static storage and must not be freed or modified.
const uint8_t* verificationProbe(size_t index, size_t& length);

// Builds and validates the 5A A5 read-only controller identity exchange.
void buildIdentificationRequest(uint8_t (&output)[kIdentificationRequestSize]);
bool decodeIdentification(const uint8_t* frame, size_t length, char* output, size_t outputSize);

// Validates and decodes the fixed 45-byte 7E live-monitor response.
bool validateFrame(const uint8_t* frame, size_t length);
bool decodeResponse(const uint8_t* frame, size_t length, MonitorData& output);

// Incremental parser for arbitrarily fragmented UART input. Noise before a 7E
// header is counted and discarded; a successful response updates MonitorData.
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
