#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "DashboardPage.h"
#include "MonitorData.h"
#include "MonitorProtocol.h"

namespace {

// Network and controller timing are intentionally fixed to the values recovered
// from the Windows application captures. UART0 must never be used for logging.
constexpr char kAccessPointName[] = "CJPowerMonitor";
constexpr char kAccessPointPassword[] = "CJPower123";
constexpr uint32_t kBatchBaud = 57600;
constexpr uint32_t kIdentificationBaud = 115200;
constexpr uint32_t kPollIntervalMs = 350;
constexpr uint32_t kResponseTimeoutMs = 220;
constexpr uint32_t kStaleAfterMs = 1000;
constexpr uint32_t kHandshakeTimeoutMs = 5000;
constexpr uint32_t kHandshakeRetryIntervalMs = 5000;
constexpr uint8_t kMaximumAttempts = 2;

ESP8266WebServer server(80);
monitor_protocol::RegisterStreamParser parser;
MonitorData monitorData;
CommunicationStats stats;
uint8_t request[monitor_protocol::kRegisterRequestSize]{};
bool waitingForResponse = false;
uint8_t currentAttempt = 0;
uint32_t requestTimeMs = 0;
uint32_t nextPollTimeMs = 0;
char controllerIdentity[17] = "unconfirmed";
bool handshakeComplete = false;
uint32_t nextHandshakeAttemptMs = 0;
uint32_t handshakeAttempts = 0;
uint32_t activeControllerBaud = kBatchBaud;
size_t registerIndex = 0;
MonitorData pendingMonitorData;
uint32_t cycleStartTimeMs = 0;

void setControllerBaud(uint32_t baud) {
    if (activeControllerBaud == baud) {
        return;
    }
    Serial.flush();
    Serial.updateBaudRate(baud);
    activeControllerBaud = baud;
    delay(20);
    while (Serial.available() > 0) { Serial.read(); }
}

// Replays the read-only protocol-verification traffic from HandShake.pcapng and
// accepts the controller only after its 5A A5 identity frame passes CRC checking.
// This function blocks while probing; loop() never calls serviceController()
// until it returns true.
bool performProtocolVerification() {
    ++handshakeAttempts;
    // The Saleae trace and CH343 setup sequence correlate the 7E protocol with
    // 57600 baud. The later 5A A5 identity exchange runs at 115200 baud.
    setControllerBaud(kBatchBaud);
    // Preserve the approximate inter-frame gaps observed in the capture. The
    // four middle probes contain identical bytes but are distinct transmissions.
    constexpr uint16_t delaysMs[] = {500, 70, 70, 70, 70, 500};
    for (size_t index = 0; index < monitor_protocol::kVerificationProbeCount; ++index) {
        size_t length = 0;
        const uint8_t* probe = monitor_protocol::verificationProbe(index, length);
        if (probe == nullptr || length == 0) {
            return false;
        }
        Serial.write(probe, length);
        Serial.flush();
        delay(delaysMs[index]);
    }

    // Probe traffic has no expected reply. Remove any noise before the framed
    // identity request so it cannot be mistaken for an identification response.
    while (Serial.available() > 0) { Serial.read(); }
    setControllerBaud(kIdentificationBaud);
    uint8_t identificationRequest[monitor_protocol::kIdentificationRequestSize]{};
    monitor_protocol::buildIdentificationRequest(identificationRequest);
    Serial.write(identificationRequest, sizeof(identificationRequest));
    Serial.flush();

    uint8_t response[monitor_protocol::kIdentificationResponseSize]{};
    size_t count = 0;
    // Signed subtraction makes this comparison safe across millis() rollover.
    const uint32_t deadline = millis() + kHandshakeTimeoutMs;
    while (static_cast<int32_t>(millis() - deadline) < 0 && count < sizeof(response)) {
        if (Serial.available() <= 0) { yield(); continue; }
        const uint8_t value = static_cast<uint8_t>(Serial.read());
        if (count == 0 && value != 0x5A) { continue; }
        if (count == 1 && value != 0xA5) { count = 0; continue; }
        response[count++] = value;
    }
    const bool valid = monitor_protocol::decodeIdentification(
        response, count, controllerIdentity, sizeof(controllerIdentity));
    // The successful application remains at 115200 for dashboard register reads.
    return valid;
}

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
    // /api/live retains raw values for fields whose enums or scaling have not
    // been confirmed. The browser decides whether a snapshot is stale via ageMs.
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
    // /api/status remains useful before telemetry starts: it tells the browser
    // whether polling is gated and which controller identity was validated.
    String json;
    json.reserve(400);
    json += F("{\"firmware\":\"CJPower ESP8266 Data Monitor\",\"readOnly\":true,");
    json += F("\"handshakeComplete\":"); json += handshakeComplete ? F("true") : F("false");
    json += F(",\"handshakeAttempts\":"); json += handshakeAttempts;
    json += F(",\"handshakeTimeoutMs\":"); json += kHandshakeTimeoutMs;
    json += F(",\"handshakeRetryIntervalMs\":"); json += kHandshakeRetryIntervalMs;
    json += F(",\"controllerIdentity\":\""); json += controllerIdentity; json += F("\",");
    json += F("\"probeBaudRate\":57600,\"registerBaudRate\":115200,");
    json += F("\"activeControllerBaudRate\":"); json += activeControllerBaud;
    json += F(",\"pollIntervalMs\":350,\"responseTimeoutMs\":220,");
    json += F("\"apIp\":\""); json += WiFi.softAPIP().toString();
    json += F("\",\"clients\":"); json += WiFi.softAPgetStationNum();
    json += ','; appendStats(json);
    json += '}';
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send(200, F("application/json"), json);
}

void sendRequest(uint32_t now) {
    if (registerIndex == 0 && currentAttempt == 0) {
        cycleStartTimeMs = now;
    }
    const monitor_protocol::RegisterDescriptor& descriptor =
        monitor_protocol::dashboardRegister(registerIndex);
    monitor_protocol::buildRegisterReadRequest(descriptor.address, descriptor.size, request);
    Serial.write(request, sizeof(request));
    Serial.flush();
    ++stats.requests;
    ++currentAttempt;
    requestTimeMs = now;
    waitingForResponse = true;
}

void serviceController(uint32_t now) {
    // Drain all currently available UART bytes so fragmented USB/UART delivery
    // does not affect frame parsing.
    while (Serial.available() > 0) {
        const int incoming = Serial.read();
        if (incoming < 0) {
            break;
        }

        const monitor_protocol::RegisterDescriptor& descriptor =
            monitor_protocol::dashboardRegister(registerIndex);
        const monitor_protocol::ParseResult result =
            parser.push(static_cast<uint8_t>(incoming), descriptor, pendingMonitorData);
        if (result == monitor_protocol::ParseResult::Response) {
            ++stats.validResponses;
            waitingForResponse = false;
            currentAttempt = 0;
            ++registerIndex;
            if (registerIndex == monitor_protocol::kDashboardRegisterCount) {
                pendingMonitorData.valid = true;
                pendingMonitorData.updateTimeMs = now;
                monitorData = pendingMonitorData;
                registerIndex = 0;
                nextPollTimeMs = cycleStartTimeMs + kPollIntervalMs;
            } else {
                nextPollTimeMs = now;
            }
        } else if (result == monitor_protocol::ParseResult::ChecksumError) {
            ++stats.checksumErrors;
        } else if (result == monitor_protocol::ParseResult::Malformed) {
            ++stats.malformedFrames;
        }
    }

    // Retry once after a response timeout. The next regular poll is scheduled
    // only after the retry budget has been exhausted or a response is decoded.
    if (waitingForResponse && static_cast<uint32_t>(now - requestTimeMs) >= kResponseTimeoutMs) {
        ++stats.timeouts;
        waitingForResponse = false;
        parser.reset();
        if (currentAttempt < kMaximumAttempts) {
            sendRequest(now);
        } else {
            currentAttempt = 0;
            registerIndex = 0;
            pendingMonitorData = MonitorData{};
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

void serviceHandshake(uint32_t now) {
    // This is the only transition that can set handshakeComplete. Consequently,
    // no dashboard register request can be transmitted before identity validation.
    if (handshakeComplete || static_cast<int32_t>(now - nextHandshakeAttemptMs) < 0) {
        return;
    }

    waitingForResponse = false;
    currentAttempt = 0;
    registerIndex = 0;
    pendingMonitorData = MonitorData{};
    parser.reset();
    while (Serial.available() > 0) { Serial.read(); }

    Serial1.print(F("Handshake attempt "));
    Serial1.println(handshakeAttempts + 1);
    handshakeComplete = performProtocolVerification();
    if (handshakeComplete) {
        Serial1.print(F("Handshake verified; controller: "));
        Serial1.println(controllerIdentity);
        parser.reset();
        while (Serial.available() > 0) { Serial.read(); }
        nextPollTimeMs = millis();
        return;
    }

    // Schedule from the end of the failed attempt, giving a full five-second
    // quiet interval before replaying the complete verification sequence.
    nextHandshakeAttemptMs = millis() + kHandshakeRetryIntervalMs;
    Serial1.println(F("Handshake failed; live polling remains disabled"));
    Serial1.println(F("Retrying handshake in 5 seconds"));
}

}  // namespace

void setup() {
    // UART0 (GPIO1 TX, GPIO3 RX) is dedicated to the motor controller.
    Serial.begin(kBatchBaud, SERIAL_8N1);
    //Serial.swap(); // Moves UART0 TX to GPIO15 and RX to GPIO13
    // UART1 is TX-only on GPIO2 and is reserved for diagnostic messages.
    Serial1.begin(115200);
    delay(150);
    while (Serial.available() > 0) {
        Serial.read();
    }

    // Register requests are built immediately before transmission. The protocol
    // layer intentionally exposes no register-write operation.
    // serviceHandshake() performs the first attempt after the web server starts,
    // then retries at a bounded interval without ever enabling polling early.

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
    Serial1.println(F("Handshake: pending; live polling disabled"));
    nextHandshakeAttemptMs = millis();
}

void loop() {
    const uint32_t now = millis();
    // Handshake and telemetry are mutually exclusive states. Web serving remains
    // available between handshake attempts so the page can show pending status.
    if (handshakeComplete) {
        serviceController(now);
    } else {
        serviceHandshake(now);
    }
    server.handleClient();
    yield();
}
