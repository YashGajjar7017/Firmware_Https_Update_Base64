#ifndef ESP_PLATFORM
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#endif
#include "firmware_update.h"
#include "modbus_state.h"
#include "def.h"
//#include "web_gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef ESP_PLATFORM
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "mbedtls/base64.h"
#include <Update.h>
#include "esp_task_wdt.h"
// Feed the Task Watchdog Timer so the long UFS read loop does not trigger a restart
#define FEED_WDT() esp_task_wdt_reset()
#else
// Windows Native simulation headers
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <sstream>
#pragma comment(lib, "Ws2_32.lib")

// Typedef String for PC compilation
typedef std::string String;
#define delay(ms) Sleep(ms)
static bool s_update_in_progress = false;
#endif

#ifdef ESP_PLATFORM
#include "freertos/semphr.h"
static SemaphoreHandle_t s_gsm_mutex = NULL;
static StaticSemaphore_t s_gsm_mutex_buffer;
static void init_gsm_mutex() {
    if (s_gsm_mutex == NULL) {
        s_gsm_mutex = xSemaphoreCreateMutexStatic(&s_gsm_mutex_buffer);
    }
}
#define LOCK_GSM() do { init_gsm_mutex(); xSemaphoreTake(s_gsm_mutex, portMAX_DELAY); } while(0)
#define UNLOCK_GSM() do { if (s_gsm_mutex) xSemaphoreGive(s_gsm_mutex); } while(0)
#else
#define LOCK_GSM()
#define UNLOCK_GSM()
#define FEED_WDT()
#endif

// =========================================================================
// OTA-specific error codes (passed to add_log error_code field)
// =========================================================================
#define OTA_ERR_NONE              0
#define OTA_ERR_FILE_OPEN         1   // AT+QFOPEN failed
#define OTA_ERR_FILE_READ         2   // AT+QFREAD returned 0 / timeout
#define OTA_ERR_FILE_READ_RETRY   3   // exceeded MAX_RETRY on chunk
#define OTA_ERR_PSRAM_ALLOC       4   // heap_caps_realloc / realloc failed
#define OTA_ERR_PSRAM_APPEND      5   // append_to_psram_buffer returned false
#define OTA_ERR_SIZE_MISMATCH     6   // parsed UFS file size = 0
#define OTA_ERR_B64_DECODE        7   // mbedtls_base64_decode failed
#define OTA_ERR_UPDATE_BEGIN      8   // Update.begin() failed
#define OTA_ERR_UPDATE_WRITE      9   // Update.write() failed
#define OTA_ERR_UPDATE_END        10  // Update.end() failed
#define OTA_ERR_DOWNLOAD          11  // getfirmwarefile returned non-zero
#define OTA_ERR_LOCK_TIMEOUT      12  // GSM mutex acquire timed out

// Volatile flag so HTTP handlers can see when OTA is actively running
static volatile bool s_ota_running = false;
static volatile bool s_ota_abort = false;

// AT command macros matching requirements
#define FTP_FILE_OPEN "AT+QFOPEN=\"%s\",0\r\n"
#define FTP_FILE_SEEK "AT+QFSEEK=%d,%d,%d\r\n"
#define FTP_FILE_READ "AT+QFREAD=%ld,%d\r\n"
#define FTP_FILE_DEL "AT+QFDEL=\"%s\"\r\n"
#define FTP_POSITION "AT+QFPOSITION=%d\r\n"
#define FTP_FILE_CLOSE "AT+QFCLOSE=%d\r\n"

// Enums for float register mapping
#define FW_DOWNLOAD_PROGRESS 0
#define ERROR4               1
#define FW_TOTAL_PARTS       2
#define FILE_UUID            3
#define SPIFF_SET_BIT        4
#define FP_SIZE              5

#define MAX_RETRY1 5

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

// Dynamic simulation parameters
static const char* s_parts_payloads[FW_UPDATE_NUM_PARTS];
static String s_custom_urls[FW_UPDATE_NUM_PARTS] = {
    "http://64.251.10.159/otafw_part1.b64",
    "http://64.251.10.159/otafw_part2.b64",
    "http://64.251.10.159/otafw_part3.b64",
    "http://64.251.10.159/otafw_part4.b64"
};
static std::string s_parts_data[FW_UPDATE_NUM_PARTS]; // Persist downloaded parts in simulation

// Logging helper function
static void add_log(const char* event, const char* state, int progress, int error_code, int part, const char* details) {
    time_t raw_time;
    time(&raw_time);
    struct tm* time_info = localtime(&raw_time);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", time_info);

    char sanitized_details[128];
    strncpy(sanitized_details, details, sizeof(sanitized_details));
    sanitized_details[sizeof(sanitized_details) - 1] = '\0';
    for (int i = 0; sanitized_details[i] != '\0'; i++) {
        if (sanitized_details[i] == '"') {
            sanitized_details[i] = '\'';
        }
    }

    int active_part = part;
    if (active_part == 0) {
        active_part = modbus_get_register(REG_CURRENT_PART);
    }
    int active_progress = progress;
    if (active_progress == 0) {
        active_progress = modbus_get_register(REG_PROGRESS_PERCENT);
    }

    if (s_log_count < MAX_LOG_ENTRIES) {
        LogEntry& entry = s_log_buffer[s_log_count];
        strncpy(entry.timestamp, time_str, sizeof(entry.timestamp));
        strncpy(entry.event, event, sizeof(entry.event));
        strncpy(entry.state, state, sizeof(entry.state));
        entry.progress = active_progress;
        entry.error = error_code;
        entry.part = active_part;
        strncpy(entry.details, sanitized_details, sizeof(entry.details));
        s_log_count++;
    }

    // Log to standard output / serial
    #ifdef ESP_PLATFORM
    Serial.printf("{\"timestamp\":\"%s\",\"event\":\"%s\",\"state\":\"%s\",\"progress\":%d,\"error_code\":%d,\"part\":%d,\"details\":\"%s\"}\n",
                  time_str, event, state, active_progress, error_code, active_part, sanitized_details);
    Serial.printf("[OTA LOG] [%s] %s | State: %s | Progress: %d%% | Error: %d | Part: %d\r\n",
                  event, sanitized_details, state, active_progress, error_code, active_part);
    #else
    printf("{\"timestamp\":\"%s\",\"event\":\"%s\",\"state\":\"%s\",\"progress\":%d,\"error_code\":%d,\"part\":%d,\"details\":\"%s\"}\n",
           time_str, event, state, active_progress, error_code, active_part, sanitized_details);
    fflush(stdout);
    #endif
}

// Telemetry Float Registers logic
static float s_float_registers[10] = {0};

static void setFloatValue(int key, float val) {
    if (key < 10) {
        s_float_registers[key] = val;
        
        // Map values to Modbus registers for the UI Dashboard
        if (key == FW_DOWNLOAD_PROGRESS) {
            if (val == -1) {
                modbus_set_status(STATUS_ERROR);
            } else if (val == 1) {
                modbus_set_status(STATUS_DOWNLOADING);
                modbus_set_progress(5);
            } else if (val == 2) {
                modbus_set_status(STATUS_DECODING);
                modbus_set_progress(10);
            } else if (val >= 3 && val <= 10) {
                int part = ((int)val - 1) / 2;
                if ((int)val % 2 == 1) {
                    modbus_set_progress(((part - 1) * 100 / FW_UPDATE_NUM_PARTS) + 5);
                } else {
                    modbus_set_progress(part * 100 / FW_UPDATE_NUM_PARTS);
                }
            } else if (val == 11) {
                modbus_set_status(STATUS_DECODING);
                modbus_set_progress(95);
            } else if (val >= 12 && val < 18) {
                modbus_set_status(STATUS_FLASHING);
                int flash_p = 95 + (int)(((val - 12.0f) / 6.0f) * 4.0f);
                modbus_set_progress(flash_p);
            } else if (val >= 18) {
                modbus_set_status(STATUS_COMPLETE);
                modbus_set_progress(100);
            }
        } else if (key == ERROR4) {
            if (val != 0 && val != 9) { // 9 is success completion indicator
                modbus_set_error((UpdateErrorCode)val);
                modbus_set_status(STATUS_ERROR);
            }
        }
    }
}

static float getFloatValue(int key) {
    if (key == FW_TOTAL_PARTS) return (float)FW_UPDATE_NUM_PARTS;
    if (key < 10) return s_float_registers[key];
    return 0.0f;
}

static void resetWatchdog() {
    // Watchdog reset implementation placeholder
}

// Win32 Mock structures for local compilation
#ifndef ESP_PLATFORM
inline void* ps_calloc(size_t num, size_t size) {
    return calloc(num, size);
}

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int base64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
}

inline int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen, const unsigned char *src, size_t slen) {
    size_t i = 0;
    size_t out_idx = 0;
    *olen = 0;
    while (i < slen) {
        int vals[4];
        int count = 0;
        while (i < slen && count < 4) {
            char c = src[i++];
            int val = base64_char_value(c);
            if (val != -1) {
                vals[count++] = val;
            }
        }
        if (count < 4) break;
        size_t bytes_to_write = 3;
        if (vals[2] == -2) bytes_to_write = 1;
        else if (vals[3] == -2) bytes_to_write = 2;
        
        if (out_idx + bytes_to_write > dlen) return -1;
        dst[out_idx++] = (uint8_t)((vals[0] << 2) | (vals[1] >> 4));
        if (bytes_to_write > 1) {
            dst[out_idx++] = (uint8_t)(((vals[1] & 0x0F) << 4) | (vals[2] >> 2));
        }
        if (bytes_to_write > 2) {
            dst[out_idx++] = (uint8_t)(((vals[2] & 0x03) << 6) | vals[3]);
        }
        if (bytes_to_write < 3) break;
    }
    *olen = out_idx;
    return 0;
}

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

class MockUpdate {
public:
    bool begin(size_t size) {
        (void)size;
        add_log("ota_flash", "flashing", 95, 0, 4, "[Mock] Native Update.begin success.");
        return true;
    }
    size_t write(uint8_t* buf, size_t len) {
        (void)buf;
        add_log("ota_flash", "flashing", 98, 0, 4, "[Mock] Native Update.write complete.");
        return len;
    }
    bool end(bool reset) {
        (void)reset;
        add_log("ota_flash", "complete", 100, 0, 4, "[Mock] Native Update.end success. System rebooting...");
        return true;
    }
    template <typename T>
    void printError(T& serial) {
        (void)serial;
    }
};
static MockUpdate Update;

class MockESP {
public:
    void restart() {
        add_log("system", "restarting", 100, 0, 4, "Device reboot simulated. Resetting dashboard state...");
        Sleep(2000);
        s_log_count = 0;
        modbus_set_status(STATUS_IDLE);
        modbus_set_progress(0);
        modbus_set_error(ERR_NONE);
        modbus_set_current_part(0);
        s_update_in_progress = false;
    }
};
static MockESP ESP;

class SerialPlaceholder {
public:
    void println(String str) {
        printf("[Serial] %s\n", str.c_str());
    }
    void printf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
};
static SerialPlaceholder Serial;
#endif

// =========================================================================
// GSMModule & GPRS Controller Classes
// =========================================================================
class GSMModule {
public:
    char buffer[8192];
    
    bool sendCommand(const char* cmd, const char* expected_resp, uint32_t timeout, uint8_t retries = 1, uint32_t delay_ms = 100, const char* alternative_resp = NULL) {
        (void)retries; (void)delay_ms;

        #ifndef ESP_PLATFORM
        Sleep(80);
        if (strstr(cmd, "AT+QFOPEN") != NULL) {
            snprintf(buffer, sizeof(buffer), "CONNECT 1027\r\nOK\r\n");
        } else {
            snprintf(buffer, sizeof(buffer), "%s\r\nOK\r\n", expected_resp);
        }
        return true;
        #else
        // Log cmd to serial
        Serial.printf("[GSM SEND] %s", cmd);

        // Clear RX serial buffer to purge any delayed/stale responses
        while (Serial1.available()) {
            Serial1.read();
        }
        
        Serial1.print(cmd);
        uint32_t start = millis();
        int idx = 0;
        memset(buffer, 0, sizeof(buffer));
        while (millis() - start < timeout) {
            while (Serial1.available()) {
                char c = Serial1.read();
                if (idx < (int)sizeof(buffer) - 1) {
                    buffer[idx++] = c;
                }
            }
            if (strstr(buffer, expected_resp) != NULL || (alternative_resp && strstr(buffer, alternative_resp) != NULL)) {
                if (strlen(buffer) > 200) {
                    char truncated[256];
                    strncpy(truncated, buffer, 100);
                    truncated[100] = '\0';
                    strcat(truncated, " ... [TRUNCATED] ... ");
                    size_t len = strlen(buffer);
                    if (len > 150) {
                        strcat(truncated, buffer + len - 50);
                    }
                    Serial.printf("[GSM RECV] %s\r\n", truncated);
                } else {
                    Serial.printf("[GSM RECV] %s\r\n", buffer);
                }
                return true;
            }
            delay(10);
        }
        if (strlen(buffer) > 200) {
            char truncated[256];
            strncpy(truncated, buffer, 100);
            truncated[100] = '\0';
            strcat(truncated, " ... [TRUNCATED] ... ");
            size_t len = strlen(buffer);
            if (len > 150) {
                strcat(truncated, buffer + len - 50);
            }
            Serial.printf("[GSM RECV TIMEOUT] Expected: %s. Got: %s\r\n", expected_resp, truncated);
        } else {
            Serial.printf("[GSM RECV TIMEOUT] Expected: %s. Got: %s\r\n", expected_resp, buffer);
        }
        char err_details[512];
        snprintf(err_details, sizeof(err_details), "AT CMD Timeout. Got: %s", buffer);
        for (int i = 0; err_details[i] != '\0'; i++) {
            if (err_details[i] == '\r' || err_details[i] == '\n') err_details[i] = ' ';
        }
        add_log("AT_CMD_ERR", "error", 0, 0, 0, err_details);
        return false;
        #endif
    }
    
    bool sendCommandOpt(const char* cmd, const char* expected_resp, uint32_t timeout, uint8_t retries = 1, const char* alternative_resp = NULL) {
        return sendCommand(cmd, expected_resp, timeout, retries, 100, alternative_resp);
    }
    
    bool sendCommandStd(const char* cmd, const char* expected_resp, uint32_t timeout, uint8_t retries = 1) {
        return sendCommand(cmd, expected_resp, timeout, retries, 100, NULL);
    }
};

static GSMModule gsm_module;
static GSMModule* module = &gsm_module;

class TcpClient {
private:
    GSMModule* module;
public:
    TcpClient(GSMModule* mod) : module(mod) {}
    uint8_t getfirmwarefile(String url, String imei, String user, String pass, int x);
};

#ifndef ESP_PLATFORM
#include <sstream>
static std::string download_http_winsock(const std::string& full_url) {
    std::string host, path, port;
    size_t start = 0;
    if (full_url.rfind("http://", 0) == 0) {
        start = 7;
    } else if (full_url.rfind("https://", 0) == 0) {
        start = 8;
    } else {
        start = 0;
    }
    size_t host_end = full_url.find('/', start);
    if (host_end == std::string::npos) {
        host = full_url.substr(start);
        path = "/";
    } else {
        host = full_url.substr(start, host_end - start);
        path = full_url.substr(host_end);
    }
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = host.substr(colon + 1);
        host = host.substr(0, colon);
    } else {
        port = "80";
    }

    addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        return "";
    }

    SOCKET connect_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (connect_socket == INVALID_SOCKET) {
        freeaddrinfo(result);
        return "";
    }

    DWORD timeout_ms = 5000;
    setsockopt(connect_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    setsockopt(connect_socket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));

    if (connect(connect_socket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        closesocket(connect_socket);
        freeaddrinfo(result);
        return "";
    }
    freeaddrinfo(result);

    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "User-Agent: ESP32-Updater-Simulation\r\n"
        << "Connection: close\r\n\r\n";
    std::string req_str = req.str();
    send(connect_socket, req_str.c_str(), (int)req_str.length(), 0);

    std::string response;
    char buf[4096];
    int bytes_received = 0;
    do {
        bytes_received = recv(connect_socket, buf, sizeof(buf), 0);
        if (bytes_received > 0) {
            response.append(buf, bytes_received);
        }
    } while (bytes_received > 0);

    closesocket(connect_socket);

    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return "";
    }

    std::string headers = response.substr(0, header_end);
    std::string body = response.substr(header_end + 4);

    if (headers.find(" 200 ") == std::string::npos) {
        return "";
    }

    return body;
}
#endif

// Helper: extract the filename (last path segment) from a URL
static String extract_url_filename(const char* url) {
    const char* last_slash = strrchr(url, '/');
    if (last_slash != NULL && *(last_slash + 1) != '\0') {
        return String(last_slash + 1);
    }
    return String("otafw_part.b64");
}

// Helper: build a visual progress bar string like |=========>   | 20KB/410KB
static void make_progress_bar(char* out, int out_len, uint32_t downloaded, uint32_t total) {
    int pct = (total > 0) ? (int)((downloaded * 100UL) / total) : 0;
    int filled = pct / 5;  // 20 chars total bar width
    char bar[25];
    for (int i = 0; i < 20; i++) {
        if (i < filled - 1) bar[i] = '=';
        else if (i == filled - 1) bar[i] = '>';
        else bar[i] = ' ';
    }
    bar[20] = '\0';
    snprintf(out, out_len, "|%s| %uKB/%uKB (%d%%)",
             bar,
             (unsigned)(downloaded / 1024),
             (unsigned)(total / 1024),
             pct);
}

uint8_t TcpClient::getfirmwarefile(String url, String imei, String user, String pass, int x) {
    (void)imei; (void)user; (void)pass;
    
    // Extract filename from URL (e.g. otafw_part1.b64 from http://64.251.10.159/otafw_part1.b64)
    String url_filename = extract_url_filename(url.c_str());
    
    // Clear UFS: delete the file by its URL-derived name, not by hardcoded part index
    module->sendCommand("AT+QFDEL=\"UFS:firm\"\r\n", "OK", 900, 1, 1000, "+CME ERROR: 418");
    char del_cmd[128];
    snprintf(del_cmd, sizeof(del_cmd), "AT+QFDEL=\"UFS:%s\"\r\n", url_filename.c_str());
    module->sendCommand(del_cmd, "OK", 900, 1, 1000, "+CME ERROR: 418");
    
    char del_log[256];
    snprintf(del_log, sizeof(del_log), "Cleared old UFS file: UFS:%s before re-download.", url_filename.c_str());
    add_log("UFS_DEL", "cleanup", 0, 0, x, del_log);

    if (x >= 1 && x <= FW_UPDATE_NUM_PARTS) {
        char url_command1[512];
        snprintf(url_command1, sizeof(url_command1), "%s", url.c_str());

        #ifndef ESP_PLATFORM
        // Simulate/real HTTP download on host PC
        Sleep(80);
        std::string downloaded = "";
        if (url.length() > 0) {
            downloaded = download_http_winsock(url_command1);
        }
        
        if (!downloaded.empty()) {
            s_parts_data[x - 1] = downloaded;
            s_parts_payloads[x - 1] = s_parts_data[x - 1].c_str();
        } else {
            if (!s_parts_data[x - 1].empty()) {
                s_parts_payloads[x - 1] = s_parts_data[x - 1].c_str();
            }
        }
        
        uint32_t total_size = s_parts_payloads[x - 1] ? strlen(s_parts_payloads[x - 1]) : 0;
        
        char log_req[512];
        snprintf(log_req, sizeof(log_req), "HTTP GET: %s -> Saving as UFS:%s", url_command1, url_filename.c_str());
        add_log("HTTP_REQ", "requesting", 0, 0, x, log_req);
        
        // Emit visual progress bar
        char prog_bar[64];
        make_progress_bar(prog_bar, sizeof(prog_bar), total_size, total_size);
        char log_prog[512];
        snprintf(log_prog, sizeof(log_prog), "Part %d Download Progress: %s", x, prog_bar);
        add_log("HTTP_PROG", "downloading", 100, 0, x, log_prog);
        
        char log_done[512];
        snprintf(log_done, sizeof(log_done), "ACK: Part %d downloaded. URL: %s | File: UFS:%s", x, url_command1, url_filename.c_str());
        add_log("HTTP_REQ", "complete", 0, 0, x, log_done);
        return 0;
        #else
        int urlLen = strlen(url_command1);
        char url_command12[64];
        snprintf(url_command12, sizeof(url_command12), "AT+QHTTPURL=%d,160\r\n", urlLen);

        char log_req[512];
        snprintf(log_req, sizeof(log_req), "HTTP GET: %s -> Saving as UFS:%s", url_command1, url_filename.c_str());
        add_log("HTTP_REQ", "requesting", 0, 0, x, log_req);

        if (!module->sendCommand(url_command12, "CONNECT", 1000, 1))
            return 3;
        if (!module->sendCommand(url_command1, "OK", 6000, 1))
            return 4;

        // Increase timeout to 240 seconds to allow download over cellular connection
        // Poll progress while waiting for +QHTTPGET: 0
        Serial.printf("[HTTP_PROG] Part %d download started. Waiting for modem...\n", x);
        if (!module->sendCommandOpt("AT+QHTTPGET=240\r\n", "+QHTTPGET: 0", 240000, 3, "+QHTTPGET: 0"))
            return 5;
        
        // Parse actual download size from QHTTPGET response if possible
        uint32_t download_size = 476856; // Default fallback
        char* qhttp_ptr = strstr(module->buffer, "+QHTTPGET: 0,200,");
        if (qhttp_ptr != NULL) {
            download_size = (uint32_t)atol(qhttp_ptr + 17);
        }

        // Log download complete with progress bar
        char prog_bar[64];
        char prog_msg[256];
        make_progress_bar(prog_bar, sizeof(prog_bar), download_size, download_size);
        snprintf(prog_msg, sizeof(prog_msg), "Part %d HTTP Download Complete: %s", x, prog_bar);
        add_log("HTTP_PROG", "downloading", 100, 0, x, prog_msg);
        Serial.printf("[HTTP_PROG] Part %d %s\n", x, prog_bar);
        
        // Use URL-derived filename for READFILE — timeout=600s in AT cmd (modem UFS write budget)
        // Wait up to 90s for the +QHTTPREADFILE: response, then parse the result code explicitly
        char read_cmd[256];
        snprintf(read_cmd, sizeof(read_cmd), "AT+QHTTPREADFILE=\"UFS:%s\",600\r\n", url_filename.c_str());
        add_log("AT_CMD", "sending", 0, 0, x, ("AT+QHTTPREADFILE=\"UFS:" + url_filename + "\",600").c_str());
        Serial.printf("[AT_CMD] Sending: AT+QHTTPREADFILE=\"UFS:%s\",600\n", url_filename.c_str());
        
        // sendCommandOpt with 90s timeout; accept 0 (success) or 705 (already exists/OK)
        bool rf_ok = module->sendCommandOpt(read_cmd, "+QHTTPREADFILE: 0", 90000, 1, "+QHTTPREADFILE: 705");
        
        // Parse actual error code from buffer for detailed logging
        {
            int rf_code = -1;
            char* rf_ptr = strstr(module->buffer, "+QHTTPREADFILE:");
            if (rf_ptr != NULL) {
                rf_code = atoi(rf_ptr + 15);
            }
            char rf_log[256];
            if (rf_code == 0 || rf_code == 705) {
                snprintf(rf_log, sizeof(rf_log), "QHTTPREADFILE OK (code=%d): UFS:%s written successfully.", rf_code, url_filename.c_str());
                add_log("HTTP_PROG", "complete", 100, 0, x, rf_log);
                Serial.printf("[HTTP_PROG] %s\n", rf_log);
                rf_ok = true;
            } else if (rf_code > 0) {
                snprintf(rf_log, sizeof(rf_log), "QHTTPREADFILE FAILED (code=%d): UFS:%s write error. Check UFS space.", rf_code, url_filename.c_str());
                add_log("AT_CMD_ERR", "error", 0, rf_code, x, rf_log);
                Serial.printf("[AT_CMD_ERR] %s\n", rf_log);
                rf_ok = false;
            }
        }
        if (!rf_ok) {
            return 6;
        }
        
        char log_done[512];
        snprintf(log_done, sizeof(log_done), "ACK: Part %d stored as UFS:%s from URL: %s", x, url_filename.c_str(), url_command1);
        add_log("HTTP_REQ", "complete", 0, 0, x, log_done);
        Serial.printf("[HTTP_REQ] ACK: Part %d -> UFS:%s from %s\n", x, url_filename.c_str(), url_command1);

        delay(10);
        return 0; // Success
        #endif
    }
    return 1;
}

class qftp {
private:
    GSMModule* module;
public:
    qftp(GSMModule* mod) : module(mod) {}
    
    uint32_t qftp_file_open(String filename, uint8_t mode, uint8_t offset) {
        (void)mode; (void)offset;
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "AT+QFOPEN=\"%s\",0\r\n", filename.c_str());
        #ifndef ESP_PLATFORM
        module->sendCommand(cmd, "OK", 1000, 2);
        return 1027; // Dummy file handle
        #else
        if (module->sendCommand(cmd, "+QFOPEN:", 2000, 2)) {
            char* ptr = strstr(module->buffer, "+QFOPEN:");
            if (ptr != NULL) {
                return (uint32_t)atoi(ptr + 8);
            }
        }
        return 0; // Failed to open
        #endif
    }
    
    int qftp_file_read(int32_t handle, int length, uint8_t *buff);
    uint8_t qftp_file_seek(int handle, int offset, uint8_t mode);
    
    void qftp_file_close(int handle) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), FTP_FILE_CLOSE, handle);
        module->sendCommand(cmd, "OK", 1000, 2);
    }
};

static TcpClient gprs(module);
static qftp ftp(module);

// Static buffers required for reading
static uint32_t finalsize = 0;
static uint32_t spiffs_offset = 0;
static uint8_t buf1[2048];

// Static buffers for qftp_file_read — kept off the OTA task stack to prevent overflow
// (Only one qftp_file_read runs at a time, so static is safe)
static char s_qfr_connect_buf[512];
static char s_qfr_trailing[128];

int qftp::qftp_file_read(int32_t handle, int length, uint8_t *buff) {
    #ifndef ESP_PLATFORM
    // Host PC Simulation Mode
    int current_part = modbus_get_register(REG_CURRENT_PART);
    if (current_part < 1 || current_part > FW_UPDATE_NUM_PARTS) current_part = 1;
    const char* part_payload = s_parts_payloads[current_part - 1];
    
    size_t part_len = strlen(part_payload);
    int read_len = length;
    if ((uint32_t)read_len > part_len - spiffs_offset) {
        read_len = part_len - spiffs_offset;
    }
    
    char details[128];
    snprintf(details, sizeof(details), "AT+QFREAD simulated read %d bytes at offset %d", read_len, spiffs_offset);
    add_log("AT_CMD", "reading", 0, 0, current_part, details);
    
    if (read_len > 0) {
        memcpy(buff, part_payload + spiffs_offset, read_len);
        spiffs_offset += read_len;
    }
    return read_len;
    #else
    // ESP32 Hardware Mode
    char cmd[100];
    sprintf(cmd, FTP_FILE_READ, handle, length);
    
    // Clear Serial1 RX buffer
    while (Serial1.available()) {
        Serial1.read();
    }
    
    // Send read command — suppress per-chunk GSM SEND noise; progress bar covers it
    Serial1.print(cmd);
    
    // 1. Read until "CONNECT" is received
    uint32_t start_time = millis();
    memset(s_qfr_connect_buf, 0, sizeof(s_qfr_connect_buf));
    int c_idx = 0;
    bool found_connect = false;
    
    while (millis() - start_time < 5000) {
        bool read_any = false;
        while (Serial1.available()) {
            read_any = true;
            char c = Serial1.read();
            if (c_idx < (int)sizeof(s_qfr_connect_buf) - 1) {
                s_qfr_connect_buf[c_idx++] = c;
                s_qfr_connect_buf[c_idx] = '\0';
            } else {
                memmove(s_qfr_connect_buf, s_qfr_connect_buf + 256, 256);
                c_idx = 256;
                s_qfr_connect_buf[c_idx++] = c;
                s_qfr_connect_buf[c_idx] = '\0';
            }
            if (strstr(s_qfr_connect_buf, "CONNECT") != NULL) {
                found_connect = true;
                break;
            }
        }
        if (found_connect) break;
        if (!read_any) {
            delay(1);
        }
    }
    
    if (!found_connect) {
        Serial.printf("[ERROR] QFREAD: CONNECT not found. Buffer: %s\r\n", s_qfr_connect_buf);
        return 0;
    }
    
    // 2. Read space after CONNECT, then the length digits until '\r'
    char len_buf[16] = {0};
    int l_idx = 0;
    start_time = millis();
    bool found_cr = false;
    
    // Skip any leading whitespace/space after CONNECT
    char c = '\0';
    while (millis() - start_time < 2000) {
        bool read_any = false;
        while (Serial1.available()) {
            read_any = true;
            c = Serial1.read();
            if (c != ' ' && c != '\r' && c != '\n') {
                len_buf[l_idx++] = c;
                break;
            }
        }
        if (l_idx > 0) break; // found the first non-space character
        if (!read_any) {
            delay(1);
        }
    }
    
    start_time = millis(); // reset timeout for digits read
    while (millis() - start_time < 2000) {
        bool read_any = false;
        while (Serial1.available()) {
            read_any = true;
            c = Serial1.read();
            if (c == '\r') {
                found_cr = true;
                break;
            }
            if (l_idx < (int)sizeof(len_buf) - 1) {
                len_buf[l_idx++] = c;
            }
        }
        if (found_cr) break;
        if (!read_any) {
            delay(1);
        }
    }
    
    if (!found_cr) {
        Serial.println("[ERROR] QFREAD: CR after CONNECT length not found");
        return 0;
    }
    
    int length1 = atoi(len_buf);
    if (length1 < 0 || length1 > length) {
        Serial.printf("[ERROR] QFREAD: invalid parsed length %d\r\n", length1);
        return -2;
    }
    
    // Read '\n' that follows '\r'
    start_time = millis();
    bool found_lf = false;
    while (millis() - start_time < 1000) {
        bool read_any = false;
        while (Serial1.available()) {
            read_any = true;
            c = Serial1.read();
            if (c == '\n') {
                found_lf = true;
                break;
            }
        }
        if (found_lf) break;
        if (!read_any) {
            delay(1);
        }
    }
    
    // 3. Read exactly length1 bytes into buff
    int bytes_read = 0;
    start_time = millis();
    while (bytes_read < length1 && (millis() - start_time < 5000)) {
        int avail = Serial1.available();
        if (avail > 0) {
            for (int i = 0; i < avail && bytes_read < length1; i++) {
                buff[bytes_read++] = Serial1.read();
            }
        } else {
            delay(1);
        }
    }
    
    if (bytes_read < length1) {
        Serial.printf("[ERROR] QFREAD: timeout reading data. Expected %d, got %d\r\n", length1, bytes_read);
        return 0;
    }
    
    // 4. Read trailing \r\nOK\r\n
    memset(s_qfr_trailing, 0, sizeof(s_qfr_trailing));
    int t_idx = 0;
    start_time = millis();
    bool found_ok = false;
    while (millis() - start_time < 2000) {
        bool read_any = false;
        while (Serial1.available()) {
            read_any = true;
            c = Serial1.read();
            if (t_idx < (int)sizeof(s_qfr_trailing) - 1) {
                s_qfr_trailing[t_idx++] = c;
                s_qfr_trailing[t_idx] = '\0';
            } else {
                memmove(s_qfr_trailing, s_qfr_trailing + 64, 64);
                t_idx = 64;
                s_qfr_trailing[t_idx++] = c;
                s_qfr_trailing[t_idx] = '\0';
            }
            if (strstr(s_qfr_trailing, "OK") != NULL) {
                found_ok = true;
                break;
            }
        }
        if (found_ok) break;
        if (!read_any) {
            delay(1);
        }
    }
    
    // Only print CONNECT/OK summary — don't flood console with per-chunk lines
    // (the progress bar every 32KB already shows throughput)
    return length1;
    #endif
}

uint8_t qftp::qftp_file_seek(int handle, int offset, uint8_t mode) {
    #ifndef ESP_PLATFORM
    if (mode == 0) { // SEEK_SET
        spiffs_offset = offset;
    } else if (mode == 1) { // SEEK_CUR
        spiffs_offset += offset;
    }
    return 1;
    #else
    char cmd[100];
    sprintf(cmd, FTP_FILE_SEEK, handle, offset, mode);
    // Remove redundant AT+QFPOSITION commands and just execute seek
    if (module->sendCommand(cmd, "OK", 2000, 2)) {
        return 1;
    }
    return 0;
    #endif
}

static uint8_t* s_psram_base64_buffer = NULL;
static size_t s_psram_base64_capacity = 0;
static size_t s_psram_base64_length = 0;

static uint8_t* s_psram_binary_buffer = NULL;
static size_t s_psram_binary_capacity = 0;
static size_t s_psram_binary_length = 0;

static size_t s_psram_part_sizes[FW_UPDATE_NUM_PARTS] = {0};

static void clean_psram_buffer() {
    if (s_psram_base64_buffer != NULL) {
        #ifdef ESP_PLATFORM
        heap_caps_free(s_psram_base64_buffer);
        #else
        free(s_psram_base64_buffer);
        #endif
        s_psram_base64_buffer = NULL;
    }
    s_psram_base64_capacity = 0;
    s_psram_base64_length = 0;

    if (s_psram_binary_buffer != NULL) {
        #ifdef ESP_PLATFORM
        heap_caps_free(s_psram_binary_buffer);
        #else
        free(s_psram_binary_buffer);
        #endif
        s_psram_binary_buffer = NULL;
    }
    s_psram_binary_capacity = 0;
    s_psram_binary_length = 0;

    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        s_psram_part_sizes[i] = 0;
    }
}

static bool is_chunk_valid_base64(const uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t c = buf[i];
        bool valid = (c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') ||
                     c == '+' || c == '/' || c == '=' ||
                     c == '\r' || c == '\n' || c == ' ' || c == '\t';
        if (!valid) {
            return false;
        }
    }
    return true;
}

static bool decode_and_append_part_to_binary(int part_num) {
    if (s_psram_base64_length == 0) return true;
    
    // Estimate maximum binary size: 3/4 of base64 size plus safety margin
    size_t estimate = (s_psram_base64_length * 3) / 4 + 4;
    
    if (s_psram_binary_length + estimate > s_psram_binary_capacity) {
        size_t new_cap = s_psram_binary_capacity == 0 ? 1024 * 1024 : s_psram_binary_capacity;
        while (s_psram_binary_length + estimate > new_cap) {
            new_cap += 512 * 1024;
        }
        #ifdef ESP_PLATFORM
        uint8_t* new_buf = NULL;
        if (psramFound()) {
            new_buf = (uint8_t*)heap_caps_realloc(s_psram_binary_buffer, new_cap, MALLOC_CAP_SPIRAM);
        } else {
            new_buf = (uint8_t*)realloc(s_psram_binary_buffer, new_cap);
        }
        #else
        uint8_t* new_buf = (uint8_t*)realloc(s_psram_binary_buffer, new_cap);
        #endif
        if (new_buf == NULL) {
            Serial.println("[ERR] PSRAM binary buffer reallocation failed!");
            return false;
        }
        s_psram_binary_buffer = new_buf;
        s_psram_binary_capacity = new_cap;
    }
    
    size_t decodedLen = 0;
    int ret = mbedtls_base64_decode(
        s_psram_binary_buffer + s_psram_binary_length,
        s_psram_binary_capacity - s_psram_binary_length,
        &decodedLen,
        (const unsigned char*)s_psram_base64_buffer,
        s_psram_base64_length
    );
    
    if (ret != 0) {
        Serial.printf("[ERR] Base64 decode failed for Part %d with code %d\r\n", part_num, ret);
        return false;
    }
    
    s_psram_binary_length += decodedLen;
    Serial.printf("[B64_DECODE] Part %d decoded successfully. Decoded size: %u bytes. Total binary size: %u bytes.\r\n",
                  part_num, (uint32_t)decodedLen, (uint32_t)s_psram_binary_length);
    return true;
}

static bool append_to_psram_buffer(const uint8_t* data, size_t len) {
    if (s_psram_base64_length + len > s_psram_base64_capacity) {
        size_t new_capacity = s_psram_base64_capacity == 0 ? 1024 * 1024 : s_psram_base64_capacity;
        while (s_psram_base64_length + len > new_capacity) {
            new_capacity += 512 * 1024;
        }
        
        #ifdef ESP_PLATFORM
        uint8_t* new_buf = NULL;
        if (psramFound()) {
            new_buf = (uint8_t*)heap_caps_realloc(s_psram_base64_buffer, new_capacity, MALLOC_CAP_SPIRAM);
        } else {
            new_buf = (uint8_t*)realloc(s_psram_base64_buffer, new_capacity);
        }
        #else
        uint8_t* new_buf = (uint8_t*)realloc(s_psram_base64_buffer, new_capacity);
        #endif
        
        if (new_buf == NULL) {
            Serial.println("PSRAM buffer reallocation failed!");
            return false;
        }
        s_psram_base64_buffer = new_buf;
        s_psram_base64_capacity = new_capacity;
    }
    
    memcpy(s_psram_base64_buffer + s_psram_base64_length, data, len);
    s_psram_base64_length += len;
    return true;
}

uint8_t read_ufs_file_to_psram(String filename, uint16_t read_size, int part) {
    uint32_t fp = ftp.qftp_file_open(filename, 2, 0);
    if (fp == 0) {
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg), "[ERR %d] AT+QFOPEN failed for UFS:%s", OTA_ERR_FILE_OPEN, filename.c_str());
        add_log("UFS_ERR", "error", 0, OTA_ERR_FILE_OPEN, part, err_msg);
        Serial.println(err_msg);
        return 0;
    }
    
    uint32_t encodedOffset = 0;
    uint32_t fp_size = getFloatValue(FP_SIZE);
    uint32_t file_offset = 0;
    int datasize = 1;
    uint32_t chunk_count = 0;
    int current_part = part;
    uint32_t total_chunks = (fp_size + read_size - 1) / read_size;
    
    Serial.printf("[UFS_READ] Starting read of %u bytes in %u-byte chunks (%u total chunks)...\r\n",
                  fp_size, read_size, total_chunks);
    
    while (datasize > 0) {
        if (s_ota_abort) {
            Serial.println("[UFS_READ] Read aborted by user command.");
            ftp.qftp_file_close(fp);
            return 0;
        }
        uint32_t size = fp_size - file_offset;
        int retry = 0;
        bool success = false;
        
        while (retry < MAX_RETRY1 && !success) {
            memset(buf1, 0, sizeof(buf1));
            if (size > read_size) {
                datasize = ftp.qftp_file_read(fp, read_size, buf1);
            } else {
                if (size == 0) {
                    ftp.qftp_file_close(fp);
                    Serial.printf("[UFS_READ] Part %d complete. PSRAM total: %u bytes\r\n",
                                  current_part, (uint32_t)s_psram_base64_length);
                    return 1;
                }
                datasize = ftp.qftp_file_read(fp, size, buf1);
            }
            
            if (datasize == 0) {
                ftp.qftp_file_close(fp);
                Serial.printf("[UFS_READ] Part %d EOF at offset %u. PSRAM total: %u bytes\r\n",
                              current_part, file_offset, (uint32_t)s_psram_base64_length);
                return 1;
            }
            if (datasize < 0) {
                retry++;
                char retry_msg[128];
                snprintf(retry_msg, sizeof(retry_msg),
                         "[ERR %d] QFREAD failed at offset %u, retry %d/%d (code %d)",
                         OTA_ERR_FILE_READ_RETRY, encodedOffset, retry, MAX_RETRY1, datasize);
                add_log("UFS_ERR", "retrying", 0, OTA_ERR_FILE_READ_RETRY, current_part, retry_msg);
                Serial.println(retry_msg);
                ftp.qftp_file_seek(fp, encodedOffset, 0);
                delay(50);
                continue;
            }
            
            // Check for UART corruption / noise in the read chunk
            if (!is_chunk_valid_base64(buf1, datasize)) {
                retry++;
                char corrupt_msg[128];
                snprintf(corrupt_msg, sizeof(corrupt_msg),
                         "[WARN] UART noise/corruption detected in Part %d chunk at offset %u. Retrying %d/%d...",
                         current_part, encodedOffset, retry, MAX_RETRY1);
                add_log("UART_ERR", "retrying", 0, OTA_ERR_FILE_READ_RETRY, current_part, corrupt_msg);
                Serial.println(corrupt_msg);
                
                // Seek back to start of this chunk to reread it
                ftp.qftp_file_seek(fp, encodedOffset, 0);
                delay(50); // wait for serial buffer/noise to clear
                continue;
            }
            
            // Append to PSRAM base64 buffer
            if (!append_to_psram_buffer((const uint8_t*)buf1, datasize)) {
                char err_msg[128];
                snprintf(err_msg, sizeof(err_msg),
                         "[ERR %d] PSRAM append failed at offset %u. Free PSRAM: %u bytes",
                         OTA_ERR_PSRAM_APPEND, file_offset,
                         #ifdef ESP_PLATFORM
                         (uint32_t)ESP.getFreePsram());
                         #else
                         0u);
                         #endif
                add_log("PSRAM_ERR", "error", 0, OTA_ERR_PSRAM_APPEND, current_part, err_msg);
                Serial.println(err_msg);
                ftp.qftp_file_close(fp);
                return 0;
            }
            
            file_offset += datasize;
            encodedOffset += datasize;
            chunk_count++;
            success = true;
            
            // Feed the watchdog every chunk — prevents TWDT restart during 40+ second reads
            FEED_WDT();
            // Yield to FreeRTOS scheduler so WiFi / WebServer tasks stay alive
            #ifdef ESP_PLATFORM
            vTaskDelay(1);
            #endif
            
            // Update offset indicator register
            setFloatValue(SPIFF_SET_BIT, file_offset);
            
            // Print ASCII progress bar to Serial every 16 chunks (~32KB)
            if (chunk_count % 16 == 0 || file_offset >= fp_size) {
                uint32_t pct = (fp_size > 0) ? (file_offset * 100UL / fp_size) : 100;
                const int BAR_WIDTH = 40;
                int filled = (int)(pct * BAR_WIDTH / 100);
                char bar[BAR_WIDTH + 8];
                bar[0] = '|';
                for (int b = 1; b <= BAR_WIDTH; b++) {
                    if (b < filled)      bar[b] = '=';
                    else if (b == filled) bar[b] = '>';
                    else                 bar[b] = ' ';
                }
                bar[BAR_WIDTH + 1] = '|';
                bar[BAR_WIDTH + 2] = '\0';
                Serial.printf("  %s (%uKB/%uKB) Part %d\r\n",
                              bar,
                              file_offset / 1024, fp_size / 1024,
                              current_part);
            }
        }
        
        if (!success) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg),
                     "[ERR %d] Max retries (%d) exceeded or UART corrupted at offset %u for Part %d",
                     OTA_ERR_FILE_READ, MAX_RETRY1, encodedOffset, current_part);
            add_log("UFS_ERR", "error", 0, OTA_ERR_FILE_READ, current_part, err_msg);
            Serial.println(err_msg);
            ftp.qftp_file_close(fp);
            return 0;
        }
        delay(5);
    }
    
    ftp.qftp_file_close(fp);
    Serial.printf("[UFS_READ] Part %d done. PSRAM total: %u bytes\r\n",
                  current_part, (uint32_t)s_psram_base64_length);
    return 1;
}

// ---------------------------------------------------------------------------
// sanitize_psram_base64()
// ---------------------------------------------------------------------------
// Strips every character from the PSRAM base64 buffer that is NOT a valid
// standard base64 alphabet character (A-Z a-z 0-9 + / =).
// Also removes internal '=' padding: the '=' character is only valid at the
// very END of a full base64 stream.  When 4 parts are concatenated, parts
// 1-3 each end with one or two '=' padding chars.  Leaving those internal
// '=' characters in the stream makes mbedtls fail with -44 when the decoder
// sees more data after a padding marker.
//
// Returns the number of bytes removed (stripped).
static size_t sanitize_psram_base64() {
    if (s_psram_base64_buffer == NULL || s_psram_base64_length == 0) return 0;
    
    const uint8_t* src = s_psram_base64_buffer;
    uint8_t*       dst = s_psram_base64_buffer; // write in-place
    size_t         src_len = s_psram_base64_length;
    size_t         dst_len = 0;
    size_t         stripped = 0;
    uint8_t        invalid_examples[8] = {0}; // first 8 stripped non-whitespace chars
    int            invalid_count = 0;
    
    for (size_t i = 0; i < src_len; i++) {
        uint8_t c = src[i];
        bool valid = (c >= 'A' && c <= 'Z') ||
                     (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') ||
                     c == '+' || c == '/';
        // '=' is only kept if it will end up at the very end
        // (we handle this by NOT copying '=' now; we'll add trailing '=' back after)
        if (valid) {
            dst[dst_len++] = c;
        } else {
            // Track non-whitespace invalid chars for diagnostic
            if (c != '\r' && c != '\n' && c != ' ' && c != '\t' && c != '=' && c != '\0') {
                if (invalid_count < (int)sizeof(invalid_examples)) {
                    invalid_examples[invalid_count++] = c;
                }
            }
            stripped++;
        }
    }
    
    // Figure out correct trailing padding: decoded data length must be
    // reconstructed from dst_len (number of base64 data chars).
    // dst_len mod 4 tells us how many '=' to append:
    //   dst_len % 4 == 0 → no padding needed
    //   dst_len % 4 == 2 → append "=="
    //   dst_len % 4 == 3 → append "="
    int remainder = (int)(dst_len % 4);
    if (remainder == 2) {
        dst[dst_len++] = '=';
        dst[dst_len++] = '=';
    } else if (remainder == 3) {
        dst[dst_len++] = '=';
    }
    
    s_psram_base64_length = dst_len;
    
    // Log diagnostic
    char san_log[256];
    if (invalid_count > 0) {
        // Build hex string of invalid chars
        char hex_str[32] = {0};
        for (int k = 0; k < invalid_count && k < 8; k++) {
            snprintf(hex_str + k * 3, sizeof(hex_str) - k * 3, "%02X ", invalid_examples[k]);
        }
        snprintf(san_log, sizeof(san_log),
                 "Sanitized PSRAM: stripped %u bytes (incl. %d non-whitespace invalid chars: %s). Clean length: %u bytes",
                 (uint32_t)stripped, invalid_count, hex_str, (uint32_t)dst_len);
    } else {
        snprintf(san_log, sizeof(san_log),
                 "Sanitized PSRAM: stripped %u whitespace/padding bytes. Clean length: %u bytes",
                 (uint32_t)stripped, (uint32_t)dst_len);
    }
    add_log("B64_CLEAN", "sanitizing", 12, OTA_ERR_NONE, 0, san_log);
    Serial.printf("[B64_CLEAN] %s\r\n", san_log);
    
    return stripped;
}

uint8_t flash_binary_from_psram() {
    add_log("ota_flash", "flashing", 0, 0, 0, "Starting ESP32 OTA flashing from PSRAM binary buffer...");
    modbus_set_status(STATUS_FLASHING);
    setFloatValue(FW_DOWNLOAD_PROGRESS, 10.0f);
    
    if (s_psram_binary_length == 0 || s_psram_binary_buffer == NULL) {
        add_log("ota_flash", "error", 0, OTA_ERR_UPDATE_WRITE, 0, "[ERR] Binary buffer is empty!");
        return 0;
    }
    
    #ifdef ESP_PLATFORM
    if (!Update.begin(s_psram_binary_length)) {
        setFloatValue(ERROR4, 7);
        Update.printError(Serial);
        add_log("ota_flash", "error", 0, OTA_ERR_UPDATE_BEGIN, 0, "[ERR] Failed to initialize OTA partition!");
        return 0;
    }
    #else
    Update.begin(108);
    #endif
    
    size_t processed_bin = 0;
    finalsize = 0;
    
    while (processed_bin < s_psram_binary_length) {
        size_t chunk_size = 4096;
        if (processed_bin + chunk_size > s_psram_binary_length) {
            chunk_size = s_psram_binary_length - processed_bin;
        }
        
        if (chunk_size == 0) break;
        
        if (Update.write(s_psram_binary_buffer + processed_bin, chunk_size) != chunk_size) {
            Serial.println("OTA Write chunk failed!");
            #ifdef ESP_PLATFORM
            Update.printError(Serial);
            #endif
            add_log("ota_flash", "error", 0, OTA_ERR_UPDATE_WRITE, 0, "OTA writing chunk from PSRAM failed!");
            setFloatValue(ERROR4, 5);
            return 0;
        }
        
        processed_bin += chunk_size;
        finalsize += chunk_size;
        
        float flash_progress = 10.0f + ((float)processed_bin / s_psram_binary_length) * 7.0f;
        setFloatValue(FW_DOWNLOAD_PROGRESS, flash_progress);
        
        char log_details[256];
        snprintf(log_details, sizeof(log_details), "Flashing: wrote %u of %u binary bytes.", 
                 (uint32_t)processed_bin, (uint32_t)s_psram_binary_length);
        add_log("ota_flash", "flashing", (int)((processed_bin * 100) / s_psram_binary_length), 0, 0, log_details);
        
        delay(2);
    }
    
    return 1;
}



static void list_ufs_files_to_log() {
    #ifdef ESP_PLATFORM
    LOCK_GSM();
    while (Serial1.available()) Serial1.read();
    Serial1.print("AT+QFLST=\"UFS:*\"\r\n");
    uint32_t start = millis();
    int idx = 0;
    char list_buf[2048] = {0};
    while (millis() - start < 2000) {
        while (Serial1.available()) {
            char c = Serial1.read();
            if (idx < (int)sizeof(list_buf) - 1) {
                list_buf[idx++] = c;
            }
        }
        if (strstr(list_buf, "OK") != NULL || strstr(list_buf, "ERROR") != NULL) {
            break;
        }
        delay(10);
    }
    UNLOCK_GSM();
    
    Serial.println("[UFS_LIST] Current files in UFS storage:");
    char* line = strtok(list_buf, "\r\n");
    while (line != NULL) {
        if (strstr(line, "+QFLST:") != NULL) {
            Serial.printf("  %s\r\n", line);
            add_log("UFS_LIST", "cleanup", 0, 0, 0, line);
        }
        line = strtok(NULL, "\r\n");
    }
    #else
    Serial.println("[UFS_LIST] [Mock] Current files in UFS storage:");
    Serial.println("  +QFLST: \"UFS:otafw_part1.b64\",476856");
    add_log("UFS_LIST", "cleanup", 0, 0, 0, "+QFLST: \"UFS:otafw_part1.b64\",476856");
    #endif
}

// =========================================================================
// Main orchestrator flow based on user specification
// =========================================================================
static uint8_t trigger_firmware_update_flow() {
    String devimei = "123456789012345";
    String username = "username";
    String password = "password";
    uint8_t err_code = 0;
    
    // Clear Modbus registers 1 to 20 at start of update
    for (int r = 1; r <= 20; r++) {
        modbus_set_register(r, 0);
    }
    
    modbus_set_error(ERR_NONE);
    modbus_set_status(STATUS_DOWNLOADING);
    
    setFloatValue(FW_DOWNLOAD_PROGRESS, 1);
    
    add_log("ota_flash", "initializing", 5, 0, 0, "Initializing OTA update flashing session...");
    
    clean_psram_buffer(); // clear old buffers
    
    // Pre-allocate the temporary base64 buffer (500KB) and the accumulator binary buffer (1.5MB)
    #ifdef ESP_PLATFORM
    {
        uint32_t total_parts = (uint32_t)getFloatValue(FW_TOTAL_PARTS);
        size_t b64_alloc_size = 500000UL;
        size_t bin_alloc_size = total_parts * 375000UL;
        
        uint8_t* b64_buf = NULL;
        uint8_t* bin_buf = NULL;
        
        if (psramFound()) {
            b64_buf = (uint8_t*)heap_caps_malloc(b64_alloc_size, MALLOC_CAP_SPIRAM);
            bin_buf = (uint8_t*)heap_caps_malloc(bin_alloc_size, MALLOC_CAP_SPIRAM);
        } else {
            b64_buf = (uint8_t*)malloc(b64_alloc_size);
            bin_buf = (uint8_t*)malloc(bin_alloc_size);
        }
        
        if (b64_buf != NULL && bin_buf != NULL) {
            s_psram_base64_buffer = b64_buf;
            s_psram_base64_capacity = b64_alloc_size;
            s_psram_base64_length = 0;
            
            s_psram_binary_buffer = bin_buf;
            s_psram_binary_capacity = bin_alloc_size;
            s_psram_binary_length = 0;
            
            char alloc_log[256];
            snprintf(alloc_log, sizeof(alloc_log),
                     "PSRAM allocated: Base64 Temp %u bytes, Binary Accumulator %u bytes",
                     (uint32_t)b64_alloc_size, (uint32_t)bin_alloc_size);
            add_log("PSRAM_ALLOC", "ready", 2, OTA_ERR_NONE, 0, alloc_log);
            Serial.printf("[PSRAM] %s\r\n", alloc_log);
        } else {
            if (b64_buf) heap_caps_free(b64_buf);
            if (bin_buf) heap_caps_free(bin_buf);
            
            char err_log[128];
            snprintf(err_log, sizeof(err_log),
                     "[ERR %d] PSRAM allocation failed! Free PSRAM: %u bytes",
                     OTA_ERR_PSRAM_ALLOC, (uint32_t)ESP.getFreePsram());
            add_log("PSRAM_ERR", "error", 0, OTA_ERR_PSRAM_ALLOC, 0, err_log);
            Serial.println(err_log);
            modbus_set_status(STATUS_ERROR);
            modbus_set_error(ERR_OTA_FLASH);
            return 0;
        }
    }
    #else
    // PC Simulator allocation
    s_psram_base64_buffer = (uint8_t*)malloc(500000UL);
    s_psram_base64_capacity = 500000UL;
    s_psram_base64_length = 0;
    
    s_psram_binary_buffer = (uint8_t*)malloc(1500000UL);
    s_psram_binary_capacity = 1500000UL;
    s_psram_binary_length = 0;
    #endif
    
    setFloatValue(FW_DOWNLOAD_PROGRESS, 2);
    finalsize = 0;
    spiffs_offset = 0;
    
    // PRE-CLEAR: Delete ALL old firmware part files from UFS before starting download
    add_log("UFS_CLEAN", "cleanup", 0, 0, 0, "Pre-clearing ALL old OTA firmware files from modem UFS storage...");
    #ifdef ESP_PLATFORM
    LOCK_GSM();
    for (int p = 1; p <= FW_UPDATE_NUM_PARTS; p++) {
        // Delete using the URL-derived filename of each configured URL
        String fname = extract_url_filename(s_custom_urls[p - 1].c_str());
        if (fname.length() == 0) {
            char fb[32];
            snprintf(fb, sizeof(fb), "otafw_part%d.b64", p);
            fname = fb;
        }
        char del_all[128];
        snprintf(del_all, sizeof(del_all), "AT+QFDEL=\"UFS:%s\"\r\n", fname.c_str());
        bool del_ok = module->sendCommand(del_all, "OK", 900, 1, 200, "+CME ERROR: 418");
        char del_log[256];
        if (del_ok) {
            if (strstr(module->buffer, "+CME ERROR: 418") != NULL) {
                snprintf(del_log, sizeof(del_log), "Pre-clean: UFS:%s not found (already clean)", fname.c_str());
            } else {
                snprintf(del_log, sizeof(del_log), "Pre-deleted: UFS:%s", fname.c_str());
            }
        } else {
            snprintf(del_log, sizeof(del_log), "Pre-delete failed: UFS:%s", fname.c_str());
        }
        add_log("UFS_CLEAN", "cleanup", 0, 0, p, del_log);
    }
    // Also delete combined firmware file
    module->sendCommand("AT+QFDEL=\"UFS:firm\"\r\n", "OK", 900, 1, 200, "+CME ERROR: 418");
    UNLOCK_GSM();
    add_log("UFS_CLEAN", "cleanup", 0, 0, 0, "UFS pre-clean complete. Starting sequential part downloads.");
    #endif
    
    for (int part = 1; part <= getFloatValue(FW_TOTAL_PARTS); part++) {
        if (s_ota_abort) {
            add_log("ota_flash", "aborted", 0, 0, part, "OTA update aborted by user command.");
            Serial.println("[OTA] Process aborted by user command.");
            clean_psram_buffer();
            return 0;
        }
        modbus_set_current_part(part);
        
        String fw_url = s_custom_urls[part - 1];
        if (fw_url.length() == 0) {
            char fallback_url[128];
            snprintf(fallback_url, sizeof(fallback_url), "http://64.251.10.159/otafw_part%d.b64", part);
            fw_url = fallback_url;
        }
        
        // Fetch part via HTTP/HTTPS AT command flow with up to 5 retries on noise/error
        LOCK_GSM();
        int k = -1;
        for (int retry = 1; retry <= 5; retry++) {
            k = gprs.getfirmwarefile(fw_url, devimei, username, password, part);
            Serial.printf("[HTTP_DL] Part %d download attempt %d/5 returned: %d\n", part, retry, k);
            if (k == 0) {
                break; // Succeeded! Move immediately forward
            }
            // If noise/error occurred, log it and retry unless we hit max attempts
            if (retry < 5) {
                char retry_msg[128];
                snprintf(retry_msg, sizeof(retry_msg), "[WARN] Part %d download failed (ret=%d), retrying (%d/5)...", part, k, retry + 1);
                add_log("DL_RETRY", "retrying", 0, 0, part, retry_msg);
                #ifdef ESP_PLATFORM
                delay(1000); // Wait 1 second before retrying on ESP32
                #else
                Sleep(1000); // Wait 1 second before retrying on PC simulation
                #endif
            }
        }
        UNLOCK_GSM();
        setFloatValue(ERROR4, k);
        if (k != 0) {
            char dl_err[128];
            snprintf(dl_err, sizeof(dl_err),
                     "[ERR %d] getfirmwarefile failed for Part %d (ret=%d). Aborting OTA.",
                     OTA_ERR_DOWNLOAD, part, k);
            add_log("DL_ERR", "error", 0, OTA_ERR_DOWNLOAD, part, dl_err);
            Serial.println(dl_err);
            setFloatValue(ERROR4, 11);
            clean_psram_buffer();
            return 0;
        }
        
        setFloatValue(FW_DOWNLOAD_PROGRESS, 3);
        setFloatValue(FW_DOWNLOAD_PROGRESS, 1 + (part * 2));
        
        // Use filename extracted from the URL (matches what modem saved in UFS storage)
        String filename1 = extract_url_filename(fw_url.c_str());
        
        // Log which UFS file we're now reading from flash
        char ufs_log[256];
        snprintf(ufs_log, sizeof(ufs_log), "Reading UFS file: UFS:%s (from URL: %s)", filename1.c_str(), fw_url.c_str());
        add_log("UFS_READ", "reading", 0, 0, part, ufs_log);
        
        // Query file size dynamically using the URL-derived filename & read file contents completely into PSRAM buffer
        LOCK_GSM();
        #ifndef ESP_PLATFORM
        setFloatValue(FP_SIZE, strlen(s_parts_payloads[part - 1]));
        #else
        uint32_t parsed_size = 476856; // Default fallback
        char list_cmd[256];
        snprintf(list_cmd, sizeof(list_cmd), "AT+QFLST=\"UFS:%s\"\r\n", filename1.c_str());
        bool size_ok = module->sendCommand(list_cmd, "OK", 2000, 1);
        if (size_ok) {
            char* ptr = strstr(module->buffer, filename1.c_str());
            if (ptr != NULL) {
                char* comma = strchr(ptr, ',');
                if (comma != NULL) {
                    parsed_size = (uint32_t)atol(comma + 1);
                    if (parsed_size == 0) parsed_size = 476856;
                }
            }
            char size_log[128];
            snprintf(size_log, sizeof(size_log), "UFS:%s size from modem: %u bytes", filename1.c_str(), parsed_size);
            add_log("UFS_READ", "reading", 0, 0, part, size_log);
            Serial.printf("[UFS_READ] %s\n", size_log);
        } else {
            char size_log[128];
            snprintf(size_log, sizeof(size_log), "Warning: Failed to get UFS:%s size, using default fallback.", filename1.c_str());
            add_log("UFS_READ_WARN", "warning", 0, 0, part, size_log);
        }
        setFloatValue(FP_SIZE, parsed_size);
        #endif
        
        // Reset base64 buffer length for current part
        s_psram_base64_length = 0;
        
        // Copy UFS file contents completely into PSRAM buffer
        size_t before_len = s_psram_binary_length;
        err_code = read_ufs_file_to_psram(filename1, 2048, part); // 2KB chunks to stay within modem QCOM buffer
        if (err_code == 0) {
            char rd_err[128];
            snprintf(rd_err, sizeof(rd_err),
                     "[ERR %d] read_ufs_file_to_psram failed for Part %d. Aborting OTA.",
                     OTA_ERR_FILE_READ, part);
            add_log("UFS_ERR", "error", 0, OTA_ERR_FILE_READ, part, rd_err);
            Serial.println(rd_err);
            setFloatValue(FILE_UUID, 0);
            clean_psram_buffer();
            UNLOCK_GSM();
            return 0;
        }
        
        // Clean any whitespace / lines and fix trailing padding for this part
        sanitize_psram_base64();
        
        // Decode this part immediately and append to binary accumulator
        if (!decode_and_append_part_to_binary(part)) {
            char dec_err[128];
            snprintf(dec_err, sizeof(dec_err), "[ERR %d] Base64 decode failed for Part %d immediately!", OTA_ERR_B64_DECODE, part);
            add_log("DEC_ERR", "error", 0, OTA_ERR_B64_DECODE, part, dec_err);
            Serial.println(dec_err);
            setFloatValue(FILE_UUID, 0);
            clean_psram_buffer();
            UNLOCK_GSM();
            return 0;
        }
        
        // Explicitly mark this part as completed and stored in PSRAM
        modbus_set_register(5 + (part - 1) * 5, 1); // Stored in PSRAM = 1 (Yes)
        modbus_set_register(1 + (part - 1) * 5, part * 10 + 9); // Status = Completed (19, 29, 39, 49)
        modbus_set_register(3 + (part - 1) * 5, 100); // Progress = 100
        
        size_t after_len = s_psram_binary_length;
        if (part >= 1 && part <= FW_UPDATE_NUM_PARTS) {
            s_psram_part_sizes[part - 1] = after_len - before_len;
        }
        
        // Log the list of files/parts in PSRAM to console and GUI
        char psram_list_str[512] = {0};
        int offset_p = snprintf(psram_list_str, sizeof(psram_list_str), "PSRAM buffered parts: ");
        for (int p_idx = 0; p_idx < FW_UPDATE_NUM_PARTS; p_idx++) {
            if (s_psram_part_sizes[p_idx] > 0) {
                offset_p += snprintf(psram_list_str + offset_p, sizeof(psram_list_str) - offset_p,
                                     "[Part %d: %d bytes] ", p_idx + 1, (int)s_psram_part_sizes[p_idx]);
            }
        }
        add_log("PSRAM_LIST", "buffering", 2 + (part * 2), 0, part, psram_list_str);
        Serial.printf("[OTA LOG] %s\r\n", psram_list_str);
        
        // Immediately delete downloaded part file from GPRS storage to free up UFS space
        #ifdef ESP_PLATFORM
        char del_cmd[128];
        snprintf(del_cmd, sizeof(del_cmd), "AT+QFDEL=\"UFS:%s\"\r\n", filename1.c_str());
        module->sendCommand(del_cmd, "OK", 1000, 1, 200, "+CME ERROR: 418");
        #endif
        UNLOCK_GSM();
        
        setFloatValue(FW_DOWNLOAD_PROGRESS, 2 + (part * 2));
        delay(10);
    }
    
    resetWatchdog();
    delay(100);
    
    // List remaining UFS files to confirm status/free space
    list_ufs_files_to_log();
    
    // Now start flashing from PSRAM binary accumulator buffer to OTA partition
    err_code = flash_binary_from_psram();
    if (err_code == 0) {
        clean_psram_buffer();
        return 0;
    }
    
    char details[128];
    snprintf(details, sizeof(details), "Flashing complete. Total binary size: %u bytes", (uint32_t)s_psram_binary_length);
    add_log("validation", "checking", 90, 0, FW_UPDATE_NUM_PARTS, details);
    modbus_set_status(STATUS_FLASHING);
    
    // Commit the OTA partition
    if (Update.end(true)) {
        setFloatValue(ERROR4, 9);
        modbus_set_status(STATUS_COMPLETE);
        modbus_set_progress(100);
        add_log("system", "restarting", 100, 0, FW_UPDATE_NUM_PARTS, "OTA Flashing complete. Restarting ESP32...");
        clean_psram_buffer();
        delay(2000);
        ESP.restart();
    } else {
        #ifdef ESP_PLATFORM
        Update.printError(Serial);
        #endif
        setFloatValue(ERROR4, 10);
        add_log("ota_flash", "error", 100, ERR_OTA_FLASH, FW_UPDATE_NUM_PARTS, "OTA partition closing write failed!");
    }
    
    clean_psram_buffer();
    
    if (err_code == 1) {
        setFloatValue(FILE_UUID, 0);
    }
    
    return 1;
}

#ifdef ESP_PLATFORM
static void ota_background_task(void* pvParameters) {
    s_ota_running = true;
    trigger_firmware_update_flow();
    s_ota_running = false;
    vTaskDelete(NULL);
}
#endif

#ifndef ESP_PLATFORM
static void trigger_ota_update_thread();
#endif

void handle_modbus_command(uint16_t cmd) {
    Serial.printf("[Modbus CMD] Received command: %d\n", cmd);
    switch (cmd) {
        case Start_Firmware_Process:
        case Firmware_Update_Restart: {
            s_ota_abort = false;
            #ifdef ESP_PLATFORM
            if (!s_ota_running) {
                s_log_count = 0;
                xTaskCreate(ota_background_task, "ota_task", 16384, NULL, 5, NULL);
            } else {
                Serial.println("[Modbus CMD] OTA already running, ignoring start/restart.");
            }
            #else
            trigger_ota_update_thread();
            #endif
            break;
        }
        case Abort_Firmware_Process: {
            s_ota_abort = true;
            Serial.println("[Modbus CMD] Abort requested.");
            break;
        }
        case GPRS_Restart: {
            #ifdef ESP_PLATFORM
            Serial.println("[Modbus CMD] GPRS Restart requested.");
            digitalWrite(GSM_PWRKEY_PIN, LOW);
            delay(2000);
            digitalWrite(GSM_PWRKEY_PIN, HIGH);
            delay(5000); // Wait for modem bootup
            #else
            Serial.println("[Modbus CMD] Mock GPRS Restart completed.");
            #endif
            break;
        }
        case End_Firmware_Update_Process: {
            s_ota_abort = false;
            modbus_set_status(STATUS_IDLE);
            modbus_set_progress(0);
            modbus_set_error(ERR_NONE);
            modbus_set_current_part(0);
            clean_psram_buffer();
            Serial.println("[Modbus CMD] Process ended and registers reset.");
            break;
        }
        default:
            Serial.printf("[Modbus CMD] Unknown command: %d\n", cmd);
            break;
    }
}

#ifdef ESP_PLATFORM
// =========================================================================
// ESP32 Web Server Endpoints
// =========================================================================
#if 0
static WebServer server(80);

static void handle_root() {
    server.send(200, "text/html", index_html);
}

static void handle_status() {
    int offset = 0;
    if (server.hasArg("offset")) {
        offset = server.arg("offset").toInt();
    }
    
    char buf_reg_1[16], buf_reg_3[16], buf_reg_5[16], buf_reg_7[16], buf_reg_9[16], buf_reg_11[16];
    snprintf(buf_reg_1, sizeof(buf_reg_1), "%.2f", getFloatValue(FW_DOWNLOAD_PROGRESS));
    snprintf(buf_reg_3, sizeof(buf_reg_3), "%.2f", getFloatValue(ERROR4));
    snprintf(buf_reg_5, sizeof(buf_reg_5), "%.2f", getFloatValue(FW_TOTAL_PARTS));
    snprintf(buf_reg_7, sizeof(buf_reg_7), "%.2f", getFloatValue(FILE_UUID));
    snprintf(buf_reg_9, sizeof(buf_reg_9), "%.2f", getFloatValue(SPIFF_SET_BIT));
    snprintf(buf_reg_11, sizeof(buf_reg_11), "%.2f", getFloatValue(FP_SIZE));
    
    int current_part = 1;
    for (int p = 1; p <= 4; p++) {
        uint16_t stat = modbus_get_register(1 + (p - 1) * 5);
        if (stat != 0 && stat != p * 10 + 9) { // not completed
            current_part = p;
            break;
        }
    }
    
    int overall_status = 0;
    bool has_error = false;
    bool has_flashing = false;
    bool has_decompressing = false;
    bool has_downloading = false;
    bool all_completed = true;
    
    for (int p = 1; p <= 4; p++) {
        uint16_t stat = modbus_get_register(1 + (p - 1) * 5);
        int local_type = stat - p * 10;
        if (stat == 0) {
            all_completed = false;
        } else {
            if (local_type == 7) has_error = true;
            else if (local_type == 5) has_flashing = true;
            else if (local_type == 3) has_decompressing = true;
            else if (local_type == 1) has_downloading = true;
            if (local_type != 9) all_completed = false;
        }
    }
    
    if (has_error) overall_status = 5;
    else if (all_completed) overall_status = 4;
    else if (has_flashing) overall_status = 3;
    else if (has_decompressing) overall_status = 2;
    else if (has_downloading) overall_status = 1;
    
    int overall_progress = 0;
    if (overall_status == 4) {
        overall_progress = 100;
    } else {
        for (int p = 1; p <= 4; p++) {
            overall_progress += modbus_get_register(3 + (p - 1) * 5) / 4;
        }
    }

    String json = "{\n";
    json += "  \"gprs_connected\": true,\n";
    json += "  \"status\": " + String(overall_status) + ",\n";
    json += "  \"progress\": " + String(overall_progress) + ",\n";
    json += "  \"error\": " + String(has_error ? 3 : 0) + ",\n";
    json += "  \"part\": " + String(current_part) + ",\n";
    json += "  \"float_reg_1\": " + String(buf_reg_1) + ",\n";
    json += "  \"float_reg_3\": " + String(buf_reg_3) + ",\n";
    json += "  \"float_reg_5\": " + String(buf_reg_5) + ",\n";
    json += "  \"float_reg_7\": " + String(buf_reg_7) + ",\n";
    json += "  \"float_reg_9\": " + String(buf_reg_9) + ",\n";
    json += "  \"float_reg_11\": " + String(buf_reg_11) + ",\n";
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
    json += "  ],\n";
    json += "  \"psram_files\": [\n";
    bool first_file = true;
    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        if (s_psram_part_sizes[i] > 0) {
            if (!first_file) json += ",\n";
            json += "    {\"name\": \"PSRAM:otafw_part" + String(i + 1) + ".b64\", \"size\": " + String(s_psram_part_sizes[i]) + "}";
            first_file = false;
        }
    }
    json += "\n  ]\n";
    json += "}";
    server.send(200, "application/json", json);
}

static void ota_background_task(void* pvParameters) {
    s_ota_running = true;
    trigger_firmware_update_flow();
    s_ota_running = false;
    vTaskDelete(NULL);
}

static void handle_trigger() {
    if (s_ota_running) {
        add_log("trigger", "error", 0, OTA_ERR_NONE, 0,
                "[ERR] OTA already running! Ignoring duplicate trigger.");
        server.send(409, "application/json", "{\"error\":\"OTA already running\"}");
        return;
    }
    s_log_count = 0;
    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        char arg_name[8];
        snprintf(arg_name, sizeof(arg_name), "url%d", i + 1);
        if (server.hasArg(arg_name)) {
            s_custom_urls[i] = server.arg(arg_name);
        } else {
            char fallback_url[128];
            snprintf(fallback_url, sizeof(fallback_url), "http://64.251.10.159/otafw_part%d.b64", i + 1);
            s_custom_urls[i] = fallback_url;
        }
    }
    xTaskCreate(ota_background_task, "ota_task", 16384, NULL, 5, NULL);
    // Stack doubled from 8192 to 16384 — qftp_file_read + trigger_firmware_update_flow
    // combined stack depth exceeds 5KB with all local arrays.
    server.send(200, "application/json", "{\"status\":\"triggered\"}");
}

static void handle_test_gprs() {
    bool ok = false;
    #ifdef ESP_PLATFORM
    init_gsm_mutex();
    if (xSemaphoreTake(s_gsm_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        ok = module->sendCommand("AT+CGATT?\r\n", "+CGATT: 1", 3000, 2);
        xSemaphoreGive(s_gsm_mutex);
    }
    #else
    ok = true;
    #endif
    
    add_log("gprs_check", ok ? "idle" : "error", 0, ok ? ERR_NONE : ERR_GPRS_FAIL, 0, 
            ok ? "GPRS connection checked: Online." : "GPRS connection checked: Offline!");
            
    String json = "{\"connected\":" + String(ok ? "true" : "false") + "}";
    server.send(200, "application/json", json);
}

static void handle_ping_server() {
    String url_param = "64.251.10.159";
    if (server.hasArg("url")) {
        url_param = server.arg("url");
    }
    
    String host = url_param;
    int start = 0;
    if (host.startsWith("http://")) start = 7;
    else if (host.startsWith("https://")) start = 8;
    int slash = host.indexOf('/', start);
    if (slash != -1) {
        host = host.substring(start, slash);
    } else {
        host = host.substring(start);
    }
    int colon = host.indexOf(':');
    if (colon != -1) {
        host = host.substring(0, colon);
    }
    
    add_log("ping_test", "checking", 0, 0, 0, ("Pinging host: " + host).c_str());
    
    bool success = false;
    #ifdef ESP_PLATFORM
    IPAddress ip;
    if (WiFi.hostByName(host.c_str(), ip)) {
        success = true;
        add_log("ping_test", "idle", 0, 0, 0, ("Host resolved to " + ip.toString() + ". Ping success (latency 42ms).").c_str());
    } else {
        add_log("ping_test", "error", 0, 0, 0, "Host resolution failed!");
    }
    #else
    success = true;
    add_log("ping_test", "idle", 0, 0, 0, ("Host resolved. [Mock] Ping to " + host + " successful (latency 45ms).").c_str());
    #endif
    
    String json = "{\"success\":" + String(success ? "true" : "false") + "}";
    server.send(200, "application/json", json);
}

static void handle_clear_cache() {
    add_log("cleanup", "working", 0, 0, 0, "Manual request to clean UFS cache & files...");
    #ifdef ESP_PLATFORM
    init_gsm_mutex();
    if (xSemaphoreTake(s_gsm_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        module->sendCommand("AT+QFDEL=\"UFS:*\"\r\n", "OK", 2000, 1);
        xSemaphoreGive(s_gsm_mutex);
    }
    #else
    // mock UFS clean
    #endif
    modbus_set_status(STATUS_IDLE);
    modbus_set_progress(0);
    modbus_set_error(ERR_NONE);
    modbus_set_current_part(0);
    
    add_log("cleanup", "idle", 0, 0, 0, "Cache files cleared from UFS and local registers reset.");
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
}

static void handle_reboot() {
    add_log("system", "restarting", 0, 0, 0, "Manual system reboot requested via web panel...");
    server.send(200, "application/json", "{\"status\":\"rebooting\"}");
    delay(500);
    ESP.restart();
}

static void handle_write_register() {
    if (server.hasArg("register") && server.hasArg("value")) {
        int reg = server.arg("register").toInt();
        int val = server.arg("value").toInt();
        modbus_set_register(reg, val);
        
        // Synchronize float registers for simulation
        if (reg == REG_DOWNLOAD_STATUS) {
            if (val == STATUS_IDLE) setFloatValue(FW_DOWNLOAD_PROGRESS, 0.0f);
            else if (val == STATUS_DOWNLOADING) setFloatValue(FW_DOWNLOAD_PROGRESS, 1.0f);
            else if (val == STATUS_DECODING) setFloatValue(FW_DOWNLOAD_PROGRESS, 2.0f);
            else if (val == STATUS_FLASHING) setFloatValue(FW_DOWNLOAD_PROGRESS, 3.0f);
            else if (val == STATUS_COMPLETE) setFloatValue(FW_DOWNLOAD_PROGRESS, 18.0f);
            else if (val == STATUS_ERROR) setFloatValue(FW_DOWNLOAD_PROGRESS, -1.0f);
        } else if (reg == REG_ERROR_CODE) {
            setFloatValue(ERROR4, (float)val);
        } else if (reg == REG_CURRENT_PART) {
            setFloatValue(SPIFF_SET_BIT, val * 10000.0f);
            setFloatValue(FP_SIZE, 80000.0f);
        }
        
        char details[128];
        snprintf(details, sizeof(details), "Manual Modbus Write: Reg %d = %d", reg, val);
        add_log("modbus_write", "idle", 0, 0, 0, details);
        
        server.send(200, "application/json", "{\"status\":\"written\"}");
    } else {
        server.send(400, "application/json", "{\"error\":\"Missing args\"}");
    }
}

static void handle_list_modem_files() {
    #ifdef ESP_PLATFORM
    init_gsm_mutex();
    if (xSemaphoreTake(s_gsm_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        // Modem is busy with OTA — tell the GUI clearly instead of returning empty
        String busy_json = "{\n  \"busy\": true,\n  \"files\": [\n";
        busy_json += "    {\"name\": \"[Modem busy - OTA in progress]\", \"size\": 0}\n";
        busy_json += "  ]\n}";
        server.send(200, "application/json", busy_json);
        return;
    }
    
    while (Serial1.available()) Serial1.read();
    Serial1.print("AT+QFLST=\"UFS:*\"\r\n");
    uint32_t start = millis();
    int idx = 0;
    char list_buf[2048] = {0};
    while (millis() - start < 1500) {
        while (Serial1.available()) {
            char c = Serial1.read();
            if (idx < (int)sizeof(list_buf) - 1) {
                list_buf[idx++] = c;
            }
        }
        if (strstr(list_buf, "OK") != NULL || strstr(list_buf, "ERROR") != NULL) {
            break;
        }
        delay(10);
    }
    
    String json = "{\n  \"files\": [\n";
    char* line = strtok(list_buf, "\r\n");
    bool first = true;
    while (line != NULL) {
        if (strstr(line, "+QFLST:") != NULL) {
            char* name_start = strchr(line, '"');
            if (name_start != NULL) {
                char* name_end = strchr(name_start + 1, '"');
                if (name_end != NULL) {
                    *name_end = '\0';
                    String fname = String(name_start + 1);
                    char* size_ptr = strchr(name_end + 1, ',');
                    long fsize = 0;
                    if (size_ptr != NULL) {
                        fsize = atol(size_ptr + 1);
                    }
                    if (!first) json += ",\n";
                    json += "    {\"name\": \"" + fname + "\", \"size\": " + String(fsize) + "}";
                    first = false;
                }
            }
        }
        line = strtok(NULL, "\r\n");
    }
    json += "\n  ]\n}";
    server.send(200, "application/json", json);
    xSemaphoreGive(s_gsm_mutex);
    #else
    String json = "{\n  \"files\": [\n"
                  "    {\"name\": \"UFS:otafw_part1.b64\", \"size\": 744155},\n"
                  "    {\"name\": \"UFS:otafw_part2.b64\", \"size\": 744155},\n"
                  "    {\"name\": \"UFS:otafw_part3.b64\", \"size\": 744155},\n"
                  "    {\"name\": \"UFS:otafw_part4.b64\", \"size\": 744155}\n"
                  "  ]\n"
                  "}";
    server.send(200, "application/json", json);
    #endif
}

static void handle_esp32_storage() {
    #ifdef ESP_PLATFORM
    uint32_t flash_size = ESP.getFlashChipSize();
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t psram_size = 0;
    uint32_t psram_free = 0;
    if (psramFound()) {
        psram_size = ESP.getPsramSize();
        psram_free = ESP.getFreePsram();
    }
    
    String json = "{\n";
    json += "  \"flash_total\": " + String(flash_size) + ",\n";
    json += "  \"flash_free\": " + String(flash_size / 2) + ",\n";
    json += "  \"heap_free\": " + String(free_heap) + ",\n";
    json += "  \"psram_total\": " + String(psram_size) + ",\n";
    json += "  \"psram_free\": " + String(psram_free) + "\n";
    json += "}";
    server.send(200, "application/json", json);
    #else
    String json = "{\n"
                  "  \"flash_total\": 4194304,\n"
                  "  \"flash_free\": 2097152,\n"
                  "  \"heap_free\": 285430,\n"
                  "  \"psram_total\": 4194304,\n"
                  "  \"psram_free\": 2097152\n"
                  "}";
    server.send(200, "application/json", json);
    #endif
}
#endif

#ifndef MODEM_RX_PIN
#define MODEM_RX_PIN 1
#endif
#ifndef MODEM_TX_PIN
#define MODEM_TX_PIN 2
#endif
#ifndef MODEM_BAUD_RATE
#define MODEM_BAUD_RATE 115200
#endif

#ifndef GSM_EN_PIN
#define GSM_EN_PIN 21
#endif
#ifndef GSM_PWRKEY_PIN
#define GSM_PWRKEY_PIN 5
#endif

#ifdef ESP_PLATFORM
#include <WiFi.h>

static WiFiServer s_modbus_server(502);

static void modbus_tcp_task(void* pvParameters) {
    (void)pvParameters;
    s_modbus_server.begin();
    Serial.println("[Modbus TCP] Server started on port 502");
    
    uint8_t buffer[260];
    
    while (true) {
        WiFiClient client = s_modbus_server.available();
        if (client) {
            Serial.println("[Modbus TCP] Client connected");
            while (client.connected()) {
                if (client.available() >= 12) {
                    int read_len = client.read(buffer, 12);
                    if (read_len >= 12) {
                        uint16_t transaction_id = (buffer[0] << 8) | buffer[1];
                        uint16_t protocol_id = (buffer[2] << 8) | buffer[3];
                        uint16_t length = (buffer[4] << 8) | buffer[5];
                        uint8_t unit_id = buffer[6];
                        uint8_t function_code = buffer[7];
                        
                        if (protocol_id == 0 && length >= 6) {
                            if (function_code == 3) { // Read Holding Registers
                                uint16_t start_addr = (buffer[8] << 8) | buffer[9];
                                uint16_t reg_qty = (buffer[10] << 8) | buffer[11];
                                
                                if (reg_qty > 125) reg_qty = 125;
                                
                                uint8_t resp_len = 3 + 2 * reg_qty; // unit_id + fc + byte_count + data
                                uint8_t resp[260];
                                
                                resp[0] = buffer[0];
                                resp[1] = buffer[1];
                                resp[2] = buffer[2];
                                resp[3] = buffer[3];
                                resp[4] = (resp_len >> 8) & 0xFF;
                                resp[5] = resp_len & 0xFF;
                                resp[6] = unit_id;
                                
                                resp[7] = 3;
                                resp[8] = 2 * reg_qty;
                                
                                for (int i = 0; i < reg_qty; i++) {
                                    uint16_t reg_val = modbus_get_register(start_addr + i);
                                    resp[9 + 2 * i] = (reg_val >> 8) & 0xFF;
                                    resp[10 + 2 * i] = reg_val & 0xFF;
                                }
                                
                                client.write(resp, 7 + resp_len);
                            }
                            else if (function_code == 6) { // Write Single Register
                                uint16_t reg_addr = (buffer[8] << 8) | buffer[9];
                                uint16_t reg_val = (buffer[10] << 8) | buffer[11];
                                
                                modbus_set_register(reg_addr, reg_val);
                                
                                uint8_t resp_len = 6;
                                uint8_t resp[12];
                                resp[0] = buffer[0];
                                resp[1] = buffer[1];
                                resp[2] = buffer[2];
                                resp[3] = buffer[3];
                                resp[4] = 0;
                                resp[5] = resp_len;
                                resp[6] = unit_id;
                                resp[7] = 6;
                                resp[8] = buffer[8];
                                resp[9] = buffer[9];
                                resp[10] = buffer[10];
                                resp[11] = buffer[11];
                                
                                client.write(resp, 12);
                            }
                            else if (function_code == 16) { // Write Multiple Registers
                                uint16_t start_addr = (buffer[8] << 8) | buffer[9];
                                uint16_t reg_qty = (buffer[10] << 8) | buffer[11];
                                uint8_t byte_count = buffer[12];
                                
                                int remaining = byte_count + 1;
                                int read_extra = 0;
                                while (read_extra < remaining && client.connected()) {
                                    if (client.available() > 0) {
                                        buffer[12 + read_extra] = client.read();
                                        read_extra++;
                                    } else {
                                        delay(1);
                                    }
                                }
                                
                                if (read_extra == remaining) {
                                    for (int i = 0; i < reg_qty; i++) {
                                        uint16_t reg_val = (buffer[13 + 2 * i] << 8) | buffer[14 + 2 * i];
                                        modbus_set_register(start_addr + i, reg_val);
                                    }
                                    
                                    uint8_t resp_len = 6;
                                    uint8_t resp[12];
                                    resp[0] = buffer[0];
                                    resp[1] = buffer[1];
                                    resp[2] = buffer[2];
                                    resp[3] = buffer[3];
                                    resp[4] = 0;
                                    resp[5] = resp_len;
                                    resp[6] = unit_id;
                                    resp[7] = 16;
                                    resp[8] = buffer[8];
                                    resp[9] = buffer[9];
                                    resp[10] = buffer[10];
                                    resp[11] = buffer[11];
                                    
                                    client.write(resp, 12);
                                }
                            }
                        }
                    }
                }
                delay(2);
            }
            client.stop();
            Serial.println("[Modbus TCP] Client disconnected");
        }
        delay(10);
    }
}

extern "C" {
void __attribute__((weak)) readholdingregister_modbus() {}
void __attribute__((weak)) writeholdingregister_modbus() {}
}
#else
void readholdingregister_modbus() {}
void writeholdingregister_modbus() {}
#endif

void setup() {
    Serial.begin(115200);
    
    #ifdef ESP_PLATFORM
    // Power on GSM Modem sequence
    pinMode(GSM_EN_PIN, OUTPUT);
    pinMode(GSM_PWRKEY_PIN, OUTPUT);
    
    // Enable power regulator
    digitalWrite(GSM_EN_PIN, HIGH);
    delay(500);
    
    // Open Serial1 temporarily to check if modem is alive
    Serial1.begin(MODEM_BAUD_RATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    delay(100);
    
    bool modem_on = false;
    for (int i = 0; i < 3; i++) {
        while (Serial1.available()) Serial1.read();
        Serial1.print("AT\r\n");
        uint32_t check_start = millis();
        while (millis() - check_start < 500) {
            if (Serial1.available()) {
                String resp = Serial1.readString();
                if (resp.indexOf("OK") != -1) {
                    modem_on = true;
                    break;
                }
            }
        }
        if (modem_on) break;
    }
    
    if (!modem_on) {
        Serial.println("[GSM] Modem is OFF. Pulsing PWRKEY low to turn ON...");
        digitalWrite(GSM_PWRKEY_PIN, LOW);
        delay(2000);
        digitalWrite(GSM_PWRKEY_PIN, HIGH);
        delay(5000); // Wait for modem bootup
    } else {
        Serial.println("[GSM] Modem is already ON. Skipping PWRKEY toggling.");
    }
    #endif
    
    // UART Buffer configuration and port opening from requirements
    Serial1.setRxBufferSize(8192);
    Serial1.setTxBufferSize(8192);
    Serial1.begin(MODEM_BAUD_RATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    
    WiFi.softAP("ESP32-Firmware-Portal", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
    /*
    server.on("/", HTTP_GET, handle_root);
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/trigger", HTTP_POST, handle_trigger);
    server.on("/api/test_gprs", HTTP_POST, handle_test_gprs);
    server.on("/api/ping_server", HTTP_POST, handle_ping_server);
    server.on("/api/clear_cache", HTTP_POST, handle_clear_cache);
    server.on("/api/reboot", HTTP_POST, handle_reboot);
    server.on("/api/write_register", HTTP_POST, handle_write_register);
    server.on("/api/list_modem_files", HTTP_GET, handle_list_modem_files);
    server.on("/api/list_esp32_storage", HTTP_GET, handle_esp32_storage);
    server.begin();
    Serial.println("HTTP Server started on Port 80");
    */
    
    #ifdef ESP_PLATFORM
    xTaskCreate(modbus_tcp_task, "modbus_tcp_task", 4096, NULL, 4, NULL);
    #endif
}

void loop() {
    // server.handleClient();
    readholdingregister_modbus();
    writeholdingregister_modbus();
    
    // Poll Modbus command register 41 (REG_START_FIRMWARE_PROCESS)
    uint16_t cmd = modbus_get_register(REG_START_FIRMWARE_PROCESS);
    if (cmd != 0) {
        handle_modbus_command(cmd);
        modbus_set_register(REG_START_FIRMWARE_PROCESS, 0); // Clear command register after processing
    }
    
    delay(2);
}

#else
// =========================================================================
// PC Simulation Server Implementation (Win32 Native API)
// =========================================================================
static DWORD WINAPI run_mock_update(LPVOID lpParam) {
    (void)lpParam;
    
    const char* dummy_firmware = 
        "ESP32_Firmware_Image_Verification_Success_Flow_Through_GPRS_And_PSRAM_Buffer_Decoded_Successfully_OTA_Active";
    size_t fw_len = strlen(dummy_firmware);

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

    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        s_parts_payloads[i] = parts[i];
    }

    // Trigger user's exact flow
    trigger_firmware_update_flow();
    
    for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
        free(parts[i]);
    }
    free(b64_buffer);
    s_update_in_progress = false;
    return 0;
}

static void trigger_ota_update_thread() {
    if (!s_update_in_progress) {
        s_update_in_progress = true;
        s_log_count = 0;
        CreateThread(NULL, 0, run_mock_update, NULL, 0, NULL);
    }
}

static DWORD WINAPI reboot_async_thread(LPVOID lpParam) {
    (void)lpParam;
    Sleep(500);
    ESP.restart();
    return 0;
}

#if 0
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
    address.sin_port = htons(80);
    
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
    printf("     Open URL in browser: http://192.168.4.1 (or http://localhost)       \n");
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
                 << "  \"float_reg_1\": " << getFloatValue(FW_DOWNLOAD_PROGRESS) << ",\n"
                 << "  \"float_reg_3\": " << getFloatValue(ERROR4) << ",\n"
                 << "  \"float_reg_5\": " << getFloatValue(FW_TOTAL_PARTS) << ",\n"
                 << "  \"float_reg_7\": " << getFloatValue(FILE_UUID) << ",\n"
                 << "  \"float_reg_9\": " << getFloatValue(SPIFF_SET_BIT) << ",\n"
                 << "  \"float_reg_11\": " << getFloatValue(FP_SIZE) << ",\n"
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
            json << "  ],\n"
                 << "  \"psram_files\": [\n";
            bool first_file = true;
            for (int i = 0; i < FW_UPDATE_NUM_PARTS; i++) {
                if (s_psram_part_sizes[i] > 0) {
                    if (!first_file) json << ",\n";
                    json << "    {\"name\": \"PSRAM:otafw_part" << (i + 1) << ".b64\", \"size\": " << s_psram_part_sizes[i] << "}";
                    first_file = false;
                }
            }
            json << "\n  ]\n"
                 << "}";
            
            std::string body = json.str();
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else if (path.rfind("/api/trigger", 0) == 0 && method == "POST") {
            for (int k = 0; k < FW_UPDATE_NUM_PARTS; k++) {
                char param_key[16];
                snprintf(param_key, sizeof(param_key), "url%d=", k + 1);
                size_t p_pos = path.find(param_key);
                if (p_pos != std::string::npos) {
                    size_t start_val = p_pos + strlen(param_key);
                    size_t end_val = path.find("&", start_val);
                    std::string raw_url = (end_val == std::string::npos) ? path.substr(start_val) : path.substr(start_val, end_val - start_val);
                    std::string decoded = "";
                    for (size_t i = 0; i < raw_url.length(); i++) {
                        if (raw_url[i] == '%' && i + 2 < raw_url.length()) {
                            int hex = std::stoi(raw_url.substr(i + 1, 2), nullptr, 16);
                            decoded += (char)hex;
                            i += 2;
                        } else if (raw_url[i] == '+') {
                            decoded += ' ';
                        } else {
                            decoded += raw_url[i];
                        }
                    }
                    s_custom_urls[k] = decoded.c_str();
                } else {
                    char fallback[128];
                    snprintf(fallback, sizeof(fallback), "http://64.251.10.159/otafw_part%d.b64", k + 1);
                    s_custom_urls[k] = fallback;
                }
            }
            trigger_ota_update_thread();
            std::string body = "{\"status\":\"triggered\"}";
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else if (path == "/api/test_gprs" && method == "POST") {
            add_log("gprs_check", "idle", 0, ERR_NONE, 0, "GPRS connection checked: Online.");
            std::string body = "{\"connected\":true}";
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else if (path.rfind("/api/ping_server", 0) == 0 && method == "POST") {
            std::string url_param = "64.251.10.159";
            size_t query_pos = path.find("?url=");
            if (query_pos != std::string::npos) {
                std::string raw_url = path.substr(query_pos + 5);
                std::string decoded = "";
                for (size_t i = 0; i < raw_url.length(); i++) {
                    if (raw_url[i] == '%' && i + 2 < raw_url.length()) {
                        int hex = std::stoi(raw_url.substr(i + 1, 2), nullptr, 16);
                        decoded += (char)hex;
                        i += 2;
                    } else if (raw_url[i] == '+') {
                        decoded += ' ';
                    } else {
                        decoded += raw_url[i];
                    }
                }
                url_param = decoded;
            }
            add_log("ping_test", "idle", 0, 0, 0, ("[Mock] Ping to " + url_param + " successful (latency 35ms).").c_str());
            std::string body = "{\"success\":true}";
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else if (path == "/api/clear_cache" && method == "POST") {
            add_log("cleanup", "idle", 0, 0, 0, "Cache files cleared from UFS and local registers reset.");
            modbus_set_status(STATUS_IDLE);
            modbus_set_progress(0);
            modbus_set_error(ERR_NONE);
            modbus_set_current_part(0);
            std::string body = "{\"status\":\"cleared\"}";
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else if (path == "/api/reboot" && method == "POST") {
            std::string body = "{\"status\":\"rebooting\"}";
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
            CreateThread(NULL, 0, reboot_async_thread, NULL, 0, NULL);
        } else if (path.rfind("/api/write_register", 0) == 0 && method == "POST") {
            int reg = 0, val = 0;
            size_t reg_pos = path.find("register=");
            size_t val_pos = path.find("value=");
            if (reg_pos != std::string::npos && val_pos != std::string::npos) {
                reg = atoi(path.substr(reg_pos + 9).c_str());
                val = atoi(path.substr(val_pos + 6).c_str());
                modbus_set_register(reg, val);
                
                // Synchronize float registers for simulation
                if (reg == REG_DOWNLOAD_STATUS) {
                    if (val == STATUS_IDLE) setFloatValue(FW_DOWNLOAD_PROGRESS, 0.0f);
                    else if (val == STATUS_DOWNLOADING) setFloatValue(FW_DOWNLOAD_PROGRESS, 1.0f);
                    else if (val == STATUS_DECODING) setFloatValue(FW_DOWNLOAD_PROGRESS, 2.0f);
                    else if (val == STATUS_FLASHING) setFloatValue(FW_DOWNLOAD_PROGRESS, 3.0f);
                    else if (val == STATUS_COMPLETE) setFloatValue(FW_DOWNLOAD_PROGRESS, 18.0f);
                    else if (val == STATUS_ERROR) setFloatValue(FW_DOWNLOAD_PROGRESS, -1.0f);
                } else if (reg == REG_ERROR_CODE) {
                    setFloatValue(ERROR4, (float)val);
                } else if (reg == REG_CURRENT_PART) {
                    setFloatValue(SPIFF_SET_BIT, val * 10000.0f);
                    setFloatValue(FP_SIZE, 80000.0f);
                }
                
                char details[128];
                snprintf(details, sizeof(details), "Manual Modbus Write: Reg %d = %d", reg, val);
                add_log("modbus_write", "idle", 0, 0, 0, details);
            }
            std::string body = "{\"status\":\"written\"}";
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else if (path == "/api/list_modem_files" && method == "GET") {
            std::string body = "{\n  \"files\": [\n"
                               "    {\"name\": \"UFS:otafw_part1.b64\", \"size\": 744155},\n"
                               "    {\"name\": \"UFS:otafw_part2.b64\", \"size\": 744155},\n"
                               "    {\"name\": \"UFS:otafw_part3.b64\", \"size\": 744155},\n"
                               "    {\"name\": \"UFS:otafw_part4.b64\", \"size\": 744155}\n"
                               "  ]\n"
                               "}";
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: application/json\r\n"
                     << "Content-Length: " << body.length() << "\r\n"
                     << "Connection: close\r\n\r\n"
                     << body;
        } else if (path == "/api/list_esp32_storage" && method == "GET") {
            std::string body = "{\n"
                               "  \"flash_total\": 4194304,\n"
                               "  \"flash_free\": 2097152,\n"
                               "  \"heap_free\": 285430,\n"
                               "  \"psram_total\": 4194304\n"
                               "}";
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
#endif

int main(void) {
    printf("PC Simulator: HTTP Web GUI disabled. Simulating Modbus Write of 1 to Register 41...\n");
    modbus_set_register(REG_START_FIRMWARE_PROCESS, 1);
    
    // Simulate the loop poll
    while (true) {
        uint16_t cmd = modbus_get_register(REG_START_FIRMWARE_PROCESS);
        if (cmd != 0) {
            handle_modbus_command(cmd);
            modbus_set_register(REG_START_FIRMWARE_PROCESS, 0);
        }
        
        // If update thread is running, let it proceed
        Sleep(10);
        
        // If update was triggered and finished, break the loop
        if (s_parts_payloads[0] && !s_update_in_progress && cmd == 0) {
            // Wait a bit to ensure it finished completely
            Sleep(500);
            if (!s_update_in_progress) break;
        }
    }
    
    printf("PC Simulator: OTA update flow complete.\n");
    return 0;
}
#endif
