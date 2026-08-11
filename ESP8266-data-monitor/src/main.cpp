#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "DashboardPage.h"
#include "MonitorData.h"
#include "MonitorProtocol.h"

namespace {

constexpr char kAccessPointName[] = "CJPowerMonitor";
constexpr char kAccessPointPassword[] = "CJPower123";
constexpr uint32_t kControllerBaud = 115200;
constexpr uint32_t kPollIntervalMs = 150;
constexpr uint32_t kResponseTimeoutMs = 220;
constexpr uint32_t kStaleAfterMs = 1000;
constexpr uint8_t kMaximumAttempts = 2;

ESP8266WebServer server(80);
monitor_protocol::StreamParser parser;
MonitorData monitorData;
CommunicationStats stats;
uint8_t request[monitor_protocol::kRequestSize]{};
bool waitingForResponse = false;
uint8_t currentAttempt = 0;
uint32_t requestTimeMs = 0;
uint32_t nextPollTimeMs = 0;

void writeJsonBoolean(String& json, const __FlashStringHelper* name, bool value) {
    json += '"';
    json += name;
    json += F("\":");
    json += value ? F("true") : F("false");
}

void appendStats(String& json) {
    json += F("\"stats\":{");
    json += F("\"requests\":"); json += stats.requests;
    json += F(",\"validResponses\":"); json += stats.validResponses;
    json += F(",\"checksumErrors\":"); json += stats.checksumErrors;
    json += F(",\"malformedFrames\":"); json += stats.malformedFrames;
    json += F(",\"timeouts\":"); json += stats.timeouts;
    json += F(",\"discardedBytes\":"); json += parser.discardedBytes();
    json += '}';
}

void handleLiveData() {
    String json;
    json.reserve(1150);
    json += '{';
    writeJsonBoolean(json, F("valid"), monitorData.valid);
    json += F(",\"ageMs\":");
    json += monitorData.valid ? millis() - monitorData.updateTimeMs : 0xFFFFFFFFUL;
    json += F(",\"driveStateRaw\":"); json += monitorData.driveStateRaw;
    json += F(",\"runningStateRaw\":"); json += monitorData.runningStateRaw;
    json += F(",\"vehicleGearRaw\":"); json += monitorData.vehicleGearRaw;
    json += F(",\"vehicleModeRaw\":"); json += monitorData.vehicleModeRaw;
    json += F(",\"motorSpeedRpm\":"); json += monitorData.motorSpeedRpm;
    json += F(",\"faultCode\":"); json += monitorData.faultCode;
    json += F(",\"dcCurrentA\":"); json += String(monitorData.dcCurrentA(), 1);
    json += F(",\"dcVoltageV\":"); json += String(monitorData.dcVoltageV(), 1);
    json += F(",\"phaseCurrentRaw\":"); json += monitorData.phaseCurrentRaw;
    json += F(",\"motorPowerRaw\":"); json += monitorData.motorPowerRaw;
    json += F(",\"throttleVoltageV\":"); json += String(monitorData.throttleVoltageV(), 3);
    json += F(",\"mcuTemperatureC\":"); json += monitorData.mcuTemperatureC();
    json += F(",\"motorTemperatureC\":"); json += monitorData.motorTemperatureC();
    json += F(",\"hallValue\":"); json += monitorData.hallValue;
    json += F(",\"p1\":"); json += monitorData.p1;
    json += F(",\"p2\":"); json += monitorData.p2;
    json += ','; writeJsonBoolean(json, F("brake"), monitorData.brake);
    json += ','; writeJsonBoolean(json, F("antiTheft"), monitorData.antiTheft);
    json += ','; writeJsonBoolean(json, F("park"), monitorData.park);
    json += ','; writeJsonBoolean(json, F("cruise"), monitorData.cruise);
    json += ','; writeJsonBoolean(json, F("reverse"), monitorData.reverse);
    json += ','; writeJsonBoolean(json, F("sideStand"), monitorData.sideStand);
    json += ','; writeJsonBoolean(json, F("boost"), monitorData.boost);
    json += ','; writeJsonBoolean(json, F("accelerateGear"), monitorData.accelerateGear);
    json += ','; writeJsonBoolean(json, F("decelerateGear"), monitorData.decelerateGear);
    json += ','; appendStats(json);
    json += '}';
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send(200, F("application/json"), json);
}

void handleStatus() {
    String json;
    json.reserve(400);
    json += F("{\"firmware\":\"CJPower ESP8266 Data Monitor\",\"readOnly\":true,");
    json += F("\"baudRate\":115200,\"pollIntervalMs\":150,\"responseTimeoutMs\":220,");
    json += F("\"apIp\":\""); json += WiFi.softAPIP().toString();
    json += F("\",\"clients\":"); json += WiFi.softAPgetStationNum();
    json += ','; appendStats(json);
    json += '}';
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send(200, F("application/json"), json);
}

void sendRequest(uint32_t now) {
    Serial.write(request, sizeof(request));
    Serial.flush();
    ++stats.requests;
    ++currentAttempt;
    requestTimeMs = now;
    waitingForResponse = true;
}

void serviceController(uint32_t now) {
    while (Serial.available() > 0) {
        const int incoming = Serial.read();
        if (incoming < 0) {
            break;
        }

        const monitor_protocol::ParseResult result = parser.push(static_cast<uint8_t>(incoming), monitorData);
        if (result == monitor_protocol::ParseResult::Response) {
            monitorData.updateTimeMs = now;
            ++stats.validResponses;
            waitingForResponse = false;
            currentAttempt = 0;
            nextPollTimeMs = now + kPollIntervalMs;
        } else if (result == monitor_protocol::ParseResult::ChecksumError) {
            ++stats.checksumErrors;
        } else if (result == monitor_protocol::ParseResult::Malformed) {
            ++stats.malformedFrames;
        }
    }

    if (waitingForResponse && static_cast<uint32_t>(now - requestTimeMs) >= kResponseTimeoutMs) {
        ++stats.timeouts;
        waitingForResponse = false;
        parser.reset();
        if (currentAttempt < kMaximumAttempts) {
            sendRequest(now);
        } else {
            currentAttempt = 0;
            nextPollTimeMs = now + kPollIntervalMs;
        }
    }

    if (!waitingForResponse && static_cast<int32_t>(now - nextPollTimeMs) >= 0) {
        sendRequest(now);
    }

    if (monitorData.valid && static_cast<uint32_t>(now - monitorData.updateTimeMs) > kStaleAfterMs) {
        // Preserve the last snapshot for diagnosis; ageMs marks it stale in the UI.
    }
}

}  // namespace

void setup() {
    // UART0 (GPIO1 TX, GPIO3 RX) is dedicated to the motor controller.
    Serial.begin(kControllerBaud, SERIAL_8N1);
    // UART1 is TX-only on GPIO2 and is reserved for diagnostic messages.
    Serial1.begin(115200);
    delay(150);
    while (Serial.available() > 0) {
        Serial.read();
    }

    monitor_protocol::buildBatchReadRequest(request);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(kAccessPointName, kAccessPointPassword);

    server.on(F("/"), HTTP_GET, []() {
        server.send_P(200, PSTR("text/html; charset=utf-8"), kDashboardPage);
    });
    server.on(F("/api/live"), HTTP_GET, handleLiveData);
    server.on(F("/api/status"), HTTP_GET, handleStatus);
    server.onNotFound([]() { server.send(404, F("text/plain"), F("Not found")); });
    server.begin();

    Serial1.println();
    Serial1.println(F("CJPower read-only data monitor"));
    Serial1.print(F("AP: ")); Serial1.println(kAccessPointName);
    Serial1.print(F("IP: ")); Serial1.println(WiFi.softAPIP());
    nextPollTimeMs = millis();
}

void loop() {
    const uint32_t now = millis();
    serviceController(now);
    server.handleClient();
    yield();
}
