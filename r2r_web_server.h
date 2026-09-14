/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vector>
#include <WiFi.h>
#include <WebServer.h>
#include "r2r_renderer.h"

namespace r2r {

class R2RWebServer {
public:
    R2RWebServer();
    ~R2RWebServer();

    void init(R2RRenderer* renderer);
    void update(); // Non-blocking client poll (< 0.05ms)

    bool isConnected() const { return _is_connected; }
    bool isApMode() const { return _is_ap_mode; }
    bool isTimeSynced() const { return _time_synced; }
    String getIpAddress() const { return _ip_str; }
    int getRssi() const { return _rssi; }

    bool syncNtpTime(uint32_t timeoutMs = 4000);
    bool fetchNtpTimeUDP(const char* serverHost, uint32_t timeoutMs = 1500);

private:
    R2RRenderer* _renderer = nullptr;
    WebServer _server{80};

    bool _is_connected = false;
    bool _is_ap_mode = false;
    bool _time_synced = false;
    String _ip_str = "0.0.0.0";
    int _rssi = 0;
    char _currentTrack[128] = "No Track Playing";

    std::vector<WiFiClient> _raw_clients;
    uint32_t _last_raw_push_ms = 0;
    uint32_t _last_pushed_record_seq = 0;

    void setupRoutes();
    void handleRoot();
    void handleStream();
    void handlePlaying();
    void handleTelemetry();
    void handleAction();
    void handleSetTime();
    void handleRaw();
    void handleTest();
    void handleInfo();
    void processStreamingClients();
};

} // namespace r2r
