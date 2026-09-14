/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#include "r2r_calculator.h"
#include <numeric>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace r2r {

R2RCalculator::R2RCalculator()
{
    setTapeSpeed(TapeSpeed::Ips3_75); // Default: 3.75 IPS
    setHubType(ReelHubType::Cine7);
    resetStats();
}

float R2RCalculator::getNominalIps() const
{
    switch (_tape_speed) {
        case TapeSpeed::Ips3_75: return 3.75f;
        case TapeSpeed::Ips7_5:  return 7.50f;
        case TapeSpeed::Ips15:   return 15.00f;
        default:                 return 3.75f;
    }
}

float R2RCalculator::getRecordIntervalSec() const
{
    switch (_tape_speed) {
        case TapeSpeed::Ips3_75: return 0.500f; // 2 records per sec
        case TapeSpeed::Ips7_5:  return 0.250f; // 4 records per sec
        case TapeSpeed::Ips15:   return 0.125f; // 8 records per sec
        default:                 return 0.500f;
    }
}

float R2RCalculator::getRecordRateHz() const
{
    switch (_tape_speed) {
        case TapeSpeed::Ips3_75: return 2.0f;
        case TapeSpeed::Ips7_5:  return 4.0f;
        case TapeSpeed::Ips15:   return 8.0f;
        default:                 return 2.0f;
    }
}

const char* R2RCalculator::getTapeSpeedName(TapeSpeed speed)
{
    switch (speed) {
        case TapeSpeed::Ips3_75: return "3.75 IPS";
        case TapeSpeed::Ips7_5:  return "7.50 IPS";
        case TapeSpeed::Ips15:   return "15.0 IPS";
        default:                 return "3.75 IPS";
    }
}

void R2RCalculator::setTapeSpeed(TapeSpeed speed)
{
    _tape_speed = speed;
}

const char* R2RCalculator::getHubTypeName(ReelHubType type)
{
    switch (type) {
        case ReelHubType::Nab105: return "10.5\" NAB (4.50\" Hub)";
        case ReelHubType::Cine7:  return "7\" Cine (2.25\" Hub)";
        case ReelHubType::Small3: return "3\"-5\" Small (1.75\" Hub)";
        case ReelHubType::Custom: return "Custom Hub";
        default: return "Unknown";
    }
}

void R2RCalculator::setHubType(ReelHubType type, float customRadiusInches)
{
    _hub_type = type;
    switch (type) {
        case ReelHubType::Nab105:
            _hub_radius_inches    = 2.25f; // Diameter 4.50 inches
            _flange_radius_inches = 5.25f; // Diameter 10.5 inches
            break;
        case ReelHubType::Cine7:
            _hub_radius_inches    = 1.125f; // Diameter 2.25 inches
            _flange_radius_inches = 3.500f; // Diameter 7.0 inches
            break;
        case ReelHubType::Small3:
            _hub_radius_inches    = 0.875f; // Diameter 1.75 inches
            _flange_radius_inches = 2.500f; // Diameter 5.0 inches
            break;
        case ReelHubType::Custom:
            _hub_radius_inches    = customRadiusInches > 0.1f ? customRadiusInches : 1.125f;
            _flange_radius_inches = _hub_radius_inches * 3.1f;
            break;
    }
    _current_pack_radius = _hub_radius_inches;
    _stats.packRadiusInches = _current_pack_radius;
}

const char* R2RCalculator::getPlayStateName(TapePlayState state)
{
    switch (state) {
        case TapePlayState::Stopped:     return "STOPPED";
        case TapePlayState::NormalPlayA: return "PLAY A";
        case TapePlayState::NormalPlayB: return "PLAY B";
        case TapePlayState::FastForward: return "FAST FWD";
        case TapePlayState::FastReverse: return "FAST REV";
        default:                         return "UNKNOWN";
    }
}

const char* R2RCalculator::getTestModeName(TestModeOverride mode)
{
    switch (mode) {
        case TestModeOverride::Disabled:    return "DISABLED";
        case TestModeOverride::Stop:        return "STOP";
        case TestModeOverride::PlayCCW:     return "PLAY_CCW";
        case TestModeOverride::PlayCW:      return "PLAY_CW";
        case TestModeOverride::FastForward: return "FFW";
        case TestModeOverride::FastReverse: return "FREV";
        default:                            return "UNKNOWN";
    }
}

void R2RCalculator::setTestMode(TestModeOverride mode, float customRpm)
{
    _test_override = mode;
    if (customRpm > 0.0f) {
        _simulated_rpm = customRpm;
    } else {
        switch (mode) {
            case TestModeOverride::Stop:
                _simulated_rpm = 0.0f;
                break;
            case TestModeOverride::PlayCCW:
            case TestModeOverride::PlayCW:
                _simulated_rpm = 25.0f;
                break;
            case TestModeOverride::FastForward:
            case TestModeOverride::FastReverse:
                _simulated_rpm = 100.0f;
                break;
            case TestModeOverride::Disabled:
            default:
                break;
        }
    }

    // Immediately update state and output record so responses and /raw reflect test mode instantly
    switch (mode) {
        case TestModeOverride::PlayCCW: {
            _play_state = TapePlayState::NormalPlayA;
            _last_active_clockwise = false;
            _smoothed_signed_dps = _simulated_rpm * 6.0f;
            _smoothed_dps = _simulated_rpm * 6.0f;
            _stats.isMoving = true;
            _stats.isClockwise = false;
            _stats.currentRpm = _simulated_rpm;
            _stats.rawRpm = _simulated_rpm;
            _stats.ips = getNominalIps();
            _is_play_qualified = true;
            _play_qualification_timer = _play_qualification_sec;
            uint32_t sec = static_cast<uint32_t>(_simulated_tape_seconds);
            std::snprintf(_latest_line_record, sizeof(_latest_line_record),
                          "DCT0A_01_aaaaaaaaaa_%04u_%04u\n", sec, sec);
            _record_seq++;
            break;
        }
        case TestModeOverride::PlayCW: {
            _play_state = TapePlayState::NormalPlayB;
            _last_active_clockwise = true;
            _smoothed_signed_dps = -(_simulated_rpm * 6.0f);
            _smoothed_dps = _simulated_rpm * 6.0f;
            _stats.isMoving = true;
            _stats.isClockwise = true;
            _stats.currentRpm = _simulated_rpm;
            _stats.rawRpm = _simulated_rpm;
            _stats.ips = getNominalIps();
            _is_play_qualified = true;
            _play_qualification_timer = _play_qualification_sec;
            uint32_t sec = static_cast<uint32_t>(_simulated_tape_seconds);
            std::snprintf(_latest_line_record, sizeof(_latest_line_record),
                          "DCT0B_01_aaaaaaaaaa_%04u_%04u\n", sec, sec);
            _record_seq++;
            break;
        }
        case TestModeOverride::FastForward: {
            _play_state = TapePlayState::FastForward;
            _last_active_clockwise = false;
            _smoothed_signed_dps = _simulated_rpm * 6.0f;
            _smoothed_dps = _simulated_rpm * 6.0f;
            _stats.isMoving = true;
            _stats.isClockwise = false;
            _stats.currentRpm = _simulated_rpm;
            _stats.rawRpm = _simulated_rpm;
            _is_play_qualified = false;
            _play_qualification_timer = 0.0f;
            std::snprintf(_latest_line_record, sizeof(_latest_line_record), "### NOCARRIER ###\n");
            _record_seq++;
            break;
        }
        case TestModeOverride::FastReverse: {
            _play_state = TapePlayState::FastReverse;
            _last_active_clockwise = true;
            _smoothed_signed_dps = -(_simulated_rpm * 6.0f);
            _smoothed_dps = _simulated_rpm * 6.0f;
            _stats.isMoving = true;
            _stats.isClockwise = true;
            _stats.currentRpm = _simulated_rpm;
            _stats.rawRpm = _simulated_rpm;
            _is_play_qualified = false;
            _play_qualification_timer = 0.0f;
            std::snprintf(_latest_line_record, sizeof(_latest_line_record), "### NOCARRIER ###\n");
            _record_seq++;
            break;
        }
        case TestModeOverride::Stop: {
            _play_state = TapePlayState::Stopped;
            _smoothed_signed_dps = 0.0f;
            _smoothed_dps = 0.0f;
            _stats.isMoving = false;
            _stats.currentRpm = 0.0f;
            _stats.rawRpm = 0.0f;
            _stats.ips = 0.0f;
            _is_play_qualified = false;
            _play_qualification_timer = 0.0f;
            std::snprintf(_latest_line_record, sizeof(_latest_line_record), "### NOCARRIER ###\n");
            _record_seq++;
            break;
        }
        case TestModeOverride::Disabled:
        default:
            break;
    }
    _last_play_state = _play_state;
}

void R2RCalculator::resetStats()
{
    _stats = R2RStats();
    _current_pack_radius = _hub_radius_inches;
    _stats.packRadiusInches = _current_pack_radius;
    _smoothed_signed_dps = 0.0f;
    _smoothed_dps = 0.0f;
    _last_active_clockwise = false;
    _zero_dps_frames = 0;
    _reel_angle_deg = 0.0f;
    for (size_t i = 0; i < RollingWindowSize; i++) {
        _rpm_history[i] = 0.0f;
    }
    _history_idx = 0;

    _simulated_tape_seconds   = 0.0f;
    _play_state               = TapePlayState::Stopped;
    _last_play_state          = TapePlayState::Stopped;
    _record_cadence_timer     = 0.0f;
    _no_carrier_timer         = 0.0f;
    _play_qualification_timer = 0.0f;
    _is_play_qualified        = false;
    std::snprintf(_latest_line_record, sizeof(_latest_line_record), "### NOCARRIER ###\n");
}

void R2RCalculator::setSimulatedTapeSeconds(float sec)
{
    _simulated_tape_seconds = std::max(0.0f, sec);
    _record_cadence_timer   = 0.0f;
    uint32_t s = static_cast<uint32_t>(_simulated_tape_seconds);
    if (_is_play_qualified) {
        if (_play_state == TapePlayState::NormalPlayA) {
            std::snprintf(_latest_line_record, sizeof(_latest_line_record),
                          "DCT0A_01_aaaaaaaaaa_%04u_%04u\n", s, s);
            _record_seq++;
        } else if (_play_state == TapePlayState::NormalPlayB) {
            std::snprintf(_latest_line_record, sizeof(_latest_line_record),
                          "DCT0B_01_aaaaaaaaaa_%04u_%04u\n", s, s);
            _record_seq++;
        }
    }
}

void R2RCalculator::resetSimulatedTapeSeconds()
{
    setSimulatedTapeSeconds(0.0f);
}

void R2RCalculator::update(const ImuData& imuData, float dt)
{
    if (dt <= 0.0f) {
        dt = 0.016f;
    }

    float rawDps = 0.0f;
    bool clockwise = true;
    float absDps = 0.0f;

    // 1. Process Input Source (Live 6-Axis IMU vs Test Simulation Override)
    if (_test_override == TestModeOverride::Disabled) {
        // Determine primary angular velocity from configured rotation axis
        switch (_axis) {
            case RotationAxis::AxisZ:
                rawDps = imuData.gyroZ;
                break;
            case RotationAxis::AxisX:
                rawDps = imuData.gyroX;
                break;
            case RotationAxis::AxisY:
                rawDps = imuData.gyroY;
                break;
            case RotationAxis::Auto:
            default: {
                float absX = std::abs(imuData.gyroX);
                float absY = std::abs(imuData.gyroY);
                float absZ = std::abs(imuData.gyroZ);
                if (absZ >= absX && absZ >= absY) {
                    rawDps = imuData.gyroZ;
                } else if (absX >= absY) {
                    rawDps = imuData.gyroX;
                } else {
                    rawDps = imuData.gyroY;
                }
                break;
            }
        }

        // Apply symmetric zero deadband to prevent stationary sensor noise (3.0 deg/s = 0.5 RPM)
        if (std::abs(rawDps) < 3.0f) {
            rawDps = 0.0f;
        }

        if (rawDps == 0.0f) {
            _zero_dps_frames++;
            // Fast-stop asymmetric brake: rapidly decay when sensor is physically stopped
            _smoothed_signed_dps *= 0.50f;
            if (_zero_dps_frames >= 3 || std::abs(_smoothed_signed_dps) < 1.0f) {
                _smoothed_signed_dps = 0.0f;
            }
        } else {
            _zero_dps_frames = 0;
            // Signed Exponential Moving Average (EMA) filtering during active motion
            _smoothed_signed_dps = (_ema_alpha * rawDps) + ((1.0f - _ema_alpha) * _smoothed_signed_dps);
        }

        absDps = std::abs(rawDps);
        _smoothed_dps = std::abs(_smoothed_signed_dps);

        // Update active direction with directional hysteresis:
        // Positive = CCW (Side A / Forward), Negative = CW (Side B / Reverse).
        // If decelerating towards zero or within deadband, preserve the last active direction.
        if (_smoothed_signed_dps > 3.6f) {
            _last_active_clockwise = false; // Definite CCW / Side A
        } else if (_smoothed_signed_dps < -3.6f) {
            _last_active_clockwise = true;  // Definite CW / Side B
        }
        clockwise = _last_active_clockwise;

        // Compute instantaneous gravity orientation angle from accelerometer
        float accelAngle = std::atan2(imuData.accelY, imuData.accelX) * (180.0f / M_PI);
        while (accelAngle < 0.0f) accelAngle += 360.0f;
        while (accelAngle >= 360.0f) accelAngle -= 360.0f;

        // Continuous smooth angle tracking (shortest path interpolation)
        float diff = accelAngle - _reel_angle_deg;
        while (diff > 180.0f) diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;

        _reel_angle_deg += 0.25f * diff;
        while (_reel_angle_deg >= 360.0f) _reel_angle_deg -= 360.0f;
        while (_reel_angle_deg < 0.0f) _reel_angle_deg += 360.0f;

    } else {
        // Test Simulation Override Active
        switch (_test_override) {
            case TestModeOverride::Stop:
                rawDps    = 0.0f;
                absDps    = 0.0f;
                clockwise = _last_active_clockwise;
                break;
            case TestModeOverride::PlayCCW:
                rawDps    = _simulated_rpm * 6.0f;
                absDps    = rawDps;
                clockwise = false; // CCW
                _last_active_clockwise = false;
                break;
            case TestModeOverride::PlayCW:
                rawDps    = -(_simulated_rpm * 6.0f);
                absDps    = _simulated_rpm * 6.0f;
                clockwise = true; // CW
                _last_active_clockwise = true;
                break;
            case TestModeOverride::FastForward:
                rawDps    = _simulated_rpm * 6.0f;
                absDps    = rawDps;
                clockwise = false; // CCW
                _last_active_clockwise = false;
                break;
            case TestModeOverride::FastReverse:
                rawDps    = -(_simulated_rpm * 6.0f);
                absDps    = _simulated_rpm * 6.0f;
                clockwise = true; // CW
                _last_active_clockwise = true;
                break;
            default:
                break;
        }

        _smoothed_signed_dps = rawDps;
        _smoothed_dps = absDps;

        // Animate angle during test mode
        float angleDelta = (rawDps * dt);
        _reel_angle_deg += angleDelta;
        while (_reel_angle_deg >= 360.0f) _reel_angle_deg -= 360.0f;
        while (_reel_angle_deg < 0.0f)    _reel_angle_deg += 360.0f;
    }

    // Calculate instantaneous and smoothed RPM
    float rawRpm      = absDps / 6.0f;
    float currentRpm  = _smoothed_dps / 6.0f;
    bool isMoving     = (currentRpm > 0.6f);

    _stats.rawRpm      = rawRpm;
    _stats.currentRpm  = currentRpm;
    _stats.isClockwise = clockwise;
    _stats.isMoving    = isMoving;

    // =========================================================================
    // Linear Tape Speed (IPS) & Tape Pack Radius Physics
    // =========================================================================
    float nominalIps = getNominalIps();

    if (!isMoving) {
        // Stopped: 0.0 IPS
        _stats.ips = 0.0f;
        _stats.cmps = 0.0f;
        _stats.isCapstanLocked = false;
        _stats.packRadiusInches = _hub_radius_inches;
        _stats.packFullnessPct = 0.0f;
    }
    else if (!isHighSpeedWindMode()) {
        // PLAY / RECORD MODE (Capstan Controlled):
        // Tape linear speed is fixed by the deck's capstan motor (3.75, 7.5, or 15 IPS).
        _stats.ips = nominalIps;
        _stats.cmps = nominalIps * 2.54f;
        _stats.isCapstanLocked = true;

        // Calculate instantaneous tape pack radius only within valid playback RPM bounds (5 to 50 RPM)
        if (currentRpm >= 5.0f && currentRpm <= 50.0f) {
            float calculatedRadius = (nominalIps * 60.0f) / (2.0f * M_PI * currentRpm);
            calculatedRadius = std::clamp(calculatedRadius, _hub_radius_inches, _flange_radius_inches);
            _current_pack_radius = (0.95f * _current_pack_radius) + (0.05f * calculatedRadius);
        }

        _stats.packRadiusInches = _current_pack_radius;
        float radiusSpan = _flange_radius_inches - _hub_radius_inches;
        if (radiusSpan > 0.1f) {
            _stats.packFullnessPct = std::clamp(((_current_pack_radius - _hub_radius_inches) / radiusSpan) * 100.0f, 0.0f, 100.0f);
        }

        // Integrate tape length at constant capstan velocity
        float feetInStep = (nominalIps * dt) / 12.0f;
        _stats.tapeFeet   += feetInStep;
        _stats.tapeMeters += feetInStep * 0.3048f;
    }
    else {
        // FAST FORWARD / FAST REWIND MODE (Spooling Wind):
        // Capstan is disengaged; linear speed is dynamic = RPM * (2 * PI * R_pack) / 60
        _stats.isCapstanLocked = false;
        float windIps = currentRpm * (2.0f * M_PI * _current_pack_radius) / 60.0f;
        _stats.ips  = windIps;
        _stats.cmps = windIps * 2.54f;

        // Integrate tape length at dynamic spooling velocity
        float feetInStep = (windIps * dt) / 12.0f;
        _stats.tapeFeet   += feetInStep;
        _stats.tapeMeters += feetInStep * 0.3048f;
    }

    // Track turns & session min/max/avg stats if moving
    if (isMoving) {
        float turnsInStep = (_smoothed_dps * dt) / 360.0f;
        _stats.totalTurns += turnsInStep;
        _stats.sampleCount++;

        if (_stats.sampleCount == 1) {
            _stats.minRpm = currentRpm;
            _stats.maxRpm = currentRpm;
            _stats.avgRpm = currentRpm;
        } else {
            _stats.minRpm = std::min(_stats.minRpm, currentRpm);
            _stats.maxRpm = std::max(_stats.maxRpm, currentRpm);
            _stats.avgRpm += (currentRpm - _stats.avgRpm) / static_cast<float>(_stats.sampleCount);
        }

        // Rolling buffer for Wow & Flutter
        _rpm_history[_history_idx] = currentRpm;
        _history_idx = (_history_idx + 1) % RollingWindowSize;

        // Calculate RMS speed variation
        float sum = 0.0f;
        std::size_t validCount = 0;
        for (float val : _rpm_history) {
            if (val > 0.1f) {
                sum += val;
                validCount++;
            }
        }

        if (validCount > 5) {
            float mean = sum / validCount;
            float sqDiffSum = 0.0f;
            for (float val : _rpm_history) {
                if (val > 0.1f) {
                    float diff = val - mean;
                    sqDiffSum += diff * diff;
                }
            }
            float stdDev = std::sqrt(sqDiffSum / validCount);
            _stats.wowFlutterPct = (stdDev / mean) * 100.0f;
        } else {
            _stats.wowFlutterPct = 0.0f;
        }
    } else {
        _stats.wowFlutterPct = 0.0f;
    }

    // =========================================================================
    // Playback State Machine & DCT FSK Line Record Simulation
    // CCW = Side A / Fast Forward, CW = Side B / Fast Reverse
    // =========================================================================
    bool isCCW = !clockwise;

    if (!isMoving) {
        _play_state = TapePlayState::Stopped;
    } else if (isHighSpeedWindMode()) {
        _play_state = isCCW ? TapePlayState::FastForward : TapePlayState::FastReverse;
    } else {
        _play_state = isCCW ? TapePlayState::NormalPlayA : TapePlayState::NormalPlayB;
    }

    // State Transition Trigger
    if (_play_state != _last_play_state) {
        if (_play_state == TapePlayState::Stopped ||
            _play_state == TapePlayState::FastForward ||
            _play_state == TapePlayState::FastReverse) {
            _is_play_qualified = false;
            _play_qualification_timer = 0.0f;
            std::snprintf(_latest_line_record, sizeof(_latest_line_record), "### NOCARRIER ###\n");
            _no_carrier_timer = 0.0f;
            _record_seq++;
        } else {
            // Entering NormalPlayA or NormalPlayB
            if (_last_play_state == TapePlayState::Stopped ||
                _last_play_state == TapePlayState::FastForward ||
                _last_play_state == TapePlayState::FastReverse) {
                _play_qualification_timer = 0.0f;
                _is_play_qualified = (_play_qualification_sec <= 0.0f);
                _record_cadence_timer = 0.0f;
            }
        }
        _last_play_state = _play_state;
    }

    // State Execution & Record Output
    if (_play_state == TapePlayState::Stopped) {
        _no_carrier_timer += dt;
        if (_no_carrier_timer >= 2.0f) {
            std::snprintf(_latest_line_record, sizeof(_latest_line_record), "### NOCARRIER ###\n");
            _no_carrier_timer = 0.0f;
            _record_seq++;
        }
    }
    else if (_play_state == TapePlayState::FastForward) {
        // Fast Forward: advance tape time rapidly based on spooling rate multiplied by _wind_time_scale
        float windSpeed = (_stats.ips > 0.0f) ? _stats.ips : (nominalIps * 5.0f);
        float multiplier = (windSpeed / nominalIps) * _wind_time_scale;
        _simulated_tape_seconds += (dt * multiplier);

        _no_carrier_timer += dt;
        if (_no_carrier_timer >= 2.0f) {
            std::snprintf(_latest_line_record, sizeof(_latest_line_record), "### NOCARRIER ###\n");
            _no_carrier_timer = 0.0f;
            _record_seq++;
        }
    }
    else if (_play_state == TapePlayState::FastReverse) {
        // Fast Reverse: rewind tape time rapidly multiplied by _wind_time_scale, strictly clamped at >= 0.0
        float windSpeed = (_stats.ips > 0.0f) ? _stats.ips : (nominalIps * 5.0f);
        float multiplier = (windSpeed / nominalIps) * _wind_time_scale;
        _simulated_tape_seconds -= (dt * multiplier);
        if (_simulated_tape_seconds < 0.0f) {
            _simulated_tape_seconds = 0.0f;
        }

        _no_carrier_timer += dt;
        if (_no_carrier_timer >= 2.0f) {
            std::snprintf(_latest_line_record, sizeof(_latest_line_record), "### NOCARRIER ###\n");
            _no_carrier_timer = 0.0f;
            _record_seq++;
        }
    }
    else if (_play_state == TapePlayState::NormalPlayA || _play_state == TapePlayState::NormalPlayB) {
        // Normal Play: 1:1 real-time accumulation
        _simulated_tape_seconds += dt;
        char side = (_play_state == TapePlayState::NormalPlayA) ? 'A' : 'B';

        if (!_is_play_qualified) {
            // Delay sending line record by _play_qualification_sec (e.g. 800ms)
            // to ensure the device is not ramping up into FF/FREV or decelerating from FF/FREV
            _play_qualification_timer += dt;
            if (_play_qualification_timer >= _play_qualification_sec) {
                _is_play_qualified = true;
                uint32_t sec = static_cast<uint32_t>(_simulated_tape_seconds);
                std::snprintf(_latest_line_record, sizeof(_latest_line_record),
                              "DCT0%c_01_aaaaaaaaaa_%04u_%04u\n", side, sec, sec);
                _record_cadence_timer = 0.0f;
                _record_seq++;
            }
        } else {
            // Qualified playback: output line records at regular cadence
            _record_cadence_timer += dt;
            float interval = getRecordIntervalSec();
            if (_record_cadence_timer >= interval) {
                uint32_t sec = static_cast<uint32_t>(_simulated_tape_seconds);
                std::snprintf(_latest_line_record, sizeof(_latest_line_record),
                              "DCT0%c_01_aaaaaaaaaa_%04u_%04u\n", side, sec, sec);
                _record_cadence_timer = 0.0f;
                _record_seq++;
            }
        }
    }
}

} // namespace r2r

