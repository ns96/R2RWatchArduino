/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace r2r {

struct ImuData {
    float accelX = 0.0f;
    float accelY = 0.0f;
    float accelZ = 0.0f;
    float gyroX  = 0.0f;
    float gyroY  = 0.0f;
    float gyroZ  = 0.0f;
};

enum class ReelHubType {
    Nab105 = 0,   // NAB Hub (10.5" Reel, Hub radius = 2.25", Flange = 5.25")
    Cine7,        // Standard Cine Hub (7" Reel, Hub radius = 1.125", Flange = 3.50")
    Small3,       // Small Hub (3"-5" Reel, Hub radius = 0.875", Flange = 2.50")
    Custom,       // Custom user specified hub radius
    Count
};

enum class TapeSpeed {
    Ips3_75 = 0,  // 3 3/4 IPS (3.75 ips / 9.5 cm/s) - Default
    Ips7_5,       // 7 1/2 IPS (7.50 ips / 19.0 cm/s)
    Ips15,        // 15 IPS (15.00 ips / 38.1 cm/s)
    Count
};

enum class RotationAxis {
    Auto,     // Auto-detect dominant axis using 3D vector magnitude
    AxisZ,    // Mounted flat (deck horizontal)
    AxisX,    // Mounted vertically (deck vertical)
    AxisY     // Mounted sideways
};

enum class TapePlayState {
    Stopped = 0,
    NormalPlayA,  // CCW normal rotation (Side A)
    NormalPlayB,  // CW normal rotation (Side B)
    FastForward,  // CCW high-speed wind
    FastReverse   // CW high-speed wind
};

enum class TestModeOverride {
    Disabled = 0, // Normal operation (Live 6-axis IMU sensing)
    Stop,         // Force stationary stop
    PlayCCW,      // Force Normal Play Side A (CCW)
    PlayCW,       // Force Normal Play Side B (CW)
    FastForward,  // Force Fast Forward (CCW high-speed)
    FastReverse   // Force Fast Reverse (CW high-speed)
};

struct R2RStats {
    float currentRpm       = 0.0f;
    float rawRpm           = 0.0f;
    float minRpm           = 0.0f;
    float maxRpm           = 0.0f;
    float avgRpm           = 0.0f;
    float ips              = 0.0f; // Linear tape speed in inches/sec (Nominal in play, dynamic in wind)
    float cmps             = 0.0f; // Linear tape speed in cm/sec
    float packRadiusInches = 1.125f; // Estimated current tape pack radius
    float packFullnessPct  = 0.0f;   // Estimated reel fullness % (0% bare hub to 100% full flange)
    float wowFlutterPct    = 0.0f; // Wow & Flutter speed deviation percentage
    float totalTurns       = 0.0f; // Integrated reel turns
    float tapeFeet         = 0.0f; // Estimated tape length in feet
    float tapeMeters       = 0.0f; // Estimated tape length in meters
    bool isClockwise       = true;
    bool isMoving          = false;
    bool isCapstanLocked   = false;
    uint32_t sampleCount   = 0;
};

class R2RCalculator {
public:
    R2RCalculator();

    void update(const ImuData& imuData, float dt);
    void resetStats();

    // Capstan Tape Speed Presets (3.75, 7.5, 15 IPS)
    void setTapeSpeed(TapeSpeed speed);
    TapeSpeed getTapeSpeed() const { return _tape_speed; }
    float getNominalIps() const;
    void nextTapeSpeed() {
        int next = (static_cast<int>(_tape_speed) + 1) % static_cast<int>(TapeSpeed::Count);
        setTapeSpeed(static_cast<TapeSpeed>(next));
    }
    static const char* getTapeSpeedName(TapeSpeed speed);

    // Reel Hub Size Configuration
    void setHubType(ReelHubType type, float customRadiusInches = 1.125f);
    void nextHubType()
    {
        int next = (static_cast<int>(_hub_type) + 1) % 3;
        setHubType(static_cast<ReelHubType>(next));
    }
    ReelHubType getHubType() const { return _hub_type; }
    float getHubRadiusInches() const { return _hub_radius_inches; }
    float getFlangeRadiusInches() const { return _flange_radius_inches; }
    static const char* getHubTypeName(ReelHubType type);

    void setRotationAxis(RotationAxis axis) { _axis = axis; }
    RotationAxis getRotationAxis() const { return _axis; }

    void setFilterAlpha(float alpha) { _ema_alpha = std::clamp(alpha, 0.01f, 1.0f); }

    void setAutoRotationEnabled(bool enable) { _auto_rotation_enabled = enable; }
    bool isAutoRotationEnabled() const { return _auto_rotation_enabled; }

    void setHighSpeedThresholdRpm(float rpm) { _high_speed_threshold_rpm = std::max(10.0f, rpm); }
    float getHighSpeedThresholdRpm() const { return _high_speed_threshold_rpm; }

    void setAngleOffsetDeg(float offset) { _angle_offset_deg = offset; }
    float getAngleOffsetDeg() const { return _angle_offset_deg; }

    // Orientation & High-Speed Wind Telemetry
    float getReelAngleDeg() const { return _reel_angle_deg; }
    float getCounterRotationAngleDeg() const {
        if (!_auto_rotation_enabled) return 0.0f;
        return -_reel_angle_deg + 180.0f;
    }
    bool isHighSpeedWindMode() const { return _stats.currentRpm > _high_speed_threshold_rpm; }

    // Telemetry getters
    const R2RStats& getStats() const { return _stats; }

    // =========================================================================
    // Simulated Tape Playback, DCT FSK Line Records & State Machine
    // =========================================================================
    TapePlayState getPlayState() const { return _play_state; }
    static const char* getPlayStateName(TapePlayState state);

    float getSimulatedTapeSeconds() const { return _simulated_tape_seconds; }
    void setSimulatedTapeSeconds(float sec);
    void resetSimulatedTapeSeconds();

    char getTapeSide() const {
        return (_play_state == TapePlayState::NormalPlayB || _play_state == TapePlayState::FastReverse) ? 'B' : 'A';
    }

    float getRecordIntervalSec() const;
    float getRecordRateHz() const;
    const char* getLatestLineRecord() const { return _latest_line_record; }
    uint32_t getRecordSequence() const { return _record_seq; }

    // Play Qualification (800ms debounce before emitting first line record)
    static constexpr float DEFAULT_PLAY_QUALIFICATION_SEC = 0.800f;
    void setPlayQualificationSec(float sec) { _play_qualification_sec = (sec >= 0.0f) ? sec : 0.0f; }
    float getPlayQualificationSec() const { return _play_qualification_sec; }
    bool isPlayQualified() const { return _is_play_qualified; }

    // Fast Forward / Fast Reverse Time Multiplier Factor
    // Adjust DEFAULT_WIND_TIME_SCALE to multiply tape time progression during FF / FREV
    static constexpr float DEFAULT_WIND_TIME_SCALE = 2.0f; // <-- Multiplier for FF/FREV time increment
    void setWindTimeScale(float scale) { _wind_time_scale = (scale > 0.0f) ? scale : 1.0f; }
    float getWindTimeScale() const { return _wind_time_scale; }

    // =========================================================================
    // Test & Simulation Override API (/test?ips=..&rpm=..&mode=..)
    // =========================================================================
    void setTestMode(TestModeOverride mode, float customRpm = -1.0f);
    bool isTestModeActive() const { return _test_override != TestModeOverride::Disabled; }
    TestModeOverride getTestMode() const { return _test_override; }
    static const char* getTestModeName(TestModeOverride mode);
    float getSimulatedRpm() const { return _simulated_rpm; }

private:
    TapeSpeed _tape_speed           = TapeSpeed::Ips3_75; // Default: 3.75 IPS
    ReelHubType _hub_type           = ReelHubType::Cine7;
    RotationAxis _axis              = RotationAxis::Auto;

    float _hub_radius_inches        = 1.125f; // 7" Cine hub radius
    float _flange_radius_inches     = 3.500f; // 7" Cine outer flange radius
    float _current_pack_radius      = 1.125f;

    float _high_speed_threshold_rpm = 50.0f;
    float _angle_offset_deg         = 0.0f;
    float _ema_alpha                = 0.15f;
    float _smoothed_signed_dps      = 0.0f;
    float _smoothed_dps             = 0.0f;
    float _reel_angle_deg           = 0.0f;
    bool  _auto_rotation_enabled    = true;
    bool  _last_active_clockwise    = false; // Holds last active direction (false = CCW/Side A, true = CW/Side B)
    uint32_t _zero_dps_frames       = 0;     // Counter for fast-stop brake decay

    R2RStats _stats;

    // Simulated Tape Position & FSK Generator State
    TapePlayState _play_state            = TapePlayState::Stopped;
    TapePlayState _last_play_state       = TapePlayState::Stopped;
    float         _simulated_tape_seconds = 0.0f;
    char          _latest_line_record[40] = "### NOCARRIER ###\n";
    float         _record_cadence_timer   = 0.0f;
    float         _no_carrier_timer       = 0.0f;
    uint32_t      _record_seq             = 0;

    // Play Qualification / Debounce state (prevents premature playback on FF/FREV spin-up)
    float         _play_qualification_sec   = DEFAULT_PLAY_QUALIFICATION_SEC;
    float         _play_qualification_timer = 0.0f;
    bool          _is_play_qualified        = false;
    float         _wind_time_scale          = DEFAULT_WIND_TIME_SCALE;

    // Test mode override state
    TestModeOverride _test_override      = TestModeOverride::Disabled;
    float            _simulated_rpm      = 25.0f;

    // Rolling window buffer for Wow & Flutter computation
    static constexpr std::size_t RollingWindowSize = 30;
    float _rpm_history[RollingWindowSize] = {0.0f};
    std::size_t _history_idx = 0;
};

} // namespace r2r
