# R2RWatchArduino

An advanced ESP32 and M5Unified firmware that turns a wearable/round AMOLED display into an intelligent **Reel-to-Reel (R2R) Tape Deck Telemetry Monitor, Tachometer, and Smart Watch**.

Equipped with 6-axis inertial motion tracking (IMU) and hardware acoustic sampling, R2RWatch mounts to or monitors reel-to-reel audio tape decks in real time—tracking spooling speeds, tape pack fullness, wow & flutter, elapsed tape distance, and acoustic sound levels, while serving a responsive web dashboard and CassetteFlow audio streaming interface over Wi-Fi.

---

## Features

### 1. Multi-Mode AMOLED Display
Rendered via an optimized double-buffered 16-bit graphics canvas in ultra-fast internal SRAM, scaled 1.6643x edge-to-edge for round 466x466 AMOLED screens with high-contrast pitch-black `#000000` power saving:

- **Tachometer View**: Instantaneous reel RPM, linear tape speed (IPS and cm/s), rotation direction (CW / CCW), capstan lock status, and active tape speed preset.
- **VU Meter View**: Real-time acoustic sound monitoring with an oscilloscope audio waveform display, RMS level meter (-20 dB to +3 dB), peak-hold indicator, and selectable microphone attenuation divisor (1.0x to 0.01x).
- **Wow & Flutter View**: Rolling-window RMS speed variation analysis, showing speed instability percentage alongside Min/Max/Avg RPM telemetry and deviation spans.
- **Tape Counter View**: Integrated reel turn counter, estimated tape length in feet and meters, and simulated tape timecode (`MM:SS`).
- **Watch Face View**: Digital clock synchronized via UDP NTP, battery percentage gauge, Wi-Fi RSSI signal strength, and 1:1 native / fullscreen scaling toggle.
- **High-Speed Spooling Animation**: Interactive concentric stepping ring animation triggered during high-speed wind (> 50 RPM).

### 2. Tape Dynamics & Physics Engine (`R2RCalculator`)
- **Capstan Tape Speeds**: Standard reel-to-reel speeds:
  - `3.75 IPS` (9.5 cm/s)
  - `7.50 IPS` (19.0 cm/s)
  - `15.0 IPS` (38.1 cm/s)
- **Hub & Flange Geometry**:
  - `7" Cine Hub`: 2.25" Hub / 7.0" Flange
  - `10.5" NAB Hub`: 4.50" Hub / 10.5" Flange
  - `3"-5" Small Hub`: 1.75" Hub / 5.0" Flange
  - Custom hub radius support
- **Tape Pack Physics**: Dynamically calculates tape pack radius and reel fullness percentage (0% bare hub to 100% full flange) based on angular velocity and linear speed.
- **Simulated DCT FSK Line Generator**: Generates FSK telemetry records (`DCT0A_01_aaaaaaaaaa_SSSS_SSSS` / `### NOCARRIER ###`) with 800ms debounce qualification to prevent false triggers during acceleration and deceleration.

### 3. Web Dashboard & Remote Services (`R2RWebServer`)
- **Responsive Telemetry Dashboard (`/`)**: Built-in dark-mode dashboard with real-time gauges, live oscillogram, statistics, and remote control actions.
- **CassetteFlow Player (`/player`)**: Integrated HTML5 web player with `/playing` endpoint for real-time now-playing metadata synchronization.
- **Live Stream Viewer (`/stream` / `/terminal`)**: Web terminal displaying real-time DCT FSK line records.
- **Raw Socket Streaming (`/raw`)**: Persistent HTTP streaming endpoint delivering live line records with sub-millisecond push latency.
- **REST & Test API**:
  - `/api/telemetry` & `/api/status`: Complete JSON snapshot of sensors, physics, audio, battery, and system state.
  - `/api/action`: Remote mode switching, hub selection, tape speed selection, and counter reset.
  - `/api/set_time`: Browser and epoch-based manual RTC synchronization.
  - `/test`: Simulation endpoint to simulate playback, fast forward, rewind, custom RPM, and tape speed overrides.
  - `/info`, `/mp3db`, `/tapedb`, `/play`, `/stop`: CassetteFlow and ESP32LyraT protocol compatibility endpoints.
- **Time & Connectivity**:
  - High-precision direct UDP NTP synchronization with fallback to background SNTP.
  - Automatic hardware RTC synchronization.
  - SoftAP fallback mode (`R2R-Watch-AP` at `192.168.4.1`) when Wi-Fi is unreachable.

---

## Hardware Requirements

- **Processor**: ESP32 / ESP32-S3 (240 MHz dual-core, Internal SRAM + PSRAM).
- **Framework**: [M5Unified](https://github.com/m5stack/M5Unified) hardware abstraction library.
- **Display**: Round AMOLED / LCD display (e.g. 466x466 or 240x240).
- **Sensors & Peripherals**:
  - 6-axis IMU (InvenSense / Bosch Accelerometer & Gyroscope via Fast 400 kHz I2C).
  - Hardware PDM/I2S Microphone.
  - Touchscreen and/or physical buttons (Button A / Button B).
  - Hardware RTC and PMIC battery management.

---

## Project Structure

```
R2RWatchArduino/
├── R2RWatchArduino.ino     # Main Arduino sketch: setup, hardware polling & main loop
├── r2r_calculator.h       # Tape kinematics, hub geometry, FSK line generator header
├── r2r_calculator.cpp     # Physics implementation, Wow & Flutter, EMA filters
├── r2r_renderer.h         # Graphics rendering engine, UI view modes, canvas config
├── r2r_renderer.cpp       # Sprite drawing routines, VU meter, tachometer, watch face
├── r2r_web_server.h       # Asynchronous WebServer, API routes, streaming clients
├── r2r_web_server.cpp     # HTTP endpoint handlers, NTP time sync, SoftAP fallback
├── index.html             # Source HTML/CSS/JS for telemetry dashboard
├── web_ui_html.h          # PROGMEM embedded version of index.html
├── stream.html            # Source HTML/CSS/JS for live FSK terminal
├── stream_ui_html.h       # PROGMEM embedded version of stream.html
├── player_ui_html.h       # PROGMEM embedded version of CassetteFlow web player
├── wifi_config.example.h  # Template for private Wi-Fi SSID, password, and timezone
├── wifi_config.h          # (Git-ignored) Local Wi-Fi credentials
└── .gitignore             # Git ignore rules for private configs and build artifacts
```

---

## Getting Started

### 1. Prerequisites
- [Arduino IDE](https://www.arduino.cc/en/software) (2.0+ recommended) or [PlatformIO](https://platformio.org/).
- ESP32 Board Package installed in Arduino Board Manager.
- Install the following libraries via the Arduino Library Manager:
  - **M5Unified**
  - **M5GFX**

### 2. Wi-Fi Configuration
1. Duplicate `wifi_config.example.h` and rename it to `wifi_config.h`:
   ```bash
   cp wifi_config.example.h wifi_config.h
   ```
2. Open `wifi_config.h` and configure your credentials:
   ```cpp
   #define R2R_WIFI_SSID     "Your_WiFi_Network"
   #define R2R_WIFI_PASSWORD "Your_WiFi_Password"
   #define R2R_TIMEZONE      "EST5EDT,M3.2.0,M11.1.0" // Adjust to your POSIX timezone
   ```
   *(Note: `wifi_config.h` is excluded in `.gitignore` to protect your network credentials).*

### 3. Compilation & Flashing
1. Open `R2RWatchArduino.ino` in Arduino IDE.
2. Select your ESP32 / M5 target board under **Tools > Board**.
3. Set CPU Frequency to **240 MHz**.
4. Set Upload Speed to **921600** or **115200**.
5. Connect your device via USB and click **Upload**.

---

## On-Device Controls

| Control | Action |
| :--- | :--- |
| **Button A** (or Tap Upper Half of Screen) | Cycle Screen View Mode (`Tachometer` → `VU Meter` → `Wow & Flutter` → `Tape Counter` → `Watch Face`) |
| **Button B** (or Tap Lower Half of Screen) | Contextual Action depending on active mode: |
| &nbsp;&nbsp;&nbsp;&nbsp;↳ *In Tachometer Mode* | Cycle Capstan Speed (`3.75 IPS` → `7.50 IPS` → `15.0 IPS`) |
| &nbsp;&nbsp;&nbsp;&nbsp;↳ *In VU Meter Mode* | Cycle Microphone Attenuation Divisor (`1.0x` → `0.5x` → `0.25x` → `0.10x` → `0.01x`) |
| &nbsp;&nbsp;&nbsp;&nbsp;↳ *In Watch Face Mode* | Toggle Display Scaling Mode (`Fullscreen 1.66x` ↔ `Native 1:1`) |
| &nbsp;&nbsp;&nbsp;&nbsp;↳ *In Counter / Wow & Flutter* | Cycle Hub Geometry (`7" Cine` → `10.5" NAB` → `3"-5" Small`) |

---

## Web API Reference

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/` | `GET` | Main Web Telemetry Dashboard |
| `/player` | `GET` | Embedded CassetteFlow Audio Web Player |
| `/stream` | `GET` | Live DCT FSK Stream Viewer Terminal |
| `/raw` | `GET` | Plain-text streaming socket for real-time DCT FSK line records |
| `/api/telemetry` | `GET` | Complete telemetry payload in JSON |
| `/api/status` | `GET` | Alias for `/api/telemetry` |
| `/api/action` | `POST` | Execute remote control actions (`mode`, `hub`, `speed`, `mic_div`, `reset_counter`) |
| `/api/set_time` | `POST` | Synchronize RTC via Unix `epoch` or explicit date/time arguments |
| `/playing` | `GET/POST` | Synchronize current track title / artist metadata with web clients |
| `/test` | `GET/POST` | Test and simulation override (`?ips=3.75&rpm=25&mode=play`) |
| `/info` | `GET` | Compatibility handshake returning latest decoded state |
| `/play` / `/stop` | `GET` | CassetteFlow transport control overrides |

---

## License

This project is licensed under the [MIT License](LICENSE).
Copyright (c) 2026 Nathan / R2RWatch Project.
