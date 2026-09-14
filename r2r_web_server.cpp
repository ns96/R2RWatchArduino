/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#include "r2r_web_server.h"
#include "web_ui_html.h"
#include "stream_ui_html.h"
#include "player_ui_html.h"

// Include user-defined Wi-Fi credentials
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#include "wifi_config.example.h"
#endif

#include <M5Unified.h>
#include <WiFiUdp.h>
#include <cstdio>
#include <cstring>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

#ifndef R2R_NTP_SERVER_1
#define R2R_NTP_SERVER_1 "pool.ntp.org"
#endif

#ifndef R2R_NTP_SERVER_2
#define R2R_NTP_SERVER_2 "time.nist.gov"
#endif

#ifndef R2R_TIMEZONE
#define R2R_TIMEZONE "EST5EDT,M3.2.0,M11.1.0"
#endif

namespace r2r {

R2RWebServer::R2RWebServer()
{
}

R2RWebServer::~R2RWebServer()
{
}

void R2RWebServer::init(R2RRenderer* renderer)
{
    _renderer = renderer;

    Serial.println("\n[R2RWebServer] Initializing Wi-Fi & WebServer subsystem...");
    Serial.printf("[R2RWebServer] Free Heap: %u bytes | Free PSRAM: %u bytes\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());

    // 1. Configure Wi-Fi in Station Mode first
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    delay(50);

    bool connected = false;
    if (std::strlen(R2R_WIFI_SSID) > 0 && std::strcmp(R2R_WIFI_SSID, "YOUR_WIFI_SSID") != 0) {
        Serial.printf("[WiFi] Attempting connection to SSID: \"%s\"...\n", R2R_WIFI_SSID);
        WiFi.begin(R2R_WIFI_SSID, R2R_WIFI_PASSWORD);

        uint32_t startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < R2R_WIFI_TIMEOUT_MS) {
            delay(300);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            _is_connected = true;
            _is_ap_mode = false;
            _ip_str = WiFi.localIP().toString();
            _rssi = WiFi.RSSI();
            Serial.println("[WiFi] ===============================================");
            Serial.println("[WiFi] *** SUCCESS: CONNECTED TO LOCAL WI-FI! ***");
            Serial.printf("[WiFi] Assigned IP : %s\n", _ip_str.c_str());
            Serial.printf("[WiFi] Signal RSSI : %d dBm\n", _rssi);
            Serial.println("[WiFi] ===============================================");

            // Pull network time via direct UDP NTP immediately upon Wi-Fi connection
            syncNtpTime(3000);
        }
    }

    // 2. Fallback to SoftAP Hotspot if Station mode did not connect
    if (!connected) {
        Serial.println("[WiFi] Wi-Fi connection timed out. Starting Access Point mode...");
        WiFi.disconnect(true);
        delay(100);

        WiFi.mode(WIFI_AP);
        delay(50);

        IPAddress apIP(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(apIP, apIP, subnet);
        bool apStarted = WiFi.softAP(R2R_AP_SSID, R2R_AP_PASSWORD);

        _is_connected = true;
        _is_ap_mode = true;
        _ip_str = WiFi.softAPIP().toString();
        if (_ip_str == "0.0.0.0" || _ip_str.length() == 0) {
            _ip_str = "192.168.4.1";
        }

        Serial.println("[WiFi] ===============================================");
        Serial.printf("[WiFi] *** SOFTAP HOTSPOT ACTIVE: \"%s\" (%s) ***\n",
                      R2R_AP_SSID, apStarted ? "READY" : "RETRY");
        Serial.printf("[WiFi] Connect phone to \"%s\", then open http://%s/\n",
                      R2R_AP_SSID, _ip_str.c_str());
        Serial.println("[WiFi] ===============================================");
    }

    if (_renderer) {
        _renderer->setIpAddress(_ip_str.c_str());
    }

    // 3. Register HTTP Routes
    setupRoutes();
}

bool R2RWebServer::fetchNtpTimeUDP(const char* serverHost, uint32_t timeoutMs)
{
    WiFiUDP udp;
    if (!udp.begin(2390)) {
        return false;
    }

    // 48-byte NTP request packet (RFC 5905)
    uint8_t packetBuffer[48];
    std::memset(packetBuffer, 0, sizeof(packetBuffer));
    packetBuffer[0] = 0b11100011; // LI=3 (alarm/unsync), VN=4 (version 4), Mode=3 (Client)
    packetBuffer[1] = 0;
    packetBuffer[2] = 6;
    packetBuffer[3] = 0xEC;

    Serial.printf("[NTP] Querying UDP NTP server: %s:123...\n", serverHost);
    if (!udp.beginPacket(serverHost, 123)) {
        udp.stop();
        return false;
    }
    udp.write(packetBuffer, 48);
    if (!udp.endPacket()) {
        udp.stop();
        return false;
    }

    uint32_t start = millis();
    while ((millis() - start) < timeoutMs) {
        int cb = udp.parsePacket();
        if (cb >= 48) {
            udp.read(packetBuffer, 48);
            udp.stop();

            // Extract seconds since Jan 1, 1900 (NTP Epoch, Big-Endian bytes 40..43)
            uint32_t highWord = ((uint32_t)packetBuffer[40] << 8) | packetBuffer[41];
            uint32_t lowWord  = ((uint32_t)packetBuffer[42] << 8) | packetBuffer[43];
            uint32_t secsSince1900 = (highWord << 16) | lowWord;

            const uint32_t seventyYears = 2208988800UL; // Seconds between 1900 and 1970
            if (secsSince1900 < seventyYears) {
                return false;
            }

            time_t epoch = static_cast<time_t>(secsSince1900 - seventyYears);

            // Sanity check: Epoch must be in valid contemporary range (e.g. 2024..2038)
            if (epoch < 1704067200LL || epoch > 2147483647LL) {
                return false;
            }

            // Apply POSIX Timezone (EST5EDT, etc.)
            setenv("TZ", R2R_TIMEZONE, 1);
            tzset();

            struct tm timeinfo;
            localtime_r(&epoch, &timeinfo);

            // Apply to M5Unified hardware RTC
            m5::rtc_datetime_t dt;
            dt.date.year = timeinfo.tm_year + 1900;
            dt.date.month = timeinfo.tm_mon + 1;
            dt.date.date = timeinfo.tm_mday;
            dt.date.weekDay = timeinfo.tm_wday;
            dt.time.hours = timeinfo.tm_hour;
            dt.time.minutes = timeinfo.tm_min;
            dt.time.seconds = timeinfo.tm_sec;
            M5.Rtc.setDateTime(dt);

            // Apply to ESP32 system clock
            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
            settimeofday(&tv, nullptr);

            _time_synced = true;

            Serial.println("[NTP] ===============================================");
            Serial.printf("[NTP] *** UDP NTP TIME SYNCHRONIZED: %04d-%02d-%02d %02d:%02d:%02d ***\n",
                          dt.date.year, dt.date.month, dt.date.date,
                          dt.time.hours, dt.time.minutes, dt.time.seconds);
            Serial.printf("[NTP] Unix Epoch: %lld | Timezone: %s\n", (long long)epoch, R2R_TIMEZONE);
            Serial.println("[NTP] ===============================================");
            return true;
        }
        delay(20);
    }

    udp.stop();
    return false;
}

bool R2RWebServer::syncNtpTime(uint32_t timeoutMs)
{
    if (_is_ap_mode || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    Serial.println("[NTP] Initiating Network Time Protocol (NTP) synchronization...");

    // 1. Try direct UDP query to Primary NTP server
    if (fetchNtpTimeUDP(R2R_NTP_SERVER_1, 1500)) {
        return true;
    }

    // 2. Try direct UDP query to Secondary NTP server
    if (fetchNtpTimeUDP(R2R_NTP_SERVER_2, 1500)) {
        return true;
    }

    // 3. Fallback to background SNTP service
    Serial.println("[NTP] Direct UDP response pending, starting background SNTP daemon...");
    configTzTime(R2R_TIMEZONE, R2R_NTP_SERVER_1, R2R_NTP_SERVER_2);
    return false;
}

void R2RWebServer::setupRoutes()
{
    // 1. Serve embedded single-page HTML dashboard
    _server.on("/", HTTP_GET, [this]() {
        handleRoot();
    });

    // 2. Telemetry & Status JSON API endpoints
    _server.on("/api/telemetry", HTTP_GET, [this]() {
        handleTelemetry();
    });
    _server.on("/api/status", HTTP_GET, [this]() {
        handleTelemetry();
    });
    _server.on("/status", HTTP_GET, [this]() {
        handleTelemetry();
    });

    // 3. Remote Action API endpoint
    _server.on("/api/action", HTTP_POST, [this]() {
        handleAction();
    });

    // 4. Remote Time Sync API endpoint
    _server.on("/api/set_time", HTTP_POST, [this]() {
        handleSetTime();
    });

    // 5. Simulated DCT FSK Plain Text Record Endpoint (/raw)
    _server.on("/raw", HTTP_GET, [this]() {
        handleRaw();
    });

    // 6. Live FSK Stream Viewer Page (/stream & /terminal)
        // 6. CassetteFlow Web Player Endpoint (/player and /player/)
    _server.on("/player", HTTP_GET, [this]() {
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.send_P(200, "text/html", PLAYER_HTML);
    });
        // 6b. Now Playing Sync Endpoint (/playing)
    _server.on("/playing", HTTP_GET, [this]() {
        handlePlaying();
    });
    _server.on("/playing", HTTP_POST, [this]() {
        handlePlaying();
    });

    _server.on("/player/", HTTP_GET, [this]() {
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.send_P(200, "text/html", PLAYER_HTML);
    });

    _server.on("/stream", HTTP_GET, [this]() {
        handleStream();
    });
    _server.on("/terminal", HTTP_GET, [this]() {
        handleStream();
    });

    // 7. Test & Simulation API Endpoint (/test)
    _server.on("/test", HTTP_GET, [this]() {
        handleTest();
    });
    _server.on("/test", HTTP_POST, [this]() {
        handleTest();
    });

    // 8. CassetteFlow & ESP32LyraT Compatibility Endpoints (/info, /mp3db, /tapedb, /play, /stop)
    _server.on("/info", HTTP_GET, [this]() {
        handleInfo();
    });
    _server.on("/mp3db", HTTP_GET, [this]() {
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.send(200, "text/plain", "NONE\n");
    });
    _server.on("/tapedb", HTTP_GET, [this]() {
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.send(200, "text/plain", "NONE\n");
    });
    _server.on("/stop", HTTP_GET, [this]() {
        if (_renderer) {
            _renderer->getCalculator().setTestMode(TestModeOverride::Stop, 0.0f);
        }
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.send(200, "text/plain", "OK\n");
    });
    _server.on("/play", HTTP_GET, [this]() {
        if (_renderer) {
            String side = _server.hasArg("side") ? _server.arg("side") : "A";
            side.toUpperCase();
            if (side == "B") {
                _renderer->getCalculator().setTestMode(TestModeOverride::PlayCW, 25.0f);
            } else {
                _renderer->getCalculator().setTestMode(TestModeOverride::PlayCCW, 25.0f);
            }
        }
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.send(200, "text/plain", "OK\n");
    });

    _server.begin();
    Serial.println("[R2RWebServer] HTTP Server active on Port 80");
    Serial.printf("[R2RWebServer] Dashboard URL: http://%s/\n", _ip_str.c_str());
    Serial.printf("[R2RWebServer] Cassette Player: http://%s/player\n", _ip_str.c_str());
    Serial.printf("[R2RWebServer] Live Stream  : http://%s/stream\n", _ip_str.c_str());
    Serial.printf("[R2RWebServer] Raw Output   : http://%s/raw\n", _ip_str.c_str());
    Serial.printf("[R2RWebServer] Info Handshake: http://%s/info\n", _ip_str.c_str());
    Serial.printf("[R2RWebServer] Test Mode URL : http://%s/test?ips=3.75&rpm=25&mode=ccw\n\n", _ip_str.c_str());
}

void R2RWebServer::update()
{
    _server.handleClient();
    processStreamingClients();

    // Check if background SNTP has synchronized system time (completely non-blocking)
    if (!_time_synced && WiFi.status() == WL_CONNECTED) {
        time_t now = time(nullptr);
        if (now > 1700000000) { // Valid timestamp after Jan 2024
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            m5::rtc_datetime_t dt;
            dt.date.year    = timeinfo.tm_year + 1900;
            dt.date.month   = timeinfo.tm_mon + 1;
            dt.date.date    = timeinfo.tm_mday;
            dt.time.hours   = timeinfo.tm_hour;
            dt.time.minutes = timeinfo.tm_min;
            dt.time.seconds = timeinfo.tm_sec;
            M5.Rtc.setDateTime(dt);
            _time_synced = true;
            Serial.printf("[NTP] Background SNTP synchronized time: %04d-%02d-%02d %02d:%02d:%02d\n",
                          dt.date.year, dt.date.month, dt.date.date,
                          dt.time.hours, dt.time.minutes, dt.time.seconds);
        }
    }
}

void R2RWebServer::handleRoot()
{
    _server.send_P(200, "text/html", INDEX_HTML);
}

void R2RWebServer::handlePlaying()
{
    String track = "";
    if (_server.hasArg("track")) {
        track = _server.arg("track");
    } else if (_server.hasArg("title")) {
        track = _server.arg("title");
        if (_server.hasArg("artist")) {
            track += " - " + _server.arg("artist");
        }
    } else if (_server.hasArg("plain")) {
        track = _server.arg("plain");
    }
    track.trim();
    if (track.length() > 0) {
        std::strncpy(_currentTrack, track.c_str(), sizeof(_currentTrack) - 1);
        _currentTrack[sizeof(_currentTrack) - 1] = '\0';
        Serial.printf("[R2RWebServer] Now Playing updated from client: %s\n", _currentTrack);
    }
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", "{\"status\":\"ok\"}\n");
}

void R2RWebServer::handleStream()
{
    _server.send_P(200, "text/html", STREAM_HTML);
}

void R2RWebServer::handleRaw()
{
    if (!_renderer) {
        _server.send(500, "text/plain", "### NOCARRIER ###\n");
        return;
    }

    WiFiClient client = _server.client();
    if (!client) {
        return;
    }

    // Send HTTP/1.1 200 Streaming Header (Persistent connection without content-length)
    client.print(
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Connection: close\r\n\r\n"
    );

    // Send the first line immediately
    const char* line = _renderer->getCalculator().getLatestLineRecord();
    const char* outLine = (line && std::strlen(line) > 0) ? line : "### NOCARRIER ###\n";
    client.print(outLine);
    _last_pushed_record_seq = _renderer->getCalculator().getRecordSequence();

    // Limit active streaming sockets to 4
    while (_raw_clients.size() >= 4) {
        _raw_clients.front().stop();
        _raw_clients.erase(_raw_clients.begin());
    }

    _raw_clients.push_back(client);
}

void R2RWebServer::processStreamingClients()
{
    if (_raw_clients.empty() || !_renderer) {
        return;
    }

    auto& calc = _renderer->getCalculator();
    uint32_t currentSeq = calc.getRecordSequence();
    bool seqChanged = (currentSeq != _last_pushed_record_seq);

    uint32_t intervalMs = 2000; // 2.0s cadence for periodic repeat of STOPPED / NOCARRIER
    if ((calc.getPlayState() == TapePlayState::NormalPlayA || calc.getPlayState() == TapePlayState::NormalPlayB) && calc.isPlayQualified()) {
        switch (calc.getTapeSpeed()) {
            case TapeSpeed::Ips15:   intervalMs = 125; break; // 8 rec/s (15.0 IPS)
            case TapeSpeed::Ips7_5:  intervalMs = 250; break; // 4 rec/s (7.50 IPS)
            case TapeSpeed::Ips3_75:
            default:                 intervalMs = 500; break; // 2 rec/s (3.75 IPS)
        }
    }

    // Push IMMEDIATELY if state/sequence changed (0ms latency), OR periodically when interval has elapsed
    if (seqChanged || (millis() - _last_raw_push_ms >= intervalMs)) {
        _last_raw_push_ms = millis();
        _last_pushed_record_seq = currentSeq;
        const char* line = calc.getLatestLineRecord();
        const char* outLine = (line && std::strlen(line) > 0) ? line : "### NOCARRIER ###\n";

        for (auto it = _raw_clients.begin(); it != _raw_clients.end(); ) {
            if (it->connected()) {
                it->print(outLine);
                ++it;
            } else {
                it->stop();
                it = _raw_clients.erase(it);
            }
        }
    }
}

void R2RWebServer::handleInfo()
{
    if (!_renderer) {
        _server.send(500, "text/plain", "ERROR\n");
        return;
    }

    const char* line = _renderer->getCalculator().getLatestLineRecord();
    char cleanLine[64];
    if (line && std::strlen(line) > 0) {
        std::strncpy(cleanLine, line, sizeof(cleanLine) - 1);
        cleanLine[sizeof(cleanLine) - 1] = '\0';
        char* nl = std::strchr(cleanLine, '\r');
        if (nl) *nl = '\0';
        nl = std::strchr(cleanLine, '\n');
        if (nl) *nl = '\0';
    } else {
        std::strncpy(cleanLine, "### NOCARRIER ###", sizeof(cleanLine));
    }

    char resp[128];
    std::snprintf(resp, sizeof(resp), "DECODE %s\n", cleanLine);

    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.send(200, "text/plain", resp);
}

void R2RWebServer::handleTest()
{
    if (!_renderer) {
        _server.send(500, "application/json", "{\"error\":\"Renderer not initialized\"}");
        return;
    }

    auto& calc = _renderer->getCalculator();

    // 1. Process Tape Speed (IPS)
    if (_server.hasArg("ips")) {
        float ipsVal = _server.arg("ips").toFloat();
        if (ipsVal >= 14.0f) {
            calc.setTapeSpeed(TapeSpeed::Ips15);
        } else if (ipsVal >= 7.0f) {
            calc.setTapeSpeed(TapeSpeed::Ips7_5);
        } else {
            calc.setTapeSpeed(TapeSpeed::Ips3_75);
        }
    }

    // 2. Process Play Qualification Delay (e.g. qual=0.8 or qual=800)
    if (_server.hasArg("qual")) {
        float qVal = _server.arg("qual").toFloat();
        if (qVal > 10.0f) qVal /= 1000.0f; // Support ms input like qual=800
        calc.setPlayQualificationSec(qVal);
    }

    // 3. Process FF / FREV Wind Time Scaling Factor (e.g. wind_scale=2.5 or ff_scale=3.0)
    if (_server.hasArg("wind_scale")) {
        calc.setWindTimeScale(_server.arg("wind_scale").toFloat());
    } else if (_server.hasArg("ff_scale")) {
        calc.setWindTimeScale(_server.arg("ff_scale").toFloat());
    }

    // 4. Process RPM
    float customRpm = -1.0f;
    if (_server.hasArg("rpm")) {
        customRpm = std::max(0.0f, _server.arg("rpm").toFloat());
    }

    // 5. Process Mode (stop, play, ccw, cw, ffw, frev, live/imu, reset)
    if (_server.hasArg("mode")) {
        String modeStr = _server.arg("mode");
        modeStr.toLowerCase();
        modeStr.trim();

        if (modeStr == "stop" || modeStr == "stopped" || modeStr == "pause") {
            calc.setTestMode(TestModeOverride::Stop, customRpm >= 0.0f ? customRpm : 0.0f);
        } else if (modeStr == "play" || modeStr == "play_ccw" || modeStr == "playccw" || modeStr == "ccw" || modeStr == "playa" || modeStr == "play_a" || modeStr == "fwd" || modeStr == "forward" || modeStr == "a") {
            calc.setTestMode(TestModeOverride::PlayCCW, customRpm >= 0.0f ? customRpm : 25.0f);
        } else if (modeStr == "play_cw" || modeStr == "playcw" || modeStr == "cw" || modeStr == "playb" || modeStr == "play_b" || modeStr == "b") {
            calc.setTestMode(TestModeOverride::PlayCW, customRpm >= 0.0f ? customRpm : 25.0f);
        } else if (modeStr == "ffw" || modeStr == "ff" || modeStr == "fastforward" || modeStr == "fast_forward" || modeStr == "fast_fwd") {
            calc.setTestMode(TestModeOverride::FastForward, customRpm >= 0.0f ? customRpm : 100.0f);
        } else if (modeStr == "frev" || modeStr == "rev" || modeStr == "rew" || modeStr == "rewind" || modeStr == "fastreverse" || modeStr == "fast_reverse" || modeStr == "fast_rev") {
            calc.setTestMode(TestModeOverride::FastReverse, customRpm >= 0.0f ? customRpm : 100.0f);
        } else if (modeStr == "live" || modeStr == "imu" || modeStr == "real" || modeStr == "off" || modeStr == "disabled" || modeStr == "sensor" || modeStr == "sensors") {
            calc.setTestMode(TestModeOverride::Disabled);
        } else if (modeStr == "reset" || modeStr == "reset_time" || modeStr == "reset_tape_time" || modeStr == "zero") {
            calc.resetSimulatedTapeSeconds();
        }
    } else if (customRpm >= 0.0f) {
        // If only RPM was supplied without explicit mode
        if (calc.isTestModeActive()) {
            calc.setTestMode(calc.getTestMode(), customRpm);
        } else if (customRpm > 0.0f) {
            calc.setTestMode(TestModeOverride::PlayCCW, customRpm);
        } else {
            calc.setTestMode(TestModeOverride::Stop, 0.0f);
        }
    }

    // 6. Process Direct Tape Time Reset or Setting
    if (_server.hasArg("reset_tape_time") || _server.hasArg("reset") || _server.hasArg("reset_time")) {
        calc.resetSimulatedTapeSeconds();
    }
    if (_server.hasArg("set_tape_time")) {
        calc.setSimulatedTapeSeconds(_server.arg("set_tape_time").toFloat());
    }

    // Build JSON Response
    char jsonBuf[512];
    uint32_t tapeSec = static_cast<uint32_t>(calc.getSimulatedTapeSeconds());
    String recordClean = calc.getLatestLineRecord();
    recordClean.replace("\n", "");
    recordClean.replace("\r", "");

    std::snprintf(jsonBuf, sizeof(jsonBuf),
        "{"
        "\"status\":\"ok\","
        "\"testModeActive\":%s,"
        "\"testMode\":\"%s\","
        "\"playState\":\"%s\","
        "\"playQualified\":%s,"
        "\"tapeSide\":\"%c\","
        "\"nominalIps\":%.2f,"
        "\"simulatedRpm\":%.1f,"
        "\"windTimeScale\":%.2f,"
        "\"tapeSeconds\":%u,"
        "\"lineRecord\":\"%s\""
        "}",
        calc.isTestModeActive() ? "true" : "false",
        R2RCalculator::getTestModeName(calc.getTestMode()),
        R2RCalculator::getPlayStateName(calc.getPlayState()),
        calc.isPlayQualified() ? "true" : "false",
        calc.getTapeSide(),
        calc.getNominalIps(),
        calc.getSimulatedRpm(),
        calc.getWindTimeScale(),
        tapeSec,
        recordClean.c_str()
    );

    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", jsonBuf);
}

void R2RWebServer::handleTelemetry()
{
    if (!_renderer) {
        _server.send(500, "application/json", "{\"error\":\"Renderer not initialized\"}");
        return;
    }

    const auto& calc  = _renderer->getCalculator();
    const auto& stats = calc.getStats();
    int mode = static_cast<int>(_renderer->getMode());
    int hub  = static_cast<int>(calc.getHubType());
    int bat  = M5.Power.getBatteryLevel();
    float fps = _renderer->getFps();
    float cpu = _renderer->getCpuPct();
    float db  = _renderer->getMicDb();
    float peakDb = _renderer->getMicPeakDb();
    _rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

    auto dt = M5.Rtc.getDateTime();
    char timeBuf[16];
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", dt.time.hours, dt.time.minutes, dt.time.seconds);

    // Format simulated tape timecode
    uint32_t tapeTotalSec = static_cast<uint32_t>(calc.getSimulatedTapeSeconds());
    uint32_t tapeMin = tapeTotalSec / 60;
    uint32_t tapeSec = tapeTotalSec % 60;
    char tapeTimeBuf[16];
    std::snprintf(tapeTimeBuf, sizeof(tapeTimeBuf), "%02u:%02u", tapeMin, tapeSec);

    String lineClean = calc.getLatestLineRecord();
    lineClean.replace("\n", "");
    lineClean.replace("\r", "");

    int16_t micRaw[R2RRenderer::MIC_POINTS];
    _renderer->getMicSamples(micRaw, R2RRenderer::MIC_POINTS);

    // Format telemetry JSON
    char jsonBuf[1792];
    int offset = std::snprintf(jsonBuf, sizeof(jsonBuf),
        "{"
        "\"device\":\"R2RWatch\","
        "\"time\":\"%s\","
        "\"timeSynced\":%s,"
        "\"scale1to1\":%s,"
        "\"rpm\":%.1f,"
        "\"rawRpm\":%.1f,"
        "\"minRpm\":%.1f,"
        "\"maxRpm\":%.1f,"
        "\"avgRpm\":%.1f,"
        "\"ips\":%.1f,"
        "\"dir\":\"%s\","
        "\"flutter\":%.2f,"
        "\"p2p\":%.2f,"
        "\"drift\":%.2f,"
        "\"turns\":%.1f,"
        "\"feet\":%.1f,"
        "\"mode\":%d,"
        "\"hub\":%d,"
        "\"tapeSpeed\":%d,"
        "\"nominalIps\":%.2f,"
        "\"packRadius\":%.2f,"
        "\"packFullness\":%.1f,"
        "\"capstanLocked\":%s,"
        "\"bat\":%d,"
        "\"fps\":%.0f,"
        "\"cpu\":%.0f,"
        "\"rssi\":%d,"
        "\"db\":%.1f,"
        "\"peakDb\":%.1f,"
        "\"micDivisor\":%.2f,"
        "\"micDivisorLabel\":\"%s\","
        "\"tapeSeconds\":%.1f,"
        "\"tapeTime\":\"%s\","
        "\"playState\":\"%s\","
        "\"playQualified\":%s,"
        "\"tapeSide\":\"%c\","
        "\"testModeActive\":%s,"
        "\"testModeName\":\"%s\","
        "\"windTimeScale\":%.2f,"
        "\"lineRecord\":\"%s\","
        "\"track\":\"%s\",\n        \"recordRateHz\":%.1f,"
        "\"mic\":[",
        timeBuf,
        _time_synced ? "true" : "false",
        _renderer->isNative1to1() ? "true" : "false",
        stats.currentRpm,
        stats.rawRpm,
        stats.minRpm,
        stats.maxRpm,
        stats.avgRpm,
        stats.ips,
        stats.isMoving ? (stats.isClockwise ? "CW" : "CCW") : "STOPPED",
        stats.wowFlutterPct,
        (stats.maxRpm - stats.minRpm),
        (stats.currentRpm - stats.avgRpm),
        stats.totalTurns,
        stats.tapeFeet,
        mode,
        hub,
        static_cast<int>(calc.getTapeSpeed()),
        calc.getNominalIps(),
        stats.packRadiusInches,
        stats.packFullnessPct,
        stats.isCapstanLocked ? "true" : "false",
        bat,
        fps,
        cpu,
        _rssi,
        db,
        peakDb,
        _renderer->getMicDivisor(),
        _renderer->getMicDivisorLabel(),
        calc.getSimulatedTapeSeconds(),
        tapeTimeBuf,
        R2RCalculator::getPlayStateName(calc.getPlayState()),
        calc.isPlayQualified() ? "true" : "false",
        calc.getTapeSide(),
        calc.isTestModeActive() ? "true" : "false",
        R2RCalculator::getTestModeName(calc.getTestMode()),
        calc.getWindTimeScale(),
        lineClean.c_str(),
        _currentTrack,
        calc.getRecordRateHz()
    );

    // Append normalized audio samples [-1.0, 1.0]
    for (size_t i = 0; i < R2RRenderer::MIC_POINTS; i++) {
        float norm = std::clamp(static_cast<float>(micRaw[i]) / 16384.0f, -1.0f, 1.0f);
        offset += std::snprintf(jsonBuf + offset, sizeof(jsonBuf) - offset,
            (i == 0) ? "%.2f" : ",%.2f", norm);
    }

    std::snprintf(jsonBuf + offset, sizeof(jsonBuf) - offset, "]}");

    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", jsonBuf);
}

void R2RWebServer::handleAction()
{
    if (!_renderer) {
        _server.send(500, "application/json", "{\"error\":\"Renderer not initialized\"}");
        return;
    }

    auto& calc = _renderer->getCalculator();

    if (_server.hasArg("next_mode")) {
        _renderer->nextMode();
    }
    if (_server.hasArg("toggle_scale")) {
        _renderer->toggleScalingMode();
    }
    if (_server.hasArg("hub")) {
        int hubIdx = _server.arg("hub").toInt();
        if (hubIdx >= 0 && hubIdx <= 2) {
            calc.setHubType(static_cast<ReelHubType>(hubIdx));
        }
    }
    if (_server.hasArg("tape_speed")) {
        int spdIdx = _server.arg("tape_speed").toInt();
        if (spdIdx >= 0 && spdIdx <= 2) {
            calc.setTapeSpeed(static_cast<TapeSpeed>(spdIdx));
        }
    }
    if (_server.hasArg("mic_div")) {
        float divVal = _server.arg("mic_div").toFloat();
        if (divVal <= 0.02f) {
            _renderer->setMicDivisorIndex(4); // 0.01x
        } else if (divVal <= 0.15f) {
            _renderer->setMicDivisorIndex(3); // 0.10x
        } else if (divVal <= 0.35f) {
            _renderer->setMicDivisorIndex(2); // 0.25x
        } else if (divVal <= 0.75f) {
            _renderer->setMicDivisorIndex(1); // 0.5x
        } else {
            _renderer->setMicDivisorIndex(0); // 1.0x
        }
    }
    if (_server.hasArg("next_mic_div")) {
        _renderer->nextMicDivisor();
    }
    if (_server.hasArg("reset_counter")) {
        calc.resetStats();
    }
    if (_server.hasArg("reset_tape_time")) {
        calc.resetSimulatedTapeSeconds();
    }
    if (_server.hasArg("set_tape_time")) {
        calc.setSimulatedTapeSeconds(_server.arg("set_tape_time").toFloat());
    }

    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void R2RWebServer::handleSetTime()
{
    bool updated = false;
    m5::rtc_datetime_t dt = M5.Rtc.getDateTime();

    if (_server.hasArg("epoch")) {
        int64_t epochVal = _server.arg("epoch").toInt();
        if (epochVal > 1000000000000LL) {
            epochVal /= 1000LL;
        }
        time_t rawtime = static_cast<time_t>(epochVal);
        struct tm* timeinfo = localtime(&rawtime);
        if (timeinfo) {
            dt.date.year = timeinfo->tm_year + 1900;
            dt.date.month = timeinfo->tm_mon + 1;
            dt.date.date = timeinfo->tm_mday;
            dt.date.weekDay = timeinfo->tm_wday;
            dt.time.hours = timeinfo->tm_hour;
            dt.time.minutes = timeinfo->tm_min;
            dt.time.seconds = timeinfo->tm_sec;
            M5.Rtc.setDateTime(dt);
            struct timeval tv = { .tv_sec = rawtime, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            updated = true;
        }
    } else if (_server.hasArg("hours") && _server.hasArg("minutes") && _server.hasArg("seconds")) {
        if (_server.hasArg("year")) dt.date.year = _server.arg("year").toInt();
        if (_server.hasArg("month")) dt.date.month = _server.arg("month").toInt();
        if (_server.hasArg("day")) dt.date.date = _server.arg("day").toInt();
        dt.time.hours = _server.arg("hours").toInt();
        dt.time.minutes = _server.arg("minutes").toInt();
        dt.time.seconds = _server.arg("seconds").toInt();
        M5.Rtc.setDateTime(dt);
        updated = true;
    }

    if (updated) {
        _time_synced = true;
        Serial.printf("[RTC] Manual/Browser time synchronization applied: %04d-%02d-%02d %02d:%02d:%02d\n",
                      dt.date.year, dt.date.month, dt.date.date,
                      dt.time.hours, dt.time.minutes, dt.time.seconds);
        char resp[128];
        std::snprintf(resp, sizeof(resp),
                      "{\"status\":\"ok\",\"time\":\"%02d:%02d:%02d\",\"synced\":true}",
                      dt.time.hours, dt.time.minutes, dt.time.seconds);
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.send(200, "application/json", resp);
    } else {
        _server.sendHeader("Access-Control-Allow-Origin", "*");
        _server.send(400, "application/json", "{\"error\":\"Missing time parameters\"}");
    }
}

} // namespace r2r

