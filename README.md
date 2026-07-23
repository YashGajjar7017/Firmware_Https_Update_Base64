# ESP32 GPRS HTTPS Firmware Updater with Modbus Telemetry & Web Dashboard

This repository contains C/C++ firmware designed for ESP32 microcontrollers. It manages downloading a Base64-encoded binary firmware over a GPRS cellular modem connection, decoding it on-the-fly, writing directly to the OTA partition, exposing status via Modbus registers, and hosting an interactive Web GUI dashboard.

---

## Codebase Architecture & File Structure

The project is structured as follows:

*   **[Firmware_Update_Https_Call.ino](file:///a:/Coding/IOT_Code/Firmware_Update_Https_Call/Firmware_Update_Https_Call.ino)**: Main Arduino sketch entry point. Coordinates Wi-Fi AP setup, starts the web server routing endpoints (`/`, `/api/status`, `/api/trigger`), manages background tasks, and performs the download/OTA execution. Includes standard Win32 mock HTTP sockets for PC hosting.
*   **[def.h](file:///a:/Coding/IOT_Code/Firmware_Update_Https_Call/def.h)**: System configurations, register defines (Holding Registers 0 to 3), and Modbus read/write hooks.
*   **[web_gui.h](file:///a:/Coding/IOT_Code/Firmware_Update_Https_Call/web_gui.h)**: Embedded HTML, CSS, and JS web dashboard. Uses glassmorphic dark mode styling to visualize download progress, Modbus telemetry, and log files.
*   **[modbus_state.h](file:///a:/Coding/IOT_Code/Firmware_Update_Https_Call/modbus_state.h)** & **[modbus_state.cpp](file:///a:/Coding/IOT_Code/Firmware_Update_Https_Call/modbus_state.cpp)**: Thread-safe holding registers registry utilizing FreeRTOS mutexes.
*   **[firmware_update.h](file:///a:/Coding/IOT_Code/Firmware_Update_Https_Call/firmware_update.h)** & **[firmware_update.cpp](file:///a:/Coding/IOT_Code/Firmware_Update_Https_Call/firmware_update.cpp)**: Core logic for base64 streaming, dynamic SPIRAM/PSRAM heap growth, and native flashing.

---

## Modbus Register Mapping

| Holding Register Offset | Description | Value Range |
|---|---|---|
| **Register 0** | Download Status | `0` = Idle, `1` = Downloading, `2` = Base64 Decoding, `3` = Flashing, `4` = Complete, `5` = Error |
| **Register 1** | Progress Percentage | `0` to `100` (%) |
| **Register 2** | Diagnostic Error Code | `0` = No Error, `1` = GPRS Fail, `2` = HTTP Fail, `3` = Base64 Decode Error, `4` = Memory/PSRAM Error, `5` = OTA Flash Fail |
| **Register 3** | Current Part Received | `0` to `4` (sequential chunks) |

---

## Pin Configurations & Port Initialization

The GPRS modem communicates with the ESP32 via Hardware Serial 1:
*   **Modem Baud Rate**: `115200`
*   **Modem RX Pin**: GPIO `26` (Redefine via `MODEM_RX_PIN` macro)
*   **Modem TX Pin**: GPIO `27` (Redefine via `MODEM_TX_PIN` macro)

During `setup()`, the serial port and buffers are opened:
```cpp
Serial1.setRxBufferSize(8192);
Serial1.setTxBufferSize(8192);
Serial1.begin(MODEM_BAUD_RATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
```

---

## How to Build & Run

### 1. Compile on ESP32 Target Hardware (Arduino IDE)
1. Open [Firmware_Update_Https_Call.ino](file:///a:/Coding/IOT_Code/Firmware_Update_Https_Call/Firmware_Update_Https_Call.ino) in Arduino IDE.
2. Select your ESP32 board and make sure PSRAM is enabled in the configuration menu.
3. Upload to target board. Connect to Wi-Fi Access Point `ESP32-Firmware-Portal` (Password: `12345678`) and open your web browser to `http://192.168.4.1`.

### 2. Compile & Run Host PC Simulator
To compile the test simulation server locally on Windows:
```bash
g++ -O3 -Wall -Wextra -std=c++11 -x c++ Firmware_Update_Https_Call.ino -x none firmware_update.cpp modbus_state.cpp -lws2_32
.\a.exe
```
Open your web browser to `http://localhost:8080` to access the interactive web panel.
