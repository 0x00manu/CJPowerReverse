#include <unity.h>

#include "MonitorProtocol.h"

namespace {

constexpr uint8_t kCapturedResponse[monitor_protocol::kResponseSize] = {
    0x7E, 0x00, 0x02, 0x82, 0x24, 0xA0,
    0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFE, 0xFF, 0xE1, 0x01, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x02,
    0x4B, 0x4E, 0x02, 0x02, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF9, 0xFA, 0x0D
};

void testRequestMatchesCapture() {
    uint8_t request[monitor_protocol::kRequestSize]{};
    monitor_protocol::buildBatchReadRequest(request);
    TEST_ASSERT_EQUAL_HEX8(0x7E, request[0]);
    TEST_ASSERT_EQUAL_HEX8(0x4B, request[4]);
    TEST_ASSERT_EQUAL_HEX8(0x10, request[5]);
    TEST_ASSERT_EQUAL_HEX8(0x0C, request[81]);
    TEST_ASSERT_EQUAL_HEX8(0xED, request[82]);
    TEST_ASSERT_EQUAL_HEX8(0x0D, request[83]);
}

void testCapturedResponseDecodes() {
    MonitorData data;
    TEST_ASSERT_TRUE(monitor_protocol::decodeResponse(kCapturedResponse, sizeof(kCapturedResponse), data));
    TEST_ASSERT_TRUE(data.valid);
    TEST_ASSERT_EQUAL_INT16(-2, data.dcCurrentRaw);
    TEST_ASSERT_EQUAL_UINT16(481, data.dcVoltageRaw);
    TEST_ASSERT_EQUAL_UINT16(570, data.throttleVoltageRaw);
    TEST_ASSERT_EQUAL_INT16(35, data.mcuTemperatureC());
    TEST_ASSERT_EQUAL_INT16(38, data.motorTemperatureC());
    TEST_ASSERT_EQUAL_UINT16(1282, data.p1);
}

void testChecksumDamageIsRejected() {
    uint8_t damaged[sizeof(kCapturedResponse)]{};
    memcpy(damaged, kCapturedResponse, sizeof(damaged));
    damaged[18] ^= 0x01;
    TEST_ASSERT_FALSE(monitor_protocol::validateFrame(damaged, sizeof(damaged)));
}

void testFragmentedStreamAndNoise() {
    monitor_protocol::StreamParser parser;
    MonitorData data;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(monitor_protocol::ParseResult::None),
                          static_cast<int>(parser.push(0x55, data)));
    monitor_protocol::ParseResult result = monitor_protocol::ParseResult::None;
    for (uint8_t value : kCapturedResponse) {
        result = parser.push(value, data);
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(monitor_protocol::ParseResult::Response),
                          static_cast<int>(result));
    TEST_ASSERT_EQUAL_UINT32(1, parser.discardedBytes());
    TEST_ASSERT_EQUAL_UINT16(481, data.dcVoltageRaw);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testRequestMatchesCapture);
    RUN_TEST(testCapturedResponseDecodes);
    RUN_TEST(testChecksumDamageIsRejected);
    RUN_TEST(testFragmentedStreamAndNoise);
    return UNITY_END();
}
