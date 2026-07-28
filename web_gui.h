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
            max-width: 1400px;
            background: var(--card-bg);
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            border: 1px solid var(--border-color);
            border-radius: 24px;
            padding: 30px;
            box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.5);
            animation: fadeIn 0.8s cubic-bezier(0.16, 1, 0.3, 1);
        }

        /* Stacked outer layout: panels top | console bottom */
        .app-layout {
            display: flex;
            flex-direction: column;
            gap: 22px;
        }

        .panels-area {
            width: 100%;
        }

        /* Bottom console sidebar */
        .console-sidebar {
            width: 100%;
            display: flex;
            flex-direction: column;
            margin-top: 10px;
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
            padding-bottom: 20px;
            margin-bottom: 25px;
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
            grid-template-columns: 1.15fr 0.9fr 0.95fr;
            gap: 25px;
            margin-bottom: 25px;
        }

        @media (max-width: 1024px) {
            .grid {
                grid-template-columns: 1.2fr 0.8fr;
            }
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
            margin-bottom: 24px;
        }

        .panel:last-child {
            margin-bottom: 0;
        }

        .panel-title {
            font-size: 14px;
            font-weight: 600;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 20px;
        }

        /* Input styling */
        .input-group {
            display: flex;
            flex-direction: column;
            gap: 8px;
            margin-bottom: 16px;
        }

        .input-group label {
            font-size: 12px;
            font-weight: 600;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }

        .text-input {
            background: rgba(0, 0, 0, 0.3);
            border: 1px solid var(--border-color);
            border-radius: 12px;
            padding: 12px 16px;
            color: var(--text-primary);
            font-family: inherit;
            font-size: 14px;
            outline: none;
            transition: all 0.3s ease;
        }

        .text-input:focus {
            border-color: var(--primary);
            box-shadow: 0 0 0 3px var(--primary-glow);
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
            grid-template-columns: repeat(6, 1fr);
            gap: 12px;
        }

        .part-node.special-node {
            background: rgba(168, 85, 247, 0.03);
            border-color: rgba(168, 85, 247, 0.15);
        }

        .part-node.special-node.completed {
            background: rgba(16, 185, 129, 0.08);
            border-color: rgba(16, 185, 129, 0.3);
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

        /* Testing and diagnostic grid */
        .diag-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 12px;
        }

        .btn-action {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            color: var(--text-primary);
            padding: 12px;
            border-radius: 12px;
            font-family: 'Outfit', sans-serif;
            font-size: 13px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s ease;
            display: flex;
            justify-content: center;
            align-items: center;
            gap: 8px;
        }

        .btn-action:hover:not(:disabled) {
            background: rgba(255, 255, 255, 0.08);
            border-color: rgba(255, 255, 255, 0.15);
            transform: translateY(-1px);
        }

        .btn-action:active:not(:disabled) {
            transform: translateY(0);
        }

        .btn-action:disabled {
            background: rgba(255, 255, 255, 0.01);
            border-color: rgba(255, 255, 255, 0.02);
            color: rgba(255, 255, 255, 0.2);
            cursor: not-allowed;
        }

        .btn-action-danger {
            color: var(--rose);
            background: rgba(244, 63, 94, 0.05);
            border-color: rgba(244, 63, 94, 0.15);
        }

        .btn-action-danger:hover:not(:disabled) {
            background: rgba(244, 63, 94, 0.15);
            border-color: rgba(244, 63, 94, 0.25);
        }

        .btn-action-accent {
            color: var(--accent);
            background: rgba(168, 85, 247, 0.05);
            border-color: rgba(168, 85, 247, 0.15);
        }

        .btn-action-accent:hover:not(:disabled) {
            background: rgba(168, 85, 247, 0.15);
            border-color: rgba(168, 85, 247, 0.25);
        }

        .btn-trigger {
            width: 100%;
            background: linear-gradient(90deg, #6366f1, #a855f7);
            border: none;
            color: white;
            padding: 14px;
            border-radius: 12px;
            font-family: 'Outfit', sans-serif;
            font-size: 15px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            box-shadow: 0 8px 20px -6px rgba(99, 102, 241, 0.5);
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

        /* Status override injectors */
        .injector-panel {
            background: rgba(255, 255, 255, 0.01);
            border: 1px solid var(--border-color);
            border-radius: 14px;
            padding: 16px;
            margin-top: 15px;
        }

        .injector-lbl {
            font-size: 11px;
            font-weight: 700;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 0.5px;
            margin-bottom: 12px;
        }

        .injector-row {
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
        }

        .btn-inj {
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--border-color);
            color: var(--text-secondary);
            padding: 6px 12px;
            border-radius: 8px;
            font-family: inherit;
            font-size: 11px;
            font-weight: 500;
            cursor: pointer;
            transition: all 0.2s ease;
        }

        .btn-inj:hover {
            color: var(--text-primary);
            background: rgba(255, 255, 255, 0.08);
        }

        .btn-inj.active {
            background: var(--primary);
            border-color: var(--primary);
            color: white;
            box-shadow: 0 0 10px var(--primary-glow);
        }

        .console-panel {
            background: #060913;
            border: 1px solid var(--border-color);
            border-radius: 18px;
            padding: 20px;
            box-shadow: inset 0 2px 8px rgba(0, 0, 0, 0.8);
            display: flex;
            flex-direction: column;
            height: 100%;
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
            flex: 1;
            min-height: 300px;
            max-height: calc(100vh - 250px);
            overflow-y: auto;
            font-family: 'Fira Code', monospace;
            font-size: 11.5px;
            line-height: 1.7;
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

    <!-- Two-column layout: all panels on the left, tall console sidebar on the right -->
    <div class="app-layout">
        <div class="panels-area">
            <div class="grid">
                <!-- Column 1: Update Control -->
                <div>
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

                <div style="margin-bottom: 0px;">
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
                        <div id="partAll" class="part-node special-node">
                            <span class="part-num">All Chunks</span>
                            <div class="part-dot"></div>
                        </div>
                        <div id="partFlash" class="part-node special-node">
                            <span class="part-num">Flashed</span>
                            <div class="part-dot"></div>
                        </div>
                    </div>
                </div>

                <div id="errorBox" class="error-diagnostics">
                    <span class="error-title">Update Failed</span>
                    <span id="errorDesc">Connection timed out during HTTP handshakes.</span>
                </div>
            </div>

            <div class="panel">
                <div class="panel-title">Firmware Configuration</div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 16px;">
                    <div class="input-group" style="margin-bottom: 0;">
                        <label for="fwUrlInput1">Part 1 URL</label>
                        <input type="text" id="fwUrlInput1" class="text-input" value="http://64.251.10.159/otafw_part1.b64">
                    </div>
                    <div class="input-group" style="margin-bottom: 0;">
                        <label for="fwUrlInput2">Part 2 URL</label>
                        <input type="text" id="fwUrlInput2" class="text-input" value="http://64.251.10.159/otafw_part2.b64">
                    </div>
                    <div class="input-group" style="margin-bottom: 0;">
                        <label for="fwUrlInput3">Part 3 URL</label>
                        <input type="text" id="fwUrlInput3" class="text-input" value="http://64.251.10.159/otafw_part3.b64">
                    </div>
                    <div class="input-group" style="margin-bottom: 0;">
                        <label for="fwUrlInput4">Part 4 URL</label>
                        <input type="text" id="fwUrlInput4" class="text-input" value="http://64.251.10.159/otafw_part4.b64">
                    </div>
                </div>
                <button id="btnTrigger" class="btn-trigger" onclick="triggerOTA()">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/></svg>
                    Trigger OTA Update
                </button>
            </div>
        </div>

        <!-- Column 2: Telemetry & Overrides -->
                <div>


            <div class="panel">
                <div class="panel-title">Diagnostics & Test Actions</div>
                <div class="diag-grid">
                    <button class="btn-action" onclick="testGprs()">
                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M5 12.5a5 5 0 0 1 7-7 5 5 0 0 1 7 7M2 17a10 10 0 0 1 20 0M12 20h.01"/></svg>
                        Test GPRS
                    </button>
                    <button class="btn-action" onclick="pingServer()">
                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M21 12V7a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h7M16 16l2 2 4-4"/></svg>
                        Ping Server
                    </button>
                    <button class="btn-action" onclick="clearCache()">
                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M3 6h18M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M10 11v6M14 11v6"/></svg>
                        Clear UFS
                    </button>
                    <button class="btn-action btn-action-accent" onclick="clearErrorAck()">
                        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14M22 4L12 14.01l-3-3"/></svg>
                        ACK Errors
                    </button>
                </div>
                <button class="btn-action btn-action-danger" style="width:100%; margin-top:12px;" onclick="rebootDevice()">
                    <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M18.36 6.64a9 9 0 1 1-12.73 0M12 2v10"/></svg>
                    Reboot ESP32 Target Device
                </button>

                <!-- Modbus Manual Override Simulator -->
                <div class="injector-panel">
                    <div class="injector-lbl">Modbus Live Register Simulator</div>
                    <div style="font-size: 11px; color: var(--text-secondary); margin-bottom: 8px;">Force Status register values to check layouts:</div>
                    <div class="injector-row">
                        <button class="btn-inj" onclick="simulateStatus(0)">Idle</button>
                        <button class="btn-inj" onclick="simulateStatus(1)">Downloading</button>
                        <button class="btn-inj" onclick="simulateStatus(2)">Decoding</button>
                        <button class="btn-inj" onclick="simulateStatus(3)">Flashing</button>
                        <button class="btn-inj" onclick="simulateStatus(4)">Complete</button>
                        <button class="btn-inj" onclick="simulateStatus(5)">Error State</button>
                    </div>
                </div>
            </div>
        </div>

        <!-- Column 3: Storage details & Modem UFS files -->
                <div>
            <div class="panel">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px;">
                    <div class="panel-title" style="margin-bottom: 0;">Modem Storage (UFS)</div>
                    <button class="console-btn-clear" onclick="loadModemFiles()" style="font-weight: 600; text-transform: uppercase; display: flex; align-items: center; gap: 4px;">
                        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/></svg>
                        Sync
                    </button>
                </div>
                <div style="overflow-x: auto; max-height: 230px; overflow-y: auto;">
                    <table style="width: 100%; border-collapse: collapse; text-align: left; font-size: 13px;">
                        <thead>
                            <tr style="border-bottom: 1px solid var(--border-color); color: var(--text-secondary);">
                                <th style="padding: 8px 4px; font-weight: 600;">File Path</th>
                                <th style="padding: 8px 4px; font-weight: 600; text-align: right;">Size (Bytes)</th>
                            </tr>
                        </thead>
                        <tbody id="modemFilesBody">
                            <tr>
                                <td colspan="2" style="padding: 12px 4px; color: var(--text-secondary); text-align: center;">No files synced.</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
            </div>

            <div class="panel">
                <div class="panel-title" style="margin-bottom: 20px;">PSRAM Buffer Storage</div>
                <div style="overflow-x: auto; max-height: 180px; overflow-y: auto;">
                    <table style="width: 100%; border-collapse: collapse; text-align: left; font-size: 13px;">
                        <thead>
                            <tr style="border-bottom: 1px solid var(--border-color); color: var(--text-secondary);">
                                <th style="padding: 8px 4px; font-weight: 600;">Buffered Part</th>
                                <th style="padding: 8px 4px; font-weight: 600; text-align: right;">Size (Bytes)</th>
                            </tr>
                        </thead>
                        <tbody id="psramFilesBody">
                            <tr>
                                <td colspan="2" style="padding: 12px 4px; color: var(--text-secondary); text-align: center;">No parts buffered in PSRAM.</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
            </div>

            <div class="panel">
                <div class="panel-title">ESP32 System Storage</div>
                
                <div style="margin-bottom: 16px;">
                    <div style="display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 6px;">
                        <span style="font-weight: 600; color: var(--text-secondary);">FLASH MEMORY</span>
                        <span id="flashStatVal" style="font-family: 'Fira Code'; font-weight: 600;">4.00 MB / 4.00 MB</span>
                    </div>
                    <div class="progress-track" style="height: 8px; margin-bottom: 0;">
                        <div id="flashBar" class="progress-bar" style="width: 100%; background: linear-gradient(90deg, #10b981, #3b82f6);"></div>
                    </div>
                </div>

                <div style="margin-bottom: 16px;">
                    <div style="display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 6px;">
                        <span style="font-weight: 600; color: var(--text-secondary);">FREE HEAP SPACE</span>
                        <span id="heapStatVal" style="font-family: 'Fira Code'; font-weight: 600;">285.4 KB</span>
                    </div>
                    <div class="progress-track" style="height: 8px; margin-bottom: 0;">
                        <div id="heapBar" class="progress-bar" style="width: 75%; background: linear-gradient(90deg, #a855f7, #6366f1);"></div>
                    </div>
                </div>

                <div>
                    <div style="display: flex; justify-content: space-between; font-size: 12px; margin-bottom: 6px;">
                        <span style="font-weight: 600; color: var(--text-secondary);">PSRAM TELEMETRY</span>
                        <span id="psramStatVal" style="font-family: 'Fira Code'; font-weight: 600;">4.00 MB / 4.00 MB</span>
                    </div>
                    <div class="progress-track" style="height: 8px; margin-bottom: 0;">
                        <div id="psramBar" class="progress-bar" style="width: 100%; background: linear-gradient(90deg, #f59e0b, #e11d48);"></div>
                    </div>
                </div>
            </div>
                </div>
            </div><!-- end .grid -->
        </div><!-- end .panels-area -->

        <!-- RIGHT SIDEBAR: UART Structured Log Terminal -->
        <div class="console-sidebar">
            <div class="console-panel">
                <div class="console-header">
                    <span><div class="console-dot"></div>UART Structured Logs</span>
                    <button class="console-btn-clear" onclick="clearConsole()">Clear logs</button>
                </div>
                <div id="consoleViewport" class="console-viewport">
                    <div class="log-entry"><span class="log-time">[System]</span> <span class="log-info">Dashboard initialized. Connection established with ESP32 REST server.</span></div>
                </div>
            </div>
        </div><!-- end .console-sidebar -->
    </div><!-- end .app-layout -->
</div><!-- end .dashboard -->

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
        5: 'ESP32 partition flash update operations failed',
        6: 'Chunk download base64 decoding error',
        7: 'OTA begin initialization failed on partition',
        10: 'OTA end finalization write failed',
        11: 'HTTP Get part transaction error status code'
    };

    let logOffset = 0;
    let isPolling = false;
    let pollCount = 0;

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

            // 4. Update Part node visual status (4 parts)
            for (let p = 1; p <= 4; p++) {
                const node = document.getElementById(`part${p}`);
                if (node) {
                    node.className = 'part-node';
                    if (data.status !== 5) { // not in error state
                        if (data.part > p) {
                            node.classList.add('completed');
                        } else if (data.part === p) {
                            if (data.status === 1) { // downloading
                                node.classList.add('active');
                            } else if (data.status === 2 || data.status === 3 || data.status === 4) { // decoding / flashing / complete
                                node.classList.add('completed');
                            }
                        }
                    }
                }
            }

            // Update All Chunks node (partAll)
            const nodeAll = document.getElementById('partAll');
            if (nodeAll) {
                nodeAll.className = 'part-node special-node';
                if (data.status !== 5) {
                    if (data.status === 3 || data.status === 4) {
                        nodeAll.classList.add('completed');
                    } else if (data.status === 2 && data.part === 4) {
                        nodeAll.classList.add('active');
                    }
                }
            }

            // Update Flashed node (partFlash)
            const nodeFlash = document.getElementById('partFlash');
            if (nodeFlash) {
                nodeFlash.className = 'part-node special-node';
                if (data.status === 4) {
                    nodeFlash.classList.add('completed');
                } else if (data.status === 3) {
                    nodeFlash.classList.add('active');
                }
            }

            // 5. Diagnostics
            const errorBox = document.getElementById('errorBox');
            if (data.error > 0) {
                errorBox.className = 'error-diagnostics visible';
                document.getElementById('errorDesc').innerText = ERROR_MAP[data.error] || `Error code ${data.error}`;
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

            // Highlight simulator buttons active state
            const buttons = document.querySelectorAll('.btn-inj');
            buttons.forEach((b, idx) => {
                if (idx === data.status) {
                    b.classList.add('active');
                } else {
                    b.classList.remove('active');
                }
            });

            // 7. Render dynamic logs
            if (data.logs && data.logs.length > 0) {
                data.logs.forEach(log => {
                    appendLog(log.timestamp, log.event, log.state, log.progress, log.error, log.part, log.details);
                });
                logOffset += data.logs.length;
            }

            // Update PSRAM Buffer Storage files list
            const psramTbody = document.getElementById('psramFilesBody');
            if (psramTbody) {
                if (data.psram_files && data.psram_files.length > 0) {
                    psramTbody.innerHTML = '';
                    data.psram_files.forEach(f => {
                        const tr = document.createElement('tr');
                        tr.style.borderBottom = '1px solid rgba(255,255,255,0.04)';
                        tr.innerHTML = `
                            <td style="padding: 10px 4px; font-family:'Fira Code', monospace; color:#f59e0b;">${f.name}</td>
                            <td style="padding: 10px 4px; text-align: right; font-family:'Fira Code', monospace; color:var(--text-primary); font-weight:600;">${f.size.toLocaleString()}</td>
                        `;
                        psramTbody.appendChild(tr);
                    });
                } else {
                    psramTbody.innerHTML = `
                        <tr>
                            <td colspan="2" style="padding: 12px 4px; color: var(--text-secondary); text-align: center;">No parts buffered in PSRAM.</td>
                        </tr>
                    `;
                }
            }

            // 8. Poll storage parameters periodically
            pollCount++;
            if (pollCount % 6 === 1) {
                loadModemFiles();
                loadEsp32Storage();
            }

        } catch (e) {
            console.error('Polling failed:', e);
        } finally {
            isPolling = false;
        }
    }

    async function loadModemFiles() {
        try {
            const res = await fetch('/api/list_modem_files');
            if (!res.ok) throw new Error('Failed to retrieve file list');
            const data = await res.json();
            const tbody = document.getElementById('modemFilesBody');
            if (data.files && data.files.length > 0) {
                tbody.innerHTML = '';
                data.files.forEach(f => {
                    const tr = document.createElement('tr');
                    tr.style.borderBottom = '1px solid rgba(255,255,255,0.04)';
                    tr.innerHTML = `
                        <td style="padding: 10px 4px; font-family:'Fira Code', monospace; color:#38bdf8;">${f.name}</td>
                        <td style="padding: 10px 4px; text-align: right; font-family:'Fira Code', monospace; color:var(--text-primary); font-weight:600;">${f.size.toLocaleString()}</td>
                    `;
                    tbody.appendChild(tr);
                });
            } else {
                tbody.innerHTML = `<tr><td colspan="2" style="padding: 12px 4px; color: var(--text-secondary); text-align: center;">No files found.</td></tr>`;
            }
        } catch (e) {
            console.error('Error listing files:', e);
        }
    }

    async function loadEsp32Storage() {
        try {
            const res = await fetch('/api/list_esp32_storage');
            if (!res.ok) throw new Error('Failed to retrieve system specs');
            const data = await res.json();
            
            // Render flash storage
            const flashTotalMB = (data.flash_total / (1024 * 1024)).toFixed(2);
            const flashFreeMB = (data.flash_free / (1024 * 1024)).toFixed(2);
            document.getElementById('flashStatVal').innerText = `${flashFreeMB} MB / ${flashTotalMB} MB`;
            const flashPct = Math.min(100, Math.round((data.flash_free / data.flash_total) * 100));
            document.getElementById('flashBar').style.width = `${flashPct}%`;
            
            // Render heap
            const heapKB = (data.heap_free / 1024).toFixed(1);
            document.getElementById('heapStatVal').innerText = `${heapKB} KB`;
            const heapPct = Math.min(100, Math.round((data.heap_free / 300000) * 100));
            document.getElementById('heapBar').style.width = `${heapPct}%`;
            
            // Render PSRAM
            if (data.psram_total > 0) {
                const psramMB = (data.psram_total / (1024 * 1024)).toFixed(2);
                document.getElementById('psramStatVal').innerText = `${psramMB} MB / ${psramMB} MB`;
                document.getElementById('psramBar').style.width = '100%';
            } else {
                document.getElementById('psramStatVal').innerText = 'Not Equipped';
                document.getElementById('psramBar').style.width = '0%';
            }
        } catch (e) {
            console.error('Error fetching storage details:', e);
        }
    }

    async function triggerOTA() {
        const btn = document.getElementById('btnTrigger');
        const url1 = document.getElementById('fwUrlInput1').value;
        const url2 = document.getElementById('fwUrlInput2').value;
        const url3 = document.getElementById('fwUrlInput3').value;
        const url4 = document.getElementById('fwUrlInput4').value;
        btn.setAttribute('disabled', 'true');
        clearConsole();
        logOffset = 0;
        appendLog(null, 'system', 'triggering', 0, 0, 0, 'User triggered OTA sequence with 4 URLs.', 'info');
        
        try {
            const query = `url1=${encodeURIComponent(url1)}&url2=${encodeURIComponent(url2)}&url3=${encodeURIComponent(url3)}&url4=${encodeURIComponent(url4)}`;
            const res = await fetch(`/api/trigger?${query}`, { method: 'POST' });
            if (!res.ok) throw new Error('API server rejected trigger request');
            appendLog(null, 'system', 'triggered', 0, 0, 0, 'OTA update process successfully spawned in background.', 'info');
        } catch (e) {
            appendLog(null, 'system', 'error', 0, 0, 0, 'Failed to trigger update: ' + e.message, 'error');
            btn.removeAttribute('disabled');
        }
    }

    async function testGprs() {
        appendLog(null, 'gprs_diag', 'checking', 0, 0, 0, 'Sending GPRS diagnostics inquiry...', 'info');
        try {
            const res = await fetch('/api/test_gprs', { method: 'POST' });
            const data = await res.json();
            if (data.connected) {
                appendLog(null, 'gprs_diag', 'idle', 0, 0, 0, 'GPRS connection verified successfully.', 'info');
            } else {
                appendLog(null, 'gprs_diag', 'error', 0, 1, 0, 'GPRS connection is offline!', 'error');
            }
        } catch (e) {
            appendLog(null, 'gprs_diag', 'error', 0, 1, 0, 'GPRS verification failed: ' + e.message, 'error');
        }
    }

    async function pingServer() {
        const url1 = document.getElementById('fwUrlInput1').value;
        appendLog(null, 'ping_diag', 'checking', 0, 0, 0, `Checking host resolution and pinging server: ${url1}...`, 'info');
        try {
            const res = await fetch(`/api/ping_server?url=${encodeURIComponent(url1)}`, { method: 'POST' });
            const data = await res.json();
            if (data.success) {
                appendLog(null, 'ping_diag', 'idle', 0, 0, 0, 'Ping transaction complete. Target server responded.', 'info');
            } else {
                appendLog(null, 'ping_diag', 'error', 0, 2, 0, 'Target server ping transaction failed!', 'error');
            }
        } catch (e) {
            appendLog(null, 'ping_diag', 'error', 0, 2, 0, 'Ping request failed: ' + e.message, 'error');
        }
    }

    async function clearCache() {
        appendLog(null, 'cleanup', 'working', 0, 0, 0, 'Sending cache cleanup command...', 'info');
        try {
            await fetch('/api/clear_cache', { method: 'POST' });
            appendLog(null, 'cleanup', 'idle', 0, 0, 0, 'Modem storage cache cleared.', 'info');
            logOffset = 0;
            clearConsole();
            loadModemFiles();
        } catch (e) {
            appendLog(null, 'cleanup', 'error', 0, 0, 0, 'Cache cleanup command failed: ' + e.message, 'error');
        }
    }

    async function clearErrorAck() {
        appendLog(null, 'ack_errors', 'working', 0, 0, 0, 'Acknowledging and clearing diagnostic error registers...', 'info');
        try {
            await fetch('/api/write_register?register=3&value=0', { method: 'POST' });
            await fetch('/api/write_register?register=1&value=0', { method: 'POST' });
            appendLog(null, 'ack_errors', 'idle', 0, 0, 0, 'Diagnostic error status acknowledged and reset to Idle.', 'info');
        } catch (e) {
            appendLog(null, 'ack_errors', 'error', 0, 0, 0, 'ACK command failed: ' + e.message, 'error');
        }
    }

    async function rebootDevice() {
        if (!confirm("Are you sure you want to reboot the ESP32 target controller?")) return;
        appendLog(null, 'reboot', 'restarting', 0, 0, 0, 'Sending hardware reboot command. Device offline...', 'warn');
        try {
            await fetch('/api/reboot', { method: 'POST' });
            setTimeout(() => {
                appendLog(null, 'reboot', 'complete', 0, 0, 0, 'Device rebooted. Dashboard reconnected.', 'info');
            }, 3000);
        } catch (e) {
            appendLog(null, 'reboot', 'error', 0, 0, 0, 'Reboot command failed: ' + e.message, 'error');
        }
    }

    async function simulateStatus(statusVal) {
        appendLog(null, 'sim_inject', 'working', 0, 0, 0, `Injecting simulated register status: ${statusVal}...`, 'info');
        try {
            await fetch(`/api/write_register?register=1&value=${statusVal}`, { method: 'POST' });
            let prog = 0;
            if (statusVal === 1) prog = 15;
            else if (statusVal === 2) prog = 45;
            else if (statusVal === 3) prog = 85;
            else if (statusVal === 4) prog = 100;
            
            await fetch(`/api/write_register?register=2&value=${prog}`, { method: 'POST' });
            
            if (statusVal === 5) {
                await fetch(`/api/write_register?register=3&value=3`, { method: 'POST' });
            } else {
                await fetch(`/api/write_register?register=3&value=0`, { method: 'POST' });
            }
        } catch (e) {
            console.error('Simulation write failed:', e);
        }
    }

    // Set polling interval
    setInterval(pollStatus, 500);
</script>
</body>
</html>
)rawhtml";

#endif // WEB_GUI_H
