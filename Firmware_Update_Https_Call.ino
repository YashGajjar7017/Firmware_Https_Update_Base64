#include "firmware_update.h"
#include "modbus_state.h"
#include "web_gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#else
// Windows Native simulation headers
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <sstream>
#pragma comment(lib, "Ws2_32.lib")
#endif

// Log entry structure for UI display
struct LogEntry {
    char timestamp[32];
    char event[32];
    char state[32];
    int progress;
    int error;
    int part;
    char details[128];
};

#define MAX_LOG_ENTRIES 100
static LogEntry s_log_buffer[MAX_LOG_ENTRIES];
static int s_log_count = 0;

// Logging helper function
static void add_log(const char* event, const char* state, int progress, int error_code, int part, const char* details) {
    time_t raw_time;
    time(&raw_time);
    struct tm* time_info = localtime(&raw_time);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", time_info);

    if (s_log_count < MAX_LOG_ENTRIES) {
        LogEntry& entry = s_log_buffer[s_log_count];
        strncpy(entry.timestamp, time_str, sizeof(entry.timestamp));
        strncpy(entry.event, event, sizeof(entry.event));
        strncpy(entry.state, state, sizeof(entry.state));
        entry.progress = progress;
        entry.error = error_code;
        entry.part = part;
        strncpy(entry.details, details, sizeof(entry.details));
        s_log_count++;
    }

    // Log to standard output / serial
    printf("{\"timestamp\":\"%s\",\"event\":\"%s\",\"state\":\"%s\",\"progress\":%d,\"error_code\":%d,\"part\":%d,\"details\":\"%s\"}\n",
           time_str, event, state, progress, error_code, part, details);
    fflush(stdout);
}

// CRC32 verification helper
static uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

// Simple Base64 encoder for generating dynamic test vectors
static void base64_encode(const uint8_t* src, size_t len, char* dst) {
    static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0;
    size_t out_idx = 0;
    while (i < len) {
        uint32_t val = 0;
        int count = 0;
        for (int j = 0; j < 3; j++) {
            val <<= 8;
            if (i < len) {
                val |= src[i++];
                count++;
            }
        }
        if (count == 3) {
            dst[out_idx++] = b64_chars[(val >> 18) & 0x3F];
            dst[out_idx++] = b64_chars[(val >> 12) & 0x3F];
            dst[out_idx++] = b64_chars[(val >> 6) & 0x3F];
            dst[out_idx++] = b64_chars[val & 0x3F];
        } else if (count == 2) {
            dst[out_idx++] = b64_chars[(val >> 10) & 0x3F];
            dst[out_idx++] = b64_chars[(val >> 4) & 0x3F];
            dst[out_idx++] = b64_chars[(val << 2) & 0x3F];
            dst[out_idx++] = '=';
        } else if (count == 1) {
            dst[out_idx++] = b64_chars[(val >> 2) & 0x3F];
            dst[out_idx++] = b64_chars[(val << 4) & 0x3F];
            dst[out_idx++] = '=';
            dst[out_idx++] = '=';
        }
    }
    dst[out_idx] = '\0';
}

#ifdef ESP_PLATFORM
// =========================================================================
// ESP32 Native Web Server Implementation
// =========================================================================
static WebServer server(80);

static void handle_root() {
    server.send(200, "text/html", index_html);
}

static void handle_status() {
    int offset = 0;
    if (server.hasArg("offset")) {
        offset = server.arg("offset").toInt();
    }
    
    String json = "{\n";
    json += "  \"gprs_connected\": true,\n";
    json += "  \"status\": " + String(modbus_get_register(REG_DOWNLOAD_STATUS)) + ",\n";
    json += "  \"progress\": " + String(modbus_get_register(REG_PROGRESS_PERCENT)) + ",\n";
    json += "  \"error\": " + String(modbus_get_register(REG_ERROR_CODE)) + ",\n";
    json += "  \"part\": " + String(modbus_get_register(REG_CURRENT_PART)) + ",\n";
    json += "  \"logs\": [\n";
    for (int i = offset; i < s_log_count; i++) {
        json += "    {\n";
        json += "      \"timestamp\": \"" + String(s_log_buffer[i].timestamp) + "\",\n";
        json += "      \"event\": \"" + String(s_log_buffer[i].event) + "\",\n";
        json += "      \"state\": \"" + String(s_log_buffer[i].state) + "\",\n";
        json += "      \"progress\": " + String(s_log_buffer[i].progress) + ",\n";
        json += "      \"error\": " + String(s_log_buffer[i].error) + ",\n";
        json += "      \"part\": " + String(s_log_buffer[i].part) + ",\n";
        json += "      \"details\": \"" + String(s_log_buffer[i].details) + "\"\n";
        json += "    }" + String(i == s_log_count - 1 ? "" : ",") + "\n";
    }
    json += "  ]\n";
    json += "}";
    server.send(200, "application/json", json);
}

static void ota_background_task(void* pvParameters) {
    fw_update_context_t ctx;
    fw_update_init(&ctx, add_log);
    ctx.gprs_connected = true;
    
    // Dummy firmware base64 payload split into 4 parts
    const char* dummy_firmware = 
        "ESP32_Firmware_Image_Verification_Success_Flow_Through_GPRS_And_PSRAM_Buffer_Decoded_Successfully_OTA_Active";
    size_t fw_len = strlen(dummy_firmware);
    uint32_t expected_crc = calculate_crc32((const uint8_t*)dummy_firmware, fw_len);

    char* b64_buffer = (char*)malloc(fw_len * 2);
    base64_encode((const uint8_t*)dummy_firmware, fw_len, b64_buffer);
    size_t b64_len = strlen(b64_buffer);

    size_t part_len = b64_len / FW_UPDATE_NUM_PARTS;
    char* parts[FW_UPDATE_NUM_PARTS];
    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        size_t start = i * part_len;
        size_t len = (i == FW_UPDATE_NUM_PARTS - 1) ? (b64_len - start) : part_len;
        parts[i] = (char*)malloc(len + 1);
        strncpy(parts[i], b64_buffer + start, len);
        parts[i][len] = '\0';
    }

    const char* const_parts[FW_UPDATE_NUM_PARTS] = { parts[0], parts[1], parts[2], parts[3] };
    
    // Simulate real updates with delay transitions
    fw_update_verify_gprs(&ctx);
    delay(800);
    
    fw_update_init_psram(&ctx);
    delay(800);
    
    for (int part = 1; part <= FW_UPDATE_NUM_PARTS; part++) {
        fw_update_process_part(&ctx, part, const_parts[part - 1]);
        delay(1200);
    }
    
    fw_update_validate_image(&ctx, expected_crc);
    delay(800);
    
    fw_update_flash_ota(&ctx);
    
    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        free(parts[i]);
    }
    free(b64_buffer);
    fw_update_clean_cache(&ctx);

    vTaskDelete(NULL);
}

static void handle_trigger() {
    s_log_count = 0;
    xTaskCreate(ota_background_task, "ota_task", 8192, NULL, 5, NULL);
    server.send(200, "application/json", "{\"status\":\"triggered\"}");
}

void setup() {
    Serial.begin(115200);
    
    // Set up local WiFi Access Point
    WiFi.softAP("ESP32-Firmware-Portal", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
    server.on("/", HTTP_GET, handle_root);
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/trigger", HTTP_POST, handle_trigger);
    server.begin();
    
    Serial.println("HTTP Server started on Port 80");
}

void loop() {
    server.handleClient();
    delay(2);
}

#else
// =========================================================================
// PC Simulation Server Implementation (Win32 Native API)
// =========================================================================
static bool s_update_in_progress = false;

static DWORD WINAPI run_mock_update(LPVOID lpParam) {
    (void)lpParam;
    fw_update_context_t ctx;
    fw_update_init(&ctx, add_log);
    ctx.gprs_connected = true;
    
    const char* dummy_firmware = 
        "ESP32_Firmware_Image_Verification_Success_Flow_Through_GPRS_And_PSRAM_Buffer_Decoded_Successfully_OTA_Active";
    size_t fw_len = strlen(dummy_firmware);
    uint32_t expected_crc = calculate_crc32((const uint8_t*)dummy_firmware, fw_len);

    char* b64_buffer = (char*)malloc(fw_len * 2);
    base64_encode((const uint8_t*)dummy_firmware, fw_len, b64_buffer);
    size_t b64_len = strlen(b64_buffer);

    size_t part_len = b64_len / FW_UPDATE_NUM_PARTS;
    char* parts[FW_UPDATE_NUM_PARTS];
    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        size_t start = i * part_len;
        size_t len = (i == FW_UPDATE_NUM_PARTS - 1) ? (b64_len - start) : part_len;
        parts[i] = (char*)malloc(len + 1);
        strncpy(parts[i], b64_buffer + start, len);
        parts[i][len] = '\0';
    }

    const char* const_parts[FW_UPDATE_NUM_PARTS] = { parts[0], parts[1], parts[2], parts[3] };
    
    fw_update_verify_gprs(&ctx);
    Sleep(800);
    
    fw_update_init_psram(&ctx);
    Sleep(800);
    
    for (int part = 1; part <= FW_UPDATE_NUM_PARTS; part++) {
        fw_update_process_part(&ctx, part, const_parts[part - 1]);
        Sleep(1200);
    }
    
    fw_update_validate_image(&ctx, expected_crc);
    Sleep(800);
    
    fw_update_flash_ota(&ctx);
    
    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        free(parts[i]);
    }
    free(b64_buffer);
    fw_update_clean_cache(&ctx);

    s_update_in_progress = false;
    return 0;
}

static void trigger_ota_update_thread() {
    if (!s_update_in_progress) {
        s_update_in_progress = true;
        s_log_count = 0; // Reset logs
        CreateThread(NULL, 0, run_mock_update, NULL, 0, NULL);
    }
}

static DWORD WINAPI win_http_server_thread(LPVOID lpParam) {
    (void)lpParam;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("Winsock startup failed!\n");
        return 1;
    }
    
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }
    
    char opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    
    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    
    printf("=========================================================================\n");
    printf("     ESP32 REST WEB GUI Server successfully running on host PC           \n");
    printf("     Open URL in browser: http://localhost:8080                          \n");
    printf("=========================================================================\n\n");
    
    while (true) {
        SOCKET client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == INVALID_SOCKET) {
            continue;
        }
        
        char buffer[2048] = {0};
        recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        std::string request(buffer);
        size_t method_end = request.find(' ');
        if (method_end == std::string::npos) {
            closesocket(client_fd);
            continue;
        }
        std::string method = request.substr(0, method_end);
        size_t path_start = method_end + 1;
        size_t path_end = request.find(' ', path_start);
        if (path_end == std::string::npos) {
            closesocket(client_fd);
            continue;
        }
        std::string path = request.substr(path_start, path_end - path_start);
        
        std::ostringstream response;
        
        if (path == "/") {
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/html\r\n"
                     << "Content-Length: " << strlen(index_html) << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << index_html;
        } else if (path.rfind("/api/status", 0) == 0) {
            int offset = 0;
            size_t query_pos = path.find("?offset=");
            if (query_pos != std::string::npos) {
                offset = atoi(path.substr(query_pos + 8).c_str());
            }
            
            std::ostringstream json;
            json << "{\n"
                 << "  \"gprs_connected\": true,\n"
                 << "  \"status\": " << modbus_get_register(REG_DOWNLOAD_STATUS) << ",\n"
                 << "  \"progress\": " << modbus_get_register(REG_PROGRESS_PERCENT) << ",\n"
                 << "  \"error\": " << modbus_get_register(REG_ERROR_CODE) << ",\n"
                 << "  \"part\": " << modbus_get_register(REG_CURRENT_PART) << ",\n"
                 << "  \"logs\": [\n";
            for (int i = offset; i < s_log_count; i++) {
                json << "    {\n"
                     << "      \"timestamp\": \"" << s_log_buffer[i].timestamp << "\",\n"
                     << "      \"event\": \"" << s_log_buffer[i].event << "\",\n"
                     << "      \"state\": \"" << s_log_buffer[i].state << "\",\n"
                     << "      \"progress\": " << s_log_buffer[i].progress << ",\n"
                     << "      \"error\": " << s_log_buffer[i].error << ",\n"
                     << "      \"part\": " << s_log_buffer[i].part << ",\n"
                     << "      \"details\": \"" << s_log_buffer[i].details << "\"\n"
                     << "    }" << (i == s_log_count - 1 ? "" : ",") << "\n";
            }
            json << "  ]\n"
                 << "}";
            
            std::string body = json.str();
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else if (path == "/api/trigger" && method == "POST") {
            trigger_ota_update_thread();
            std::string body = "{\"status\":\"triggered\"}";
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else {
            std::string body = "{\"error\":\"Not Found\"}";
            response << "HTTP/1.1 404 Not Found\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        }
        
        std::string res_str = response.str();
        send(client_fd, res_str.c_str(), (int)res_str.length(), 0);
        closesocket(client_fd);
    }
    
    closesocket(server_fd);
    WSACleanup();
    return 0;
}

int main(void) {
    HANDLE thread_handle = CreateThread(NULL, 0, win_http_server_thread, NULL, 0, NULL);
    if (thread_handle != NULL) {
        WaitForSingleObject(thread_handle, INFINITE);
        CloseHandle(thread_handle);
    }
    return 0;
}
#endif
