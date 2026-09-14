/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 *
 * INSTRUCTIONS:
 * 1. Copy this file to "wifi_config.h"
 * 2. Set your local Wi-Fi SSID and Password below.
 * 3. "wifi_config.h" is ignored by Git, keeping your credentials private.
 */
#pragma once

// Set your local Wi-Fi Network Name & Password here
#define R2R_WIFI_SSID     "YOUR_WIFI_SSID"
#define R2R_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Access Point (AP) Fallback Configuration (if Wi-Fi fails to connect)
#define R2R_AP_SSID       "R2R-Watch-AP"
#define R2R_AP_PASSWORD   "" // Set empty "" for open network or 8+ chars for WPA2
#define R2R_AP_IP         192, 168, 4, 1

// Connection timeout before falling back to SoftAP mode (in milliseconds)
#define R2R_WIFI_TIMEOUT_MS 8000

// NTP Time Synchronization Configuration
#define R2R_NTP_SERVER_1  "pool.ntp.org"
#define R2R_NTP_SERVER_2  "time.nist.gov"

// POSIX Timezone String (Default: US Eastern Time with Daylight Saving Time)
// Examples:
// US Eastern:  "EST5EDT,M3.2.0,M11.1.0"
// US Central:  "CST6CDT,M3.2.0,M11.1.0"
// US Mountain: "MST7MDT,M3.2.0,M11.1.0"
// US Pacific:  "PST8PDT,M3.2.0,M11.1.0"
// UTC:         "UTC0"
// London:      "GMT0BST,M3.5.0/1,M10.5.0"
// Europe/Paris:"CET-1CEST,M3.5.0,M10.5.0/3"
// Tokyo:       "JST-9"
#define R2R_TIMEZONE      "EST5EDT,M3.2.0,M11.1.0"
