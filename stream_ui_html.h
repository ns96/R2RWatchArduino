/*
 * SPDX-FileCopyrightText: 2026 Nathan / R2RWatch Project
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <pgmspace.h>

static const char STREAM_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>R2R Watch - Live FSK Line Streamer</title>
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600;700;800&family=Outfit:wght@400;600;700;800&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0a0a0c;
            --card-bg: #131317;
            --card-border: #222228;
            --text-primary: #f0f0f5;
            --text-secondary: #82828e;
            --accent-green: #34c759;
            --accent-blue: #0a84ff;
            --accent-amber: #ff9f0a;
            --accent-red: #ff453a;
            --accent-cyan: #30d158;
            --font-mono: 'JetBrains Mono', monospace;
            --font-sans: 'Outfit', sans-serif;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            -webkit-tap-highlight-color: transparent;
        }

        body {
            background-color: var(--bg-color);
            color: var(--text-primary);
            font-family: var(--font-sans);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 0 12px 30px;
        }

        header {
            width: 100%;
            max-width: 960px;
            padding: 16px 10px 12px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--card-border);
            margin-bottom: 14px;
        }

        .brand-container {
            display: flex;
            flex-direction: column;
        }

        .brand-title {
            font-size: 1.15rem;
            font-weight: 800;
            letter-spacing: 1.5px;
            color: var(--text-primary);
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .brand-subtitle {
            font-size: 0.75rem;
            font-family: var(--font-mono);
            color: var(--text-secondary);
        }

        .header-links {
            display: flex;
            gap: 8px;
        }

        .nav-link {
            font-family: var(--font-mono);
            font-size: 0.75rem;
            color: var(--accent-blue);
            text-decoration: none;
            padding: 6px 12px;
            background: #181820;
            border: 1px solid #282834;
            border-radius: 8px;
            transition: all 0.15s ease;
        }

        .nav-link:hover {
            background: #22222e;
            color: #fff;
        }

        main {
            width: 100%;
            max-width: 960px;
            display: flex;
            flex-direction: column;
            gap: 12px;
        }

        /* Status & Telemetry Bar */
        .stats-bar {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(130px, 1fr));
            gap: 8px;
            width: 100%;
        }

        .stat-box {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 10px;
            padding: 10px 12px;
            display: flex;
            flex-direction: column;
        }

        .stat-title {
            font-family: var(--font-mono);
            font-size: 0.65rem;
            color: var(--text-secondary);
            text-transform: uppercase;
        }

        .stat-val {
            font-family: var(--font-mono);
            font-size: 1.05rem;
            font-weight: 700;
            margin-top: 4px;
            color: var(--text-primary);
        }

        /* Terminal Window */
        .terminal-card {
            background: #08080a;
            border: 1px solid var(--card-border);
            border-radius: 14px;
            display: flex;
            flex-direction: column;
            overflow: hidden;
            box-shadow: 0 8px 30px rgba(0, 0, 0, 0.6);
        }

        .terminal-header {
            background: #121216;
            padding: 10px 16px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--card-border);
            flex-wrap: wrap;
            gap: 8px;
        }

        .terminal-title-group {
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .terminal-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: var(--accent-green);
            box-shadow: 0 0 8px var(--accent-green);
            animation: pulse 1.5s infinite;
        }

        @keyframes pulse {
            0%, 100% { opacity: 1; transform: scale(1); }
            50% { opacity: 0.4; transform: scale(0.85); }
        }

        .terminal-title {
            font-family: var(--font-mono);
            font-size: 0.8rem;
            font-weight: 700;
            letter-spacing: 0.5px;
        }

        .terminal-actions {
            display: flex;
            gap: 6px;
        }

        .btn-ctrl {
            background: #1a1a22;
            border: 1px solid #2e2e3c;
            color: var(--text-secondary);
            font-family: var(--font-mono);
            font-size: 0.72rem;
            font-weight: 700;
            padding: 5px 10px;
            border-radius: 6px;
            cursor: pointer;
            transition: all 0.15s ease;
        }

        .btn-ctrl:hover {
            background: #262632;
            color: var(--text-primary);
        }

        .btn-ctrl.active {
            background: #2a2a38;
            color: var(--accent-blue);
            border-color: var(--accent-blue);
        }

        .terminal-body {
            height: 420px;
            overflow-y: auto;
            padding: 14px 16px;
            font-family: var(--font-mono);
            font-size: 0.92rem;
            line-height: 1.6;
            display: flex;
            flex-direction: column;
            gap: 3px;
            scrollbar-width: thin;
            scrollbar-color: #282834 #08080a;
        }

        .log-row {
            display: flex;
            gap: 12px;
            word-break: break-all;
        }

        .log-idx {
            color: #555562;
            user-select: none;
            min-width: 52px;
            text-align: right;
            font-size: 0.8rem;
        }

        .log-time {
            color: #727282;
            user-select: none;
            font-size: 0.8rem;
            min-width: 105px;
        }

        .log-line {
            flex: 1;
        }

        .line-side-a { color: #34c759; font-weight: 600; }
        .line-side-b { color: #0a84ff; font-weight: 600; }
        .line-nocarrier { color: #ff9f0a; }

        /* Quick Controls Card */
        .control-card {
            background: var(--card-bg);
            border: 1px solid var(--card-border);
            border-radius: 14px;
            padding: 14px;
            display: flex;
            flex-direction: column;
            gap: 10px;
        }

        .control-label {
            font-family: var(--font-mono);
            font-size: 0.7rem;
            font-weight: 700;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .button-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(95px, 1fr));
            gap: 8px;
        }

        .btn-action {
            padding: 10px 4px;
            background: #1a1a22;
            border: 1px solid #2a2a38;
            border-radius: 8px;
            color: var(--text-primary);
            font-family: var(--font-mono);
            font-size: 0.76rem;
            font-weight: 700;
            cursor: pointer;
            transition: all 0.15s ease;
            text-align: center;
            white-space: nowrap;
        }

        .btn-action:hover {
            background: #252532;
        }

        .btn-action:active {
            transform: scale(0.98);
        }

        .hub-segmented {
            display: flex;
            background: #0d0d10;
            border: 1px solid #222228;
            border-radius: 8px;
            padding: 3px;
            gap: 4px;
        }

        .hub-option {
            flex: 1;
            padding: 8px 4px;
            background: transparent;
            border: none;
            border-radius: 6px;
            color: var(--text-secondary);
            font-family: var(--font-mono);
            font-size: 0.75rem;
            font-weight: 700;
            cursor: pointer;
            text-align: center;
            transition: all 0.15s ease;
        }

        .hub-option.active {
            background: #242430;
            color: var(--text-primary);
        }
    </style>
</head>
<body>

    <header>
        <div class="brand-container">
            <span class="brand-title">
                <span>R2R WATCH</span>
                <span style="font-size: 0.75rem; background: #22222c; padding: 2px 8px; border-radius: 6px; color: var(--accent-green);">STREAM</span>
            </span>
            <span class="brand-subtitle">PERSISTENT REAL-TIME FSK STREAMER</span>
        </div>
        <div class="header-links">
            <a href="/" class="nav-link">DASHBOARD</a>
            <a href="/raw" target="_blank" class="nav-link">/raw</a>
        </div>
    </header>

    <main>
        <!-- Telemetry Stats Bar -->
        <div class="stats-bar">
            <div class="stat-box">
                <span class="stat-title">PLAY STATE</span>
                <span class="stat-val" id="statPlayState" style="color: var(--accent-green);">STOPPED</span>
            </div>
            <div class="stat-box">
                <span class="stat-title">TAPE TIMECODE</span>
                <span class="stat-val" id="statTapeTime">00:00</span>
            </div>
            <div class="stat-box">
                <span class="stat-title">SPEED & INTERVAL</span>
                <span class="stat-val" id="statCadence">3.75 IPS (500ms)</span>
            </div>
            <div class="stat-box">
                <span class="stat-title">CADENCE RATE</span>
                <span class="stat-val" id="statRateHz">2.0 REC / SEC</span>
            </div>
            <div class="stat-box">
                <span class="stat-title">TOTAL LINES</span>
                <span class="stat-val" id="statTotalLines">0</span>
            </div>
        </div>

                <!-- Now Playing Track Card -->
        <div style="background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 12px; padding: 10px 14px; display: flex; align-items: center; justify-content: space-between; gap: 10px; margin-bottom: 2px;">
            <div style="display: flex; align-items: center; gap: 10px; min-width: 0; flex: 1;">
                <span style="font-size: 1.2rem;">🎵</span>
                <div style="display: flex; flex-direction: column; min-width: 0;">
                    <span style="font-family: var(--font-mono); font-size: 0.65rem; color: var(--text-secondary); font-weight: 700; letter-spacing: 0.5px;">NOW PLAYING</span>
                    <span id="statCurrentTrack" style="font-family: var(--font-mono); font-size: 0.92rem; font-weight: 700; color: var(--accent-gold); white-space: nowrap; overflow: hidden; text-overflow: ellipsis;">No Track Playing</span>
                </div>
            </div>
            <span id="statTrackStatus" style="font-family: var(--font-mono); font-size: 0.7rem; font-weight: 700; color: var(--accent-green); background: rgba(52, 199, 89, 0.12); border: 1px solid rgba(52, 199, 89, 0.3); padding: 3px 8px; border-radius: 6px; white-space: nowrap;">SYNCED</span>
        </div>

        <!-- Terminal Card -->
        <div class="terminal-card">
            <div class="terminal-header">
                <div class="terminal-title-group">
                    <div class="terminal-dot" id="terminalDot"></div>
                    <span class="terminal-title">LIVE LINE FEED</span>
                    <span style="font-family: var(--font-mono); font-size: 0.7rem; color: var(--text-secondary);" id="cadenceTag">[PERSISTENT STREAM]</span>
                </div>
                <div class="terminal-actions">
                    <button class="btn-ctrl active" id="btnAutoScroll" onclick="toggleAutoScroll()">AUTO-SCROLL: ON</button>
                    <button class="btn-ctrl" id="btnPause" onclick="togglePause()">PAUSE</button>
                    <button class="btn-ctrl" onclick="clearTerminal()">CLEAR</button>
                    <button class="btn-ctrl" onclick="copyLog()">COPY</button>
                </div>
            </div>

            <div class="terminal-body" id="terminalLog">
                <div class="log-row" style="color: #666675;">
                    <span class="log-idx">---</span>
                    <span class="log-line">Connecting to /raw persistent stream...</span>
                </div>
            </div>
        </div>

        <!-- Remote Controls Card -->
        <div class="control-card">
            <span class="control-label">TAPE SPEED & CADENCE INTERVAL</span>
            <div class="hub-segmented">
                <button class="hub-option active" id="spdBtn0" onclick="sendSpeed(3.75)">3.75 IPS &bull; 500ms (2 rec/s)</button>
                <button class="hub-option" id="spdBtn1" onclick="sendSpeed(7.5)">7.50 IPS &bull; 250ms (4 rec/s)</button>
                <button class="hub-option" id="spdBtn2" onclick="sendSpeed(15.0)">15.0 IPS &bull; 125ms (8 rec/s)</button>
            </div>

            <span class="control-label" style="margin-top: 4px;">SIMULATION ACTIONS</span>
            <div class="button-grid">
                <button class="btn-action" onclick="sendMode('stop')">STOP</button>
                <button class="btn-action" style="color: var(--accent-green);" onclick="sendMode('play')">PLAY (SIDE A)</button>
                <button class="btn-action" style="color: var(--accent-blue);" onclick="sendMode('cw')">PLAY (SIDE B)</button>
                <button class="btn-action" onclick="sendMode('ffw')">FAST FWD</button>
                <button class="btn-action" onclick="sendMode('frev')">FAST REV</button>
                <button class="btn-action" style="color: var(--accent-red);" onclick="resetTapeTime()">RESET TIME</button>
                <button class="btn-action" style="color: var(--accent-amber);" onclick="sendMode('live')">LIVE IMU</button>
            </div>
        </div>
    </main>

    <script>
        let autoScroll = true;
        let isPaused = false;
        let lineCount = 0;
        let currentIps = 3.75;
        let pollIntervalMs = 500;

        const logContainer = document.getElementById("terminalLog");

        function appendLine(lineText) {
            lineText = lineText.trim();
            if (!lineText) return;

            lineCount++;
            document.getElementById("statTotalLines").textContent = lineCount;

            const row = document.createElement("div");
            row.className = "log-row";

            let colorClass = "line-side-a";
            if (lineText.includes("DCT0B")) {
                colorClass = "line-side-b";
            } else if (lineText.includes("NOCARRIER")) {
                colorClass = "line-nocarrier";
            }

            const now = new Date();
            const timeStr = String(now.getHours()).padStart(2, '0') + ":" +
                            String(now.getMinutes()).padStart(2, '0') + ":" +
                            String(now.getSeconds()).padStart(2, '0') + "." +
                            String(Math.floor(now.getMilliseconds() / 100));

            row.innerHTML = `
                <span class="log-idx">#${lineCount}</span>
                <span class="log-time">[${timeStr}]</span>
                <span class="log-line ${colorClass}">${lineText}</span>
            `;

            logContainer.appendChild(row);

            // Limit buffer size to 600 lines
            if (logContainer.children.length > 600) {
                logContainer.removeChild(logContainer.firstChild);
            }

            if (autoScroll) {
                logContainer.scrollTop = logContainer.scrollHeight;
            }
        }

        async function startStreaming() {
            if (isPaused) {
                setTimeout(startStreaming, 500);
                return;
            }

            try {
                const response = await fetch("/raw");
                if (!response.ok || !response.body) throw new Error("Stream unreachable");
                const reader = response.body.getReader();
                const decoder = new TextDecoder();
                let buffer = "";

                while (true) {
                    if (isPaused) {
                        reader.cancel();
                        break;
                    }
                    const { done, value } = await reader.read();
                    if (done) break;
                    buffer += decoder.decode(value, { stream: true });
                    const lines = buffer.split("\n");
                    buffer = lines.pop(); // keep trailing incomplete chunk in buffer
                    for (const l of lines) {
                        const trimmed = l.trim();
                        if (trimmed) {
                            appendLine(trimmed);
                            updateStatsFromLine(trimmed);
                        }
                    }
                }
            } catch (err) {
                // If offline or disconnected, reconnect
            }

            setTimeout(startStreaming, 1000);
        }

        function updateStatsFromLine(line) {
            if (line.includes("NOCARRIER")) {
                document.getElementById("statPlayState").textContent = "NO CARRIER";
                document.getElementById("statPlayState").style.color = "var(--accent-amber)";
            } else if (line.startsWith("DCT0A")) {
                document.getElementById("statPlayState").textContent = "PLAY A (CCW)";
                document.getElementById("statPlayState").style.color = "var(--accent-green)";
                const parts = line.split("_");
                if (parts.length >= 4) {
                    const sec = parseInt(parts[3], 10);
                    if (!isNaN(sec)) {
                        const m = Math.floor(sec / 60);
                        const s = sec % 60;
                        document.getElementById("statTapeTime").textContent =
                            String(m).padStart(2, '0') + ":" + String(s).padStart(2, '0');
                    }
                }
            } else if (line.startsWith("DCT0B")) {
                document.getElementById("statPlayState").textContent = "PLAY B (CW)";
                document.getElementById("statPlayState").style.color = "var(--accent-blue)";
                const parts = line.split("_");
                if (parts.length >= 4) {
                    const sec = parseInt(parts[3], 10);
                    if (!isNaN(sec)) {
                        const m = Math.floor(sec / 60);
                        const s = sec % 60;
                        document.getElementById("statTapeTime").textContent =
                            String(m).padStart(2, '0') + ":" + String(s).padStart(2, '0');
                    }
                }
            }
        }

        function setCadence(ips) {
            currentIps = ips;
            if (ips >= 14.0) {
                pollIntervalMs = 125; // 15.0 IPS -> 8 rec/sec
            } else if (ips >= 7.0) {
                pollIntervalMs = 250; // 7.50 IPS -> 4 rec/sec
            } else {
                pollIntervalMs = 500; // 3.75 IPS -> 2 rec/sec
            }

            const recPerSec = (1000 / pollIntervalMs).toFixed(0);
            document.getElementById("spdBtn0").classList.toggle("active", ips === 3.75);
            document.getElementById("spdBtn1").classList.toggle("active", ips === 7.5);
            document.getElementById("spdBtn2").classList.toggle("active", ips === 15.0);

            document.getElementById("statCadence").textContent = `${ips.toFixed(2)} IPS (${pollIntervalMs}ms)`;
            document.getElementById("statRateHz").textContent = `${recPerSec} REC / SEC`;
        }

        function sendSpeed(ips) {
            setCadence(ips);
            fetch(`/test?ips=${ips}`).catch(() => {});
        }

        function sendMode(mode) {
            fetch(`/test?mode=${mode}`).catch(() => {});
        }

        function resetTapeTime() {
            fetch('/api/action?reset_tape_time=1', { method: 'POST' }).catch(() => {});
            fetch('/test?mode=reset').catch(() => {});
            document.getElementById("statTapeTime").textContent = "00:00";
        }

        function toggleAutoScroll() {
            autoScroll = !autoScroll;
            const btn = document.getElementById("btnAutoScroll");
            btn.textContent = "AUTO-SCROLL: " + (autoScroll ? "ON" : "OFF");
            btn.classList.toggle("active", autoScroll);
        }

        function togglePause() {
            isPaused = !isPaused;
            const btn = document.getElementById("btnPause");
            btn.textContent = isPaused ? "RESUME" : "PAUSE";
            btn.classList.toggle("active", isPaused);
            document.getElementById("terminalDot").style.background = isPaused ? "#ff9f0a" : "#34c759";
        }

        function clearTerminal() {
            logContainer.innerHTML = "";
            lineCount = 0;
            document.getElementById("statTotalLines").textContent = "0";
        }

        function copyLog() {
            let fullText = "";
            const rows = logContainer.querySelectorAll(".log-row");
            rows.forEach(r => {
                const line = r.querySelector(".log-line");
                if (line) fullText += line.textContent + "\n";
            });
            navigator.clipboard.writeText(fullText).then(() => {
                alert("Copied " + rows.length + " lines to clipboard!");
            });
        }

        // Periodically sync IPS cadence & Now Playing track from device telemetry (every 1 second)
        async function syncDeviceTelemetry() {
            try {
                const res = await fetch("/api/telemetry", { cache: "no-store" });
                if (res.ok) {
                    const data = await res.json();
                    if (data.nominalIps && data.nominalIps !== currentIps) {
                        setCadence(data.nominalIps);
                    }
                    if (data.track) {
                        const trackEl = document.getElementById("statCurrentTrack");
                        const statusEl = document.getElementById("statTrackStatus");
                        if (trackEl) {
                            trackEl.textContent = data.track;
                        }
                        if (statusEl) {
                            const isStopped = !data.track || data.track === "Stopped" || data.track === "No Track Playing";
                            statusEl.textContent = isStopped ? "STOPPED" : "SYNCED";
                            statusEl.style.color = isStopped ? "var(--accent-amber)" : "var(--accent-green)";
                            statusEl.style.background = isStopped ? "rgba(255, 159, 10, 0.12)" : "rgba(52, 199, 89, 0.12)";
                        }
                    }
                }
            } catch (e) {}
            setTimeout(syncDeviceTelemetry, 1000);
        }

        // Initial setup
        setCadence(3.75);
        startStreaming();
        syncDeviceTelemetry();
    </script>
</body>
</html>)rawliteral";
