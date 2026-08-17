#pragma once

#include <stdint.h>

struct MonitorData {
    // Keep state bytes raw: captures contain values not represented by the
    // reverse-engineered Windows enums (for example driveStateRaw == 4).
    uint8_t driveStateRaw = 0;
    uint8_t runningStateRaw = 0;
    uint8_t vehicleGearRaw = 0;
    uint8_t vehicleModeRaw = 0;

    // Integer layout and signedness follow the captured descriptor sizes.
    // Fields with unconfirmed engineering-unit scaling retain a Raw suffix.
    int16_t motorSpeedRpm = 0;
    uint32_t faultCode = 0;
    int16_t dcCurrentRaw = 0;
    uint16_t dcVoltageRaw = 0;
    int16_t phaseCurrentRaw = 0;
    int16_t motorPowerRaw = 0;
    uint16_t throttleVoltageRaw = 0;
    uint8_t mcuTemperatureRaw = 0;
    uint8_t motorTemperatureRaw = 0;
    uint8_t hallValue = 0;
    uint16_t p1 = 0;
    uint16_t p2 = 0;

    bool brake = false;
    bool antiTheft = false;
    bool park = false;
    bool cruise = false;
    bool reverse = false;
    bool sideStand = false;
    bool boost = false;
    bool accelerateGear = false;
    bool decelerateGear = false;

    uint32_t updateTimeMs = 0;
    bool valid = false;

    // Only conversions confirmed by capture/UI correlation are exposed here.
    float dcCurrentA() const { return static_cast<float>(dcCurrentRaw) / 10.0F; }
    float dcVoltageV() const { return static_cast<float>(dcVoltageRaw) / 10.0F; }
    float throttleVoltageV() const { return static_cast<float>(throttleVoltageRaw) / 744.73F; }
    int16_t mcuTemperatureC() const { return static_cast<int16_t>(mcuTemperatureRaw) - 40; }
    int16_t motorTemperatureC() const { return static_cast<int16_t>(motorTemperatureRaw) - 40; }
};

struct CommunicationStats {
    // Monotonic counters are surfaced through both JSON endpoints for diagnosis.
    uint32_t requests = 0;
    uint32_t validResponses = 0;
    uint32_t checksumErrors = 0;
    uint32_t malformedFrames = 0;
    uint32_t timeouts = 0;
    uint32_t discardedBytes = 0;
};
