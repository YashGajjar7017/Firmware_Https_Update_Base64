#ifndef WEB_GUI_H
#define WEB_GUI_H

const char index_html[] = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Firmware Controller</title>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&family=Fira+Code:wght@400;500&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #0b0f19;
            --card-bg: rgba(17, 24, 39, 0.7);
            --border-color: rgba(255, 255, 255, 0.08);
            --text-primary: #f8fafc;
            --text-secondary: #94a3b8;
            --primary: #6366f1;
            --primary-glow: rgba(99, 102, 241, 0.4);
            --accent: #a855f7;
            --emerald: #10b981;
            --rose: #f43f5e;
            --amber: #f59e0b;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            background-color: var(--bg-color);
            background-image: 
                radial-gradient(at 0% 0%, rgba(99, 102, 241, 0.15) 0px, transparent 50%),
                radial-gradient(at 100% 100%, rgba(168, 85, 247, 0.15) 0px, transparent 50%);
            background-attachment: fixed;
            font-family: 'Outfit', sans-serif;
            color: var(--text-primary);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
            overflow-x: hidden;
        }

        .dashboard {
            width: 100%;
            max-width: 950px;
            background: var(--card-bg);
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            border: 1px solid var(--border-color);
            border-radius: 24px;
            padding: 40px;
            box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.5);
            animation: fadeIn 0.8s cubic-bezier(0.16, 1, 0.3, 1);
        }

        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(20px); }
            to { opacity: 1; transform: translateY(0); }
        }

        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--border-color);
            padding-bottom: 25px;
            margin-bottom: 30px;
        }

        .brand h1 {
            font-size: 26px;
            font-weight: 800;
            background: linear-gradient(to right, #6366f1, #a855f7);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            letter-spacing: -0.5px;
        }

        .brand p {
            font-size: 14px;
            color: var(--text-secondary);
            margin-top: 4px;
        }

        .gprs-badge {
            background: rgba(16, 185, 129, 0.1);
            border: 1px solid rgba(16, 185, 129, 0.2);
            color: var(--emerald);
            padding: 8px 16px;
            border-radius: 100px;
            font-size: 13px;
            font-weight: 600;
            display: flex;
            align-items: center;
            gap: 8px;
            box-shadow: 0 0 15px rgba(16, 185, 129, 0.1);
        }

        .gprs-badge.offline {
            background: rgba(244, 63, 94, 0.1);
            border: 1px solid rgba(244, 63, 94, 0.2);
            color: var(--rose);
            box-shadow: 0 0 15px rgba(244, 63, 94, 0.1);
        }

        .grid {
            display: grid;
            grid-template-columns: 1.2fr 0.8fr;
            gap: 30px;
            margin-bottom: 30px;
        }

        @media (max-width: 768px) {
            .grid {
                grid-template-columns: 1fr;
            }
        }

        .panel {
            background: rgba(255, 255, 255, 0.02);
            border: 1px solid var(--border-color);
            border-radius: 18px;
            padding: 24px;
        }

        .panel-title {
            font-size: 14px;
            font-weight: 600;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 20px;
        }

        /* Progress Card styling */
        .status-container {
            display: flex;
            align-content: center;
            justify-content: space-between;
            margin-bottom: 25px;
        }

        .status-value {
            font-size: 24px;
            font-weight: 800;
            color: var(--text-primary);
        }

        .progress-track {
            background: rgba(255, 255, 255, 0.05);
            height: 14px;
            border-radius: 100px;
            overflow: hidden;
            position: relative;
            margin-bottom: 20px;
        }

        .progress-bar {
            background: linear-gradient(90deg, #6366f1, #a855f7);
            height: 100%;
            width: 0%;
            border-radius: 100px;
            transition: width 0.4s cubic-bezier(0.4, 0, 0.2, 1);
            box-shadow: 0 0 12px var(--primary-glow);
        }

        /* Part indicators */
        .parts-tracker {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 12px;
        }

        .part-node {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            padding: 12px;
            border-radius: 12px;
            text-align: center;
            transition: all 0.3s ease;
        }

        .part-node .part-num {
            font-size: 12px;
            color: var(--text-secondary);
            display: block;
            margin-bottom: 4px;
        }

        .part-node .part-dot {
            width: 8px;
            height: 8px;
            background: rgba(255, 255, 255, 0.1);
            border-radius: 50%;
            display: inline-block;
            transition: all 0.3s ease;
        }

        .part-node.completed {
            background: rgba(16, 185, 129, 0.05);
            border-color: rgba(16, 185, 129, 0.2);
        }

        .part-node.completed .part-dot {
            background: var(--emerald);
            box-shadow: 0 0 10px var(--emerald);
        }

        .part-node.active {
            background: rgba(99, 102, 241, 0.05);
            border-color: rgba(99, 102, 241, 0.3);
            animation: pulse 1.5s infinite;
        }

        .part-node.active .part-dot {
            background: var(--primary);
            box-shadow: 0 0 10px var(--primary);
        }

        @keyframes pulse {
            0% { transform: scale(1); }
            50% { transform: scale(1.03); box-shadow: 0 0 15px rgba(99, 102, 241, 0.15); }
            100% { transform: scale(1); }
        }

        /* Register values list */
        .register-list {
            display: flex;
            flex-direction: column;
            gap: 14px;
        }

        .register-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: rgba(255, 255, 255, 0.01);
            padding: 12px 16px;
            border-radius: 12px;
            border: 1px solid var(--border-color);
        }

        .register-lbl {
            font-size: 13px;
            font-weight: 500;
            color: var(--text-secondary);
        }

        .register-val {
            font-family: 'Fira Code', monospace;
            font-size: 14px;
            font-weight: 600;
        }

        .btn-trigger {
            width: 100%;
            background: linear-gradient(90deg, #6366f1, #a855f7);
            border: none;
            color: white;
            padding: 16px;
            border-radius: 12px;
            font-family: 'Outfit', sans-serif;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            box-shadow: 0 8px 20px -6px rgba(99, 102, 241, 0.5);
            margin-top: 15px;
            display: flex;
            justify-content: center;
            align-items: center;
            gap: 8px;
        }

        .btn-trigger:hover:not(:disabled) {
            transform: translateY(-2px);
            box-shadow: 0 12px 24px -6px rgba(99, 102, 241, 0.7);
        }

        .btn-trigger:active:not(:disabled) {
            transform: translateY(0);
        }

        .btn-trigger:disabled {
            background: rgba(255, 255, 255, 0.08);
            color: var(--text-secondary);
            cursor: not-allowed;
            box-shadow: none;
        }

        /* Console Terminal */
        .console-panel {
            background: #060913;
            border: 1px solid var(--border-color);
            border-radius: 18px;
            padding: 20px;
            box-shadow: inset 0 2px 8px rgba(0, 0, 0, 0.8);
        }

        .console-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid rgba(255, 255, 255, 0.05);
            padding-bottom: 12px;
            margin-bottom: 12px;
        }

        .console-header span {
            font-size: 12px;
            color: var(--text-secondary);
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 1px;
            display: flex;
            align-items: center;
            gap: 6px;
        }

        .console-dot {
            width: 8px;
            height: 8px;
            background: var(--emerald);
            border-radius: 50%;
            display: inline-block;
        }

        .console-btn-clear {
            background: none;
            border: none;
            color: var(--text-secondary);
            font-family: inherit;
            font-size: 11px;
            cursor: pointer;
            transition: color 0.2s ease;
        }

        .console-btn-clear:hover {
            color: var(--text-primary);
        }

        .console-viewport {
            height: 220px;
            overflow-y: auto;
            font-family: 'Fira Code', monospace;
            font-size: 12px;
            line-height: 1.6;
            color: #38bdf8;
            padding-right: 8px;
        }

        .console-viewport::-webkit-scrollbar {
            width: 6px;
        }

        .console-viewport::-webkit-scrollbar-track {
            background: rgba(255, 255, 255, 0.01);
        }

        .console-viewport::-webkit-scrollbar-thumb {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 100px;
        }

        .log-entry {
            margin-bottom: 8px;
            animation: consoleFade 0.2s ease;
        }

        @keyframes consoleFade {
            from { opacity: 0; transform: translateX(-5px); }
            to { opacity: 1; transform: translateX(0); }
        }

        .log-time { color: var(--text-secondary); }
        .log-event { color: #c084fc; font-weight: 500; }
        .log-info { color: #f8fafc; }
        .log-error { color: var(--rose); font-weight: bold; }
        .log-warn { color: var(--amber); }

        .error-diagnostics {
            margin-top: 15px;
            padding: 14px 18px;
            border-radius: 12px;
            background: rgba(244, 63, 94, 0.05);
            border: 1px solid rgba(244, 63, 94, 0.15);
            color: #fda4af;
            font-size: 13px;
            display: none;
            animation: fadeIn 0.4s ease;
        }

        .error-diagnostics.visible {
            display: flex;
            flex-direction: column;
            gap: 4px;
        }

        .error-title {
            font-weight: 700;
            color: var(--rose);
        }
    </style>
</head>
<body>

<div class="dashboard">
    <header>
        <div class="brand">
            <h1>ESP32 OTA FIRMWARE PANEL</h1>
            <p>Cellular base64 chunk-wise streaming updater</p>
        </div>
        <div id="gprsBadge" class="gprs-badge">
            <div class="console-dot" style="background: currentColor;"></div>
            <span id="gprsBadgeTxt">Cellular: Online</span>
        </div>
    </header>

    <div class="grid">
        <!-- Main update panel -->
        <div class="panel">
            <div class="panel-title">Update Status & Telemetry</div>
            
            <div class="status-container">
                <div>
                    <span id="statusTxt" class="status-value">Idle</span>
                </div>
                <div class="status-value" id="progressVal">0%</div>
            </div>

            <div class="progress-track">
                <div id="progressBar" class="progress-bar"></div>
            </div>

            <div style="margin-bottom: 20px;">
                <span style="font-size: 12px; color: var(--text-secondary); display: block; margin-bottom: 10px; font-weight: 600; text-transform: uppercase;">Sequential Chunks Tracker</span>
                <div class="parts-tracker">
                    <div id="part1" class="part-node">
                        <span class="part-num">Part 1</span>
                        <div class="part-dot"></div>
                    </div>
                    <div id="part2" class="part-node">
                        <span class="part-num">Part 2</span>
                        <div class="part-dot"></div>
                    </div>
                    <div id="part3" class="part-node">
                        <span class="part-num">Part 3</span>
                        <div class="part-dot"></div>
                    </div>
                    <div id="part4" class="part-node">
                        <span class="part-num">Part 4</span>
                        <div class="part-dot"></div>
                    </div>
                </div>
            </div>

            <div id="errorBox" class="error-diagnostics">
                <span class="error-title">Update Failed</span>
                <span id="errorDesc">Connection timed out during HTTP handshakes.</span>
            </div>
        </div>

        <!-- Modbus register tracking panel -->
        <div class="panel">
            <div class="panel-title">Modbus Register Bindings</div>
            <div class="register-list">
                <div class="register-item">
                    <span class="register-lbl">Holding Reg 1 (Status)</span>
                    <span id="reg1" class="register-val">0</span>
                </div>
                <div class="register-item">
                    <span class="register-lbl">Holding Reg 2 (Progress)</span>
                    <span id="reg2" class="register-val">0%</span>
                </div>
                <div class="register-item">
                    <span class="register-lbl">Holding Reg 3 (Error Code)</span>
                    <span id="reg3" class="register-val">0</span>
                </div>
                <div class="register-item">
                    <span class="register-lbl">Holding Reg 4 (Current Part)</span>
                    <span id="reg4" class="register-val">0</span>
                </div>
            </div>

            <button id="btnTrigger" class="btn-trigger" onclick="triggerOTA()">
                <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/></svg>
                Trigger OTA Update
            </button>
        </div>
    </div>

    <!-- Live Serial/UART Console Panel -->
    <div class="console-panel">
        <div class="console-header">
            <span><div class="console-dot"></div>UART structured logs</span>
            <button class="console-btn-clear" onclick="clearConsole()">Clear logs</button>
        </div>
        <div id="consoleViewport" class="console-viewport">
            <div class="log-entry"><span class="log-time">[System]</span> <span class="log-info">Dashboard initialized. Connection established with ESP32 REST server.</span></div>
        </div>
    </div>
</div>

<script>
    const STATUS_MAP = {
        0: { name: 'Idle', color: '#94a3b8' },
        1: { name: 'Downloading', color: '#f59e0b' },
        2: { name: 'Decoding Base64', color: '#6366f1' },
        3: { name: 'Flashing Partition', color: '#a855f7' },
        4: { name: 'Flash Complete', color: '#10b981' },
        5: { name: 'Error state', color: '#f43f5e' }
    };

    const ERROR_MAP = {
        0: 'No Error',
        1: 'Cellular GPRS offline / network fail',
        2: 'HTTP/HTTPS connection timeout or empty response',
        3: 'Base64 parsing decode failure / CRC integrity error',
        4: 'Memory allocation error (SPIRAM/PSRAM heap out of memory)',
        5: 'ESP32 partition flash update operations failed'
    };

    let logOffset = 0;
    let isPolling = false;

    function formatTime() {
        const d = new Date();
        return `[${d.toLocaleTimeString()}]`;
    }

    function appendLog(timestamp, event, state, progress, error, part, details, level = 'info') {
        const vp = document.getElementById('consoleViewport');
        const entry = document.createElement('div');
        entry.className = 'log-entry';
        
        let classLevel = 'log-info';
        if (error > 0 || level === 'error') {
            classLevel = 'log-error';
        } else if (level === 'warn') {
            classLevel = 'log-warn';
        }

        entry.innerHTML = `
            <span class="log-time">${timestamp ? '[' + timestamp.split('T')[1] + ']' : formatTime()}</span> 
            <span class="log-event">[${event.toUpperCase()}]</span> 
            <span class="${classLevel}">${details} (State: ${state}, Part: ${part}, Progress: ${progress}%)</span>
        `;
        vp.appendChild(entry);
        vp.scrollTop = vp.scrollHeight;
    }

    function clearConsole() {
        document.getElementById('consoleViewport').innerHTML = '';
    }

    async function pollStatus() {
        if (isPolling) return;
        isPolling = true;
        try {
            const res = await fetch(`/api/status?offset=${logOffset}`);
            if (!res.ok) throw new Error('API server unreachable');
            const data = await res.json();
            
            // 1. Update GPRS online status
            const gprsBadge = document.getElementById('gprsBadge');
            const gprsBadgeTxt = document.getElementById('gprsBadgeTxt');
            if (data.gprs_connected) {
                gprsBadge.className = 'gprs-badge';
                gprsBadgeTxt.innerText = 'Cellular: Online';
            } else {
                gprsBadge.className = 'gprs-badge offline';
                gprsBadgeTxt.innerText = 'Cellular: Offline';
            }

            // 2. Update Status & Progress bar
            const st = STATUS_MAP[data.status] || { name: 'Unknown', color: '#fff' };
            const statusTxtEl = document.getElementById('statusTxt');
            statusTxtEl.innerText = st.name;
            statusTxtEl.style.color = st.color;
            
            document.getElementById('progressVal').innerText = `${data.progress}%`;
            document.getElementById('progressBar').style.width = `${data.progress}%`;

            // 3. Update Modbus labels
            document.getElementById('reg1').innerText = data.status;
            document.getElementById('reg2').innerText = `${data.progress}%`;
            document.getElementById('reg3').innerText = data.error;
            document.getElementById('reg4').innerText = data.part;

            // 4. Update Part node visual status
            for (let p = 1; p <= 4; p++) {
                const node = document.getElementById(`part${p}`);
                node.className = 'part-node';
                if (data.part >= p && data.status > 1) {
                    node.classList.add('completed');
                } else if (data.status === 1 && data.part === p) {
                    node.classList.add('active');
                }
            }

            // 5. Diagnostics
            const errorBox = document.getElementById('errorBox');
            if (data.error > 0) {
                errorBox.className = 'error-diagnostics visible';
                document.getElementById('errorDesc').innerText = ERROR_MAP[data.error] || 'Unknown state';
            } else {
                errorBox.className = 'error-diagnostics';
            }

            // 6. Action button state
            const btn = document.getElementById('btnTrigger');
            if (data.status === 0 || data.status === 4 || data.status === 5) {
                btn.removeAttribute('disabled');
            } else {
                btn.setAttribute('disabled', 'true');
            }

            // 7. Render dynamic logs
            if (data.logs && data.logs.length > 0) {
                data.logs.forEach(log => {
                    appendLog(log.timestamp, log.event, log.state, log.progress, log.error, log.part, log.details);
                });
                logOffset += data.logs.length;
            }

        } catch (e) {
            console.error('Polling failed:', e);
        } finally {
            isPolling = false;
        }
    }

    async function triggerOTA() {
        const btn = document.getElementById('btnTrigger');
        btn.setAttribute('disabled', 'true');
        clearConsole();
        logOffset = 0;
        appendLog(null, 'system', 'triggering', 0, 0, 0, 'User triggered OTA sequence. Requesting REST API...', 'info');
        
        try {
            const res = await fetch('/api/trigger', { method: 'POST' });
            if (!res.ok) throw new Error('API server rejected trigger request');
            appendLog(null, 'system', 'triggered', 0, 0, 0, 'OTA update process successfully spawned in background.', 'info');
        } catch (e) {
            appendLog(null, 'system', 'error', 0, 0, 0, 'Failed to trigger update: ' + e.message, 'error');
            btn.removeAttribute('disabled');
        }
    }

    // Set polling interval
    setInterval(pollStatus, 500);
</script>
</body>
</html>
)rawhtml";

#endif // WEB_GUI_H
