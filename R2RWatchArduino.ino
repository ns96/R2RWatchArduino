/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */

#include <Arduino.h>
#include <cstdint>
#include <Wire.h>
#include <M5Unified.h>
#include "r2r_renderer.h"
#include "r2r_web_server.h"

static r2r::R2RRenderer renderer;
static r2r::R2RWebServer webServer;
static uint32_t last_tick = 0;

void setup() {
    // 0. Initialize Serial port for diagnostics at 115200 baud
    Serial.begin(115200);
    delay(200);
    Serial.println("\n\n==========================================");
    Serial.println("     R2R WATCH FIRMWARE BOOTING...        ");
    Serial.println("==========================================");

    // 1. Initialize M5Unified hardware subsystems (Display, PMIC, IMU, Touch, Buttons)
    auto cfg = M5.config();
    M5.begin(cfg);
    Wire.setClock(400000); // 400 kHz Fast I2C for sub-millisecond IMU and PMIC queries

    // 2. Set CPU clock to maximum 240 MHz safely after M5 initialization
    setCpuFrequencyMhz(240);
    Serial.printf("[SYSTEM] CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("[SYSTEM] Initial Free Heap: %u bytes | Free PSRAM: %u bytes\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());

    // 3. Configure display orientation & backlight brightness
    M5.Display.setRotation(0);
    M5.Display.setBrightness(128);

    // 4. Initialize Hardware Microphone
    if (M5.Mic.isEnabled()) {
        M5.Mic.begin();
        Serial.println("[SYSTEM] Hardware Microphone initialized for acoustic sound sampling");
    }

    // 5. Initialize Graphics Canvas in unfragmented Internal SRAM
    renderer.init(&M5.Display);

    // 6. Initialize Wi-Fi & WebServer
    webServer.init(&renderer);

    Serial.printf("[SYSTEM] Post-Init Free Heap: %u bytes | Free PSRAM: %u bytes\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());
    Serial.println("[SYSTEM] Boot sequence complete. Starting main execution loop.\n");

    last_tick = millis();
}

void loop() {
    int64_t t_start = esp_timer_get_time();

    // 1. Update hardware state (Poll buttons, touch, IMU, power management)
    int64_t t0 = esp_timer_get_time();
    M5.update();
    int64_t t_m5 = esp_timer_get_time() - t0;

    // 2. Handle incoming WebServer HTTP client requests (< 0.05ms)
    t0 = esp_timer_get_time();
    webServer.update();
    int64_t t_web = esp_timer_get_time() - t0;

    uint32_t now = millis();
    float dt = static_cast<float>(now - last_tick) / 1000.0f;
    if (dt <= 0.0f) dt = 0.016f;
    last_tick = now;

    // 3. Read 6-axis IMU telemetry directly from M5Unified
    t0 = esp_timer_get_time();
    r2r::ImuData imuData;
    M5.Imu.getAccel(&imuData.accelX, &imuData.accelY, &imuData.accelZ);
    M5.Imu.getGyro(&imuData.gyroX, &imuData.gyroY, &imuData.gyroZ);
    int64_t t_imu = esp_timer_get_time() - t0;

    // 4. Update kinematics and render directly to AMOLED via hardware DMA
    t0 = esp_timer_get_time();
    renderer.update(imuData, dt);
    int64_t t_upd = esp_timer_get_time() - t0;

    t0 = esp_timer_get_time();
    renderer.render(&M5.Display);
    int64_t t_rnd = esp_timer_get_time() - t0;

    int64_t loop_us = esp_timer_get_time() - t_start;
    renderer.updateCpuUsage(static_cast<uint32_t>(loop_us), 16000);

    static uint32_t last_prof = 0;
    if (now - last_prof > 1000) {
        last_prof = now;
        Serial.printf("[LOOP_PROFILE] M5.update: %lld us | Web: %lld us | IMU: %lld us | Upd: %lld us | Render: %lld us | Total: %lld us | FPS: %.1f\n",
                      t_m5, t_web, t_imu, t_upd, t_rnd, loop_us, renderer.getFps());
    }

    taskYIELD();
}
