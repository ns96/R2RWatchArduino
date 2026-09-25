/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <Arduino.h>
#include <cstdint>
#include <M5GFX.h>
#include "r2r_calculator.h"
#include "r2r_panel.h"
#include <memory>
#include <vector>
#include <cmath>
#include <cstring>

namespace r2r {

enum class ViewMode {
    Tachometer = 0,
    VuMeter,
    WowFlutter,
    TapeCounter,
    WatchFace,
    Count
};

class R2RRenderer {
public:
    // Canvas Geometry: 280x280 (156 KB) in ultra-fast Internal SRAM
    // Zoomed 1.6643x to fill the full 466x466 round AMOLED display edge-to-edge (280 * 1.6643 = 466px)
    static constexpr int   CANVAS_SIZE   = 280;
    static constexpr int   CANVAS_CENTER = 140;
    static constexpr float ZOOM_SCALE    = 466.0f / static_cast<float>(CANVAS_SIZE); // 1.6643f (Full Screen Edge-to-Edge)

    R2RRenderer();
    ~R2RRenderer();

    void init(LGFX_Device* display);
    void update(const ImuData& imuData, float dt);
    void render(LGFX_Device* display);

    void nextMode();
    void setMode(ViewMode mode) { _current_mode = mode; }
    ViewMode getMode() const { return _current_mode; }

    void updateCpuUsage(uint32_t renderUs, uint32_t periodUs) {
        float cpu = (periodUs > 0) ? ((static_cast<float>(renderUs) / static_cast<float>(periodUs)) * 100.0f) : 10.0f;
        _current_cpu_pct = (0.90f * _current_cpu_pct) + (0.10f * std::clamp(cpu, 0.0f, 100.0f));
    }

    R2RCalculator& getCalculator() { return _calculator; }
    const R2RCalculator& getCalculator() const { return _calculator; }

    void toggleAutoRotation() { _calculator.setAutoRotationEnabled(!_calculator.isAutoRotationEnabled()); }

    void toggleScalingMode() {
        _use_native_1to1 = !_use_native_1to1;
        _need_screen_clear = true;
    }
    void setNative1to1(bool enable) {
        if (_use_native_1to1 != enable) {
            _use_native_1to1 = enable;
            _need_screen_clear = true;
        }
    }
    bool isNative1to1() const { return _use_native_1to1; }

    float getFps() const { return _current_fps; }
    float getCpuPct() const { return _current_cpu_pct; }
    float getMicDb() const { return _mic_rms_db; }
    float getMicPeakDb() const { return _mic_peak_db; }

    static constexpr size_t MIC_POINTS = 64;
    void getMicSamples(int16_t* out, size_t count) const {
        size_t n = std::min(count, MIC_POINTS);
        for (size_t i = 0; i < n; i++) out[i] = _mic_samples[i];
    }

    // Microphone Signal Attenuation / Division presets [1.0 (default), 0.5, 0.25, 0.10, 0.01]
    static constexpr float MIC_DIVISORS[] = { 1.0f, 0.5f, 0.25f, 0.10f, 0.01f };
    static constexpr size_t MIC_DIVISOR_COUNT = 5;

    void nextMicDivisor() {
        _mic_divisor_idx = (_mic_divisor_idx + 1) % MIC_DIVISOR_COUNT;
    }
    void setMicDivisorIndex(size_t idx) {
        if (idx < MIC_DIVISOR_COUNT) _mic_divisor_idx = idx;
    }
    size_t getMicDivisorIndex() const { return _mic_divisor_idx; }
    float getMicDivisor() const { return MIC_DIVISORS[_mic_divisor_idx]; }
    const char* getMicDivisorLabel() const {
        switch (_mic_divisor_idx) {
            case 0: return "1.0x (FULL)";
            case 1: return "0.5x (1/2)";
            case 2: return "0.25x (1/4)";
            case 3: return "0.10x (1/10)";
            case 4: return "0.01x (1/100)";
            default: return "1.0x";
        }
    }

    void setIpAddress(const char* ip) {
        if (ip) {
            std::strncpy(_ip_address, ip, sizeof(_ip_address) - 1);
            _ip_address[sizeof(_ip_address) - 1] = '\0';
        }
    }
    const char* getIpAddress() const { return _ip_address; }

    // =========================================================================
    // Single Stepping Ring Configuration (Fast Forward & Rewind)
    // =========================================================================
    static constexpr int   RING_STEP_COUNT          = 50;
    static constexpr float DEFAULT_STEP_INTERVAL_S  = 0.02f;

    void setRingStepIntervalSec(float sec) { _ring_step_interval_sec = (sec > 0.01f) ? sec : 0.01f; }
    float getRingStepIntervalSec() const { return _ring_step_interval_sec; }

    // High-Contrast Monochrome Palette (Optimized for AMOLED)
    static constexpr uint16_t COLOR_BG    = 0x0000; // Pitch Black #000000 (Pixels OFF)
    static constexpr uint16_t COLOR_WHITE = 0xFFFF; // Bright White #FFFFFF
    static constexpr uint16_t COLOR_GREY  = 0x7BEF; // Muted Grey #7C7C7C

private:
    LGFX_Device* _display = nullptr;
    LGFX_Sprite  _canvas;     // 280x280 16-bit canvas in Internal SRAM (zoomed 1.6643x to 466x466)

    // Preferred frame sink: streams bands straight to the QSPI panel. Stays inert if
    // it cannot borrow the panel or allocate its band buffer, in which case the
    // M5GFX framebuffer path below is used instead.
    BandPanel    _band_panel;

    R2RCalculator _calculator;
    ViewMode _current_mode = ViewMode::Tachometer;

    bool _initialized = false;
    bool _use_native_1to1 = false; // Default: Fullscreen edge-to-edge
    bool _need_screen_clear = false;
    uint32_t _btn_debounce_tick = 0;
    float _current_fps      = 60.0f;
    float _current_cpu_pct  = 10.0f;
    char _ip_address[32]    = "NO WIFI";

    // Persistent Real-Time Acoustic Microphone DMA Buffers
    int16_t _raw_dma_buf[MIC_POINTS] = {0};
    int16_t _mic_samples[MIC_POINTS] = {0};
    float _mic_rms_db = -20.0f;
    float _mic_peak_db = -20.0f;
    uint32_t _peak_hold_tick = 0;
    size_t _mic_divisor_idx = 0; // 0 = 1.0 (default), 1 = 0.5, 2 = 0.25

    // Single stepping ring animation tracking
    float _ring_step_interval_sec = DEFAULT_STEP_INTERVAL_S;
    float _ring_step_timer_sec    = 0.0f;
    int   _current_ring_step      = 0;

    void drawHeader(const char* title);
    void drawFooterNav();
    void drawTachometer();
    void drawVuMeter();
    void drawWowFlutter();
    void drawTapeCounter();
    void drawWatchFace();
    void drawFullScreenConcentricCircles(bool isFastForward, float rpm);

    // =========================================================================
    // Frame Transfer
    // =========================================================================
    // The visible area of the 468x468 CO5300 panel is a circle 466 px across centred at
    // (233,233); the four corners are physically masked off by the bezel. Pushing them costs
    // roughly 93 KB out of the 434 KB frame, over a QSPI link that is the hard floor on frame
    // time (434 KB at 40 MB/s is 10.9 ms). Splitting the push into horizontal bands, each
    // clipped to the chord of that circle, removes the dead traffic for the cost of ~15 extra
    // window commands per frame.
    static constexpr int VISIBLE_CENTER = 233;
    static constexpr int VISIBLE_RADIUS = 233;

    // Rows per band. Larger bands amortise the per-band window command; smaller bands track the
    // curve more closely and waste fewer pixels at the top and bottom of the circle. At 32 rows
    // the clip rectangles cover 181.5 k px against the circle's 170.6 k px, about 6% waste.
    static constexpr int FLUSH_BAND_H   = 32;

    // Push the whole canvas to the panel, band by band, clipped to the visible circle.
    void pushFrameInBands(float angleDeg, float zoom);
};

} // namespace r2r
