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

void testIdentificationResponseDecodes() {
    uint8_t response[] = {
        0x5A, 0xA5, 0x10, 0x05, 0x80, 0x42, 0x31, 0x2E, 0x54, 0x32, 0x2E,
        0x30, 0x30, 0x39, 0x2D, 0x30, 0x33, 0x31, 0x41, 0x83, 0xA3, 0x0D, 0x0A
    };
    char identity[17]{};
    TEST_ASSERT_TRUE(monitor_protocol::decodeIdentification(response, sizeof(response), identity, sizeof(identity)));
    TEST_ASSERT_EQUAL_STRING("B1.T2.009-031A", identity);
    response[19] ^= 0x01;
    TEST_ASSERT_FALSE(monitor_protocol::decodeIdentification(response, sizeof(response), identity, sizeof(identity)));
}

void testRegisterRequestMatchesWorkingCapture() {
    uint8_t request[monitor_protocol::kRegisterRequestSize]{};
    monitor_protocol::buildRegisterReadRequest(113, 2, request);
    const uint8_t expected[] = {
        0x5A, 0xA5, 0x03, 0x71, 0x80, 0x02, 0x7E, 0x18, 0x0D, 0x0A
    };
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, request, sizeof(expected));
}

void testRegisterResponseUpdatesDashboardField() {
    const uint8_t response[] = {
        0x5A, 0xA5, 0x04, 0x71, 0x80, 0xE1, 0x01, 0xF9, 0xB8, 0x0D, 0x0A
    };
    MonitorData data;
    const monitor_protocol::RegisterDescriptor descriptor{113, 2};
    TEST_ASSERT_TRUE(monitor_protocol::decodeRegisterResponse(
        response, sizeof(response), descriptor, data));
    TEST_ASSERT_EQUAL_UINT16(481, data.dcVoltageRaw);
}

void testRegisterStreamRejectsCrcDamage() {
    uint8_t response[] = {
        0x5A, 0xA5, 0x03, 0xDE, 0x80, 0x4C, 0xD9, 0x20, 0x0D, 0x0A
    };
    response[6] ^= 1;
    monitor_protocol::RegisterStreamParser parser;
    MonitorData data;
    const monitor_protocol::RegisterDescriptor descriptor{222, 1};
    monitor_protocol::ParseResult result = monitor_protocol::ParseResult::None;
    for (uint8_t value : response) {
        result = parser.push(value, descriptor, data);
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(monitor_protocol::ParseResult::ChecksumError),
                          static_cast<int>(result));
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(testRequestMatchesCapture);
    RUN_TEST(testCapturedResponseDecodes);
    RUN_TEST(testChecksumDamageIsRejected);
    RUN_TEST(testFragmentedStreamAndNoise);
    RUN_TEST(testIdentificationResponseDecodes);
    RUN_TEST(testRegisterRequestMatchesWorkingCapture);
    RUN_TEST(testRegisterResponseUpdatesDashboardField);
    RUN_TEST(testRegisterStreamRejectsCrcDamage);
    return UNITY_END();
}
