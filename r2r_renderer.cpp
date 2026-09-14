/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#pragma GCC optimize ("O3,unroll-loops,fast-math")

#include "r2r_renderer.h"
#include <M5Unified.h>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace r2r {

R2RRenderer::R2RRenderer()
{
}

R2RRenderer::~R2RRenderer()
{
    if (_initialized) {
        _canvas.deleteSprite();
    }
}

void R2RRenderer::init(LGFX_Device* display)
{
    _display = display;

    // Allocate 280x280 16-bit canvas (156 KB) in fast Internal SRAM (Zero wait-state 240MHz bus)
    _canvas.setPsram(false);
    _canvas.setColorDepth(16);
    if (!_canvas.createSprite(CANVAS_SIZE, CANVAS_SIZE)) {
        _canvas.setPsram(true);
        _canvas.createSprite(CANVAS_SIZE, CANVAS_SIZE);
        Serial.println("[RENDERER] Canvas allocated in PSRAM");
    } else {
        Serial.println("[RENDERER] *** CANVAS ALLOCATED IN ULTRA-FAST INTERNAL SRAM! ***");
    }

    _canvas.setPivot(CANVAS_CENTER, CANVAS_CENTER);
    _canvas.setTextDatum(textdatum_t::middle_center);

    // Initial clean zeroing of display outer border (pitch black AMOLED)
    _display->clear(COLOR_BG);

    _initialized = true;
}

void R2RRenderer::nextMode()
{
    int next = (static_cast<int>(_current_mode) + 1) % static_cast<int>(ViewMode::Count);
    _current_mode = static_cast<ViewMode>(next);
}

void R2RRenderer::update(const ImuData& imuData, float dt)
{
    _calculator.update(imuData, dt);

    if (dt > 0.0001f) {
        float instantFps = 1.0f / dt;
        _current_fps = (0.90f * _current_fps) + (0.10f * instantFps);
    }

    // Advance full-screen discrete ring step during high-speed spooling (>= 50 RPM)
    if (_calculator.isHighSpeedWindMode()) {
        _ring_step_timer_sec += dt;
        if (_ring_step_timer_sec >= _ring_step_interval_sec) {
            _ring_step_timer_sec -= _ring_step_interval_sec;
            _current_ring_step++;
            if (_current_ring_step >= RING_STEP_COUNT) {
                _current_ring_step = 0;
            }
        }
    } else {
        _ring_step_timer_sec = 0.0f;
        _current_ring_step = 0;
    }

    // Read real acoustic sound samples from hardware microphone asynchronously
    if (M5.Mic.isEnabled()) {
        if (!M5.Mic.isRecording()) {
            float div = getMicDivisor();
            float sum_sq = 0.0f;
            for (size_t i = 0; i < MIC_POINTS; i++) {
                float raw = static_cast<float>(_raw_dma_buf[i]);
                float scaled = raw * div;
                _mic_samples[i] = static_cast<int16_t>(std::clamp(scaled, -32768.0f, 32767.0f));
                sum_sq += scaled * scaled;
            }
            float rms = std::sqrt(sum_sq / static_cast<float>(MIC_POINTS));
            float norm = std::clamp(rms / 2500.0f, 0.0f, 1.0f);
            float targetDb = -20.0f + (norm * 23.0f);
            _mic_rms_db = (0.6f * _mic_rms_db) + (0.4f * targetDb);
            if (_mic_rms_db > _mic_peak_db) {
                _mic_peak_db = _mic_rms_db;
                _peak_hold_tick = millis();
            } else if (millis() - _peak_hold_tick > 500) {
                _mic_peak_db -= 0.5f;
                if (_mic_peak_db < -20.0f) _mic_peak_db = -20.0f;
            }

            // Start next asynchronous DMA capture
            M5.Mic.record(_raw_dma_buf, MIC_POINTS, 16000);
        }
    }

    auto tp = M5.Touch.getDetail();

    // Button B or tapping bottom area
    if (M5.BtnB.wasPressed() || (tp.wasPressed() && tp.y > 320)) {
        if (_current_mode == ViewMode::WatchFace) {
            toggleScalingMode(); // Toggles between Fullscreen (1.6643x) and Native 1:1 (1.0x)
        } else if (_current_mode == ViewMode::Tachometer) {
            _calculator.nextTapeSpeed(); // Cycles 3.75 -> 7.50 -> 15.0 IPS Capstan Presets
        } else if (_current_mode == ViewMode::VuMeter) {
            nextMicDivisor(); // Cycles raw mic signal divisor: 1.0 (default) -> 0.5 -> 0.25 -> 0.10 -> 0.01 -> 1.0
        } else {
            _calculator.nextHubType(); // Cycles 7" Cine -> 10.5" NAB -> 3" Small Hub Presets
        }
    }
    // Button A or tapping upper screen area switches Screen View Mode
    else if (M5.BtnA.wasPressed() || (tp.wasPressed() && tp.y <= 320)) {
        nextMode();
    }
}

void R2RRenderer::render(LGFX_Device* display)
{
    if (!_initialized || !_display) return;

    // Erase physical display once when switching between Fullscreen and 1:1 to clear outer borders
    if (_need_screen_clear) {
        _display->clear(COLOR_BG);
        _need_screen_clear = false;
    }

    int64_t t0 = esp_timer_get_time();

    // 1. Ultra-Fast 32-bit block memset: Zeroes 280x280 buffer in < 0.08ms
    std::memset(_canvas.getBuffer(), 0, CANVAS_SIZE * CANVAS_SIZE * sizeof(uint16_t));
    int64_t t1 = esp_timer_get_time();

    // 2. Draw selected active screen view with fast native bitmap fonts
    if (_calculator.isHighSpeedWindMode()) {
        const auto& stats = _calculator.getStats();
        bool isFFW = (_calculator.getPlayState() == TapePlayState::FastForward || !stats.isClockwise);
        drawFullScreenConcentricCircles(isFFW, stats.currentRpm);
    } else {
        switch (_current_mode) {
            case ViewMode::Tachometer:  drawTachometer();  break;
            case ViewMode::VuMeter:     drawVuMeter();     break;
            case ViewMode::WowFlutter:  drawWowFlutter();  break;
            case ViewMode::TapeCounter: drawTapeCounter(); break;
            case ViewMode::WatchFace:   drawWatchFace();   break;
            default:                    drawTachometer();  break;
        }
        drawFooterNav();
    }
    int64_t t2 = esp_timer_get_time();

    // 3. Counter-rotation angle calculation
    float counterAngle = 0.0f;
    if (!_calculator.isHighSpeedWindMode() && _calculator.isAutoRotationEnabled()) {
        counterAngle = _calculator.getCounterRotationAngleDeg();
    }

    // 4. Smooth Affine Rotation: Scaled 1.6643x to fill the full 466x466 AMOLED screen edge-to-edge
    float zoom = _use_native_1to1 ? 1.0f : ZOOM_SCALE;
    _display->startWrite();
    _canvas.pushRotateZoom(_display, 233, 233, counterAngle, zoom, zoom);
    _display->endWrite();

    int64_t t3 = esp_timer_get_time();

    static uint32_t last_prof = 0;
    if (millis() - last_prof > 1000) {
        last_prof = millis();
        Serial.printf("[RENDER_PROFILE] Clear: %lld us | Draw: %lld us | Push(DMA): %lld us | Total: %lld us | Instant FPS: %.1f\n",
                      (t1 - t0), (t2 - t1), (t3 - t2), (t3 - t0), _current_fps);
    }
}

void R2RRenderer::drawHeader(const char* title)
{
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString(title, CANVAS_CENTER, 18);
    _canvas.drawFastHLine(30, 30, 220, COLOR_GREY);

    if (_calculator.isTestModeActive()) {
        _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
        _canvas.drawString("[TEST]", 42, 18);
    }
}

void R2RRenderer::drawFooterNav()
{
    int modeCount = static_cast<int>(ViewMode::Count);
    int current   = static_cast<int>(_current_mode);
    int startX    = CANVAS_CENTER - ((modeCount - 1) * 8);

    for (int i = 0; i < modeCount; i++) {
        uint16_t color = (i == current) ? COLOR_WHITE : COLOR_GREY;
        int radius     = (i == current) ? 3 : 2;
        _canvas.fillCircle(startX + (i * 16), 265, radius, color);
    }
}

// =============================================================================
// Mode 1: High-Contrast Minimalist Tachometer (280x280 Fast Native Fonts)
// =============================================================================
void R2RRenderer::drawTachometer()
{
    drawHeader("TACHOMETER");

    const auto& stats = _calculator.getStats();

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", stats.currentRpm);
    _canvas.setFont(&fonts::Font4);
    _canvas.setTextSize(2);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 75);

    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString("RPM", CANVAS_CENTER, 118);

    // Direction & Transport Status Readout
    const char* dirStr = "STOPPED";
    if (stats.isMoving) {
        dirStr = stats.isClockwise ? ">> CW (SIDE B) >>" : "<< CCW (SIDE A) <<";
    }
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(dirStr, CANVAS_CENTER, 148);

    // Linear Speed (Capstan Speed during play, Dynamic during wind)
    if (!stats.isMoving) {
        std::snprintf(buf, sizeof(buf), "0.0 IPS");
    } else if (stats.isCapstanLocked) {
        std::snprintf(buf, sizeof(buf), "%.2f IPS", stats.ips);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f IPS (WIND)", stats.ips);
    }
    _canvas.setFont(&fonts::Font4);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 180);

    // Min / Max Session RPM Stats
    std::snprintf(buf, sizeof(buf), "MIN %.1f | MAX %.1f", stats.minRpm, stats.maxRpm);
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 218);

    std::snprintf(buf, sizeof(buf), "[ %s ]", R2RCalculator::getTapeSpeedName(_calculator.getTapeSpeed()));
    _canvas.drawString(buf, CANVAS_CENTER, 240);
}

// =============================================================================
// Mode 2: Real-Time Acoustic Waveform Oscilloscope (280x280)
// =============================================================================
void R2RRenderer::drawVuMeter()
{
    drawHeader("MIC OSCILLOSCOPE");

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%+.1f dB", _mic_rms_db);
    _canvas.setFont(&fonts::Font4);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 46);

    std::snprintf(buf, sizeof(buf), "PEAK: %+.1f dB", _mic_peak_db);
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 66);

    // 1. Oscilloscope Frame & Subdued Center Grid
    int ox = 18;
    int oy = 80;
    int ow = 244;
    int oh = 124;
    int midY = oy + (oh / 2);

    _canvas.drawRoundRect(ox, oy, ow, oh, 6, COLOR_GREY);

    // Dotted Center Baseline
    for (int x = ox + 4; x < ox + ow - 4; x += 6) {
        _canvas.drawFastHLine(x, midY, 3, COLOR_GREY);
    }
    // Dotted Vertical Quarter Lines
    for (int y = oy + 4; y < oy + oh - 4; y += 6) {
        _canvas.drawFastVLine(ox + (ow / 4), y, 3, COLOR_GREY);
        _canvas.drawFastVLine(ox + (ow / 2), y, 3, COLOR_GREY);
        _canvas.drawFastVLine(ox + (3 * ow / 4), y, 3, COLOR_GREY);
    }

    // 2. Real-Time Connected Waveform Polyline Trace
    float stepX = static_cast<float>(ow - 12) / static_cast<float>(MIC_POINTS - 1);
    int startX = ox + 6;
    int maxAmp = (oh / 2) - 6;

    int prevX = startX;
    float norm0 = std::clamp(static_cast<float>(_mic_samples[0]) / 2500.0f, -1.0f, 1.0f);
    int prevY = midY - static_cast<int>(norm0 * maxAmp);

    for (size_t i = 1; i < MIC_POINTS; i++) {
        int curX = startX + static_cast<int>(i * stepX);
        float norm = std::clamp(static_cast<float>(_mic_samples[i]) / 2500.0f, -1.0f, 1.0f);
        int curY = midY - static_cast<int>(norm * maxAmp);
        _canvas.drawLine(prevX, prevY, curX, curY, COLOR_WHITE);
        _canvas.drawLine(prevX, prevY + 1, curX, curY + 1, COLOR_WHITE); // 2px crisp beam
        prevX = curX;
        prevY = curY;
    }

    // 3. Status Badges & Button Interaction Hint
    std::snprintf(buf, sizeof(buf), "[ DIV: %s ]", getMicDivisorLabel());
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 218);

    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString("BTN B: CYCLE DIVISOR", CANVAS_CENTER, 238);
}

// =============================================================================
// Mode 3: Minimalist Speed Stability & Flutter Analyzer (280x280)
// =============================================================================
void R2RRenderer::drawWowFlutter()
{
    drawHeader("WOW & FLUTTER");

    const auto& stats = _calculator.getStats();

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f %%", stats.wowFlutterPct);
    _canvas.setFont(&fonts::Font4);
    _canvas.setTextSize(2);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 75);

    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString("RMS FLUTTER", CANVAS_CENTER, 118);

    float p2p = stats.maxRpm - stats.minRpm;
    std::snprintf(buf, sizeof(buf), "P-P FLUTTER: %.2f RPM", p2p);
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 155);

    float drift = stats.currentRpm - stats.avgRpm;
    std::snprintf(buf, sizeof(buf), "SPEED DRIFT: %+.2f RPM", drift);
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 190);

    std::snprintf(buf, sizeof(buf), "AVG SPEED: %.1f RPM", stats.avgRpm);
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 226);
}

// =============================================================================
// Mode 4: Minimalist Tape Turn & Distance Counter + DCT FSK Timecode (280x280)
// =============================================================================
void R2RRenderer::drawTapeCounter()
{
    drawHeader("TAPE COUNTER");

    const auto& stats = _calculator.getStats();
    uint32_t totalSec = static_cast<uint32_t>(_calculator.getSimulatedTapeSeconds());
    uint32_t mins = totalSec / 60;
    uint32_t secs = totalSec % 60;

    // 1. Primary Timecode Display (MM:SS)
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02u:%02u", mins, secs);
    _canvas.setFont(&fonts::Font4);
    _canvas.setTextSize(2);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 72);

    // 2. Playback State Badge
    const char* stateName = R2RCalculator::getPlayStateName(_calculator.getPlayState());
    char stateBuf[32];
    std::snprintf(stateBuf, sizeof(stateBuf), "[ %s ]", stateName);
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(stateBuf, CANVAS_CENTER, 114);

    // 3. Cadence & Set IPS
    std::snprintf(buf, sizeof(buf), "%.2f IPS | %.0f REC/S",
                  _calculator.getNominalIps(), _calculator.getRecordRateHz());
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 145);

    // 4. Physical Turns & Distance
    std::snprintf(buf, sizeof(buf), "%.1f TURNS | %.1f FT", stats.totalTurns, stats.tapeFeet);
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 175);

    // 5. Pack Radius & Fullness
    std::snprintf(buf, sizeof(buf), "PACK: %.2f\" (%d%% FULL)",
                  stats.packRadiusInches, static_cast<int>(stats.packFullnessPct));
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 205);

    // 6. Hub Preset
    const char* hubName = "[ 7\" CINE HUB ]";
    if (_calculator.getHubType() == ReelHubType::Nab105) hubName = "[ 10.5\" NAB HUB ]";
    else if (_calculator.getHubType() == ReelHubType::Small3) hubName = "[ 3\" SMALL HUB ]";
    _canvas.drawString(hubName, CANVAS_CENTER, 235);
}

// =============================================================================
// Mode 5: Minimalist System Telemetry Screen (280x280)
// =============================================================================
void R2RRenderer::drawWatchFace()
{
    drawHeader("SYSTEM INFO");

    auto dt = M5.Rtc.getDateTime();
    int batPct = M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging();

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", dt.time.hours, dt.time.minutes, dt.time.seconds);
    _canvas.setFont(&fonts::Font4);
    _canvas.setTextSize(2);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 75);

    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString("REAL TIME CLOCK", CANVAS_CENTER, 118);

    if (charging) {
        std::snprintf(buf, sizeof(buf), "%d%% [ CHARGING ]", batPct);
    } else {
        std::snprintf(buf, sizeof(buf), "%d%% BATTERY", batPct);
    }
    _canvas.setFont(&fonts::Font4);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 160);

    std::snprintf(buf, sizeof(buf), "%.0f FPS   %.0f%% CPU", _current_fps, _current_cpu_pct);
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 198);

    std::snprintf(buf, sizeof(buf), "IP: %s", _ip_address);
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextSize(2);
    _canvas.setTextColor(COLOR_GREY, COLOR_BG);
    _canvas.drawString(buf, CANVAS_CENTER, 226);

    const char* scaleStr = _use_native_1to1 ? "[ SCALE: 1:1 NATIVE ]" : "[ SCALE: FULLSCREEN ]";
    _canvas.setFont(&fonts::Font2);
    _canvas.setTextSize(1);
    _canvas.setTextColor(COLOR_WHITE, COLOR_BG);
    _canvas.drawString(scaleStr, CANVAS_CENTER, 252);
}

// =============================================================================
// Full-Screen Single Stepping Ring Animation (Fast Forward & Rewind) (280x280)
// =============================================================================
void R2RRenderer::drawFullScreenConcentricCircles(bool isFastForward, float /*rpm*/)
{
    constexpr float R_MIN = 6.0f;
    constexpr float R_MAX = 135.0f;
    constexpr float R_SPAN = R_MAX - R_MIN;

    // Fractional progress through the discrete steps (0.0 to 1.0)
    float stepFrac = static_cast<float>(_current_ring_step) / static_cast<float>(RING_STEP_COUNT - 1);

    // Fast Forward (CW) : decreases each step from outer edge (R_MAX) to center (R_MIN)
    // Fast Rewind  (CCW): increases each step from center (R_MIN) to outer edge (R_MAX)
    float r = isFastForward ? (R_MAX - (stepFrac * R_SPAN)) : (R_MIN + (stepFrac * R_SPAN));
    int rInt = static_cast<int>(r + 0.5f);

    if (rInt >= 4 && rInt <= 135) {
        // High-contrast clean wavefront ring
        _canvas.drawCircle(CANVAS_CENTER, CANVAS_CENTER, rInt, COLOR_WHITE);
        _canvas.drawCircle(CANVAS_CENTER, CANVAS_CENTER, rInt - 1, COLOR_WHITE);
    } else if (rInt < 4) {
        _canvas.fillCircle(CANVAS_CENTER, CANVAS_CENTER, 3, COLOR_WHITE);
    }
}

} // namespace r2r
