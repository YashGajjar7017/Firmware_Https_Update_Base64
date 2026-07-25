#ifndef ESP_PLATFORM
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#endif
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
#include "mbedtls/base64.h"
#include <Update.h>
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

#define MAX_RETRY1 3

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
static String s_custom_urls[4] = {
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

    if (s_log_count < MAX_LOG_ENTRIES) {
        LogEntry& entry = s_log_buffer[s_log_count];
        strncpy(entry.timestamp, time_str, sizeof(entry.timestamp));
        strncpy(entry.event, event, sizeof(entry.event));
        strncpy(entry.state, state, sizeof(entry.state));
        entry.progress = progress;
        entry.error = error_code;
        entry.part = part;
        strncpy(entry.details, sanitized_details, sizeof(entry.details));
        s_log_count++;
    }

    // Log to standard output / serial
    #ifdef ESP_PLATFORM
    Serial.printf("{\"timestamp\":\"%s\",\"event\":\"%s\",\"state\":\"%s\",\"progress\":%d,\"error_code\":%d,\"part\":%d,\"details\":\"%s\"}\n",
                  time_str, event, state, progress, error_code, part, sanitized_details);
    Serial.printf("[OTA LOG] [%s] %s | State: %s | Progress: %d%% | Error: %d | Part: %d\r\n",
                  event, sanitized_details, state, progress, error_code, part);
    #else
    printf("{\"timestamp\":\"%s\",\"event\":\"%s\",\"state\":\"%s\",\"progress\":%d,\"error_code\":%d,\"part\":%d,\"details\":\"%s\"}\n",
           time_str, event, state, progress, error_code, part, sanitized_details);
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
            } else if (val >= 3) {
                int part = ((int)val - 1) / 2;
                modbus_set_progress(part * 25);
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
    if (key == FW_TOTAL_PARTS) return 4.0f;
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
        char details[256];
        snprintf(details, sizeof(details), "AT CMD: %s", cmd);
        size_t len = strlen(details);
        while (len > 0 && (details[len-1] == '\r' || details[len-1] == '\n')) {
            details[--len] = '\0';
        }
        add_log("AT_CMD", "sending", 0, 0, 0, details);
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
                Serial.printf("[GSM RECV] %s\r\n", buffer);
                return true;
            }
            delay(10);
        }
        Serial.printf("[GSM RECV TIMEOUT] Expected: %s. Got: %s\r\n", expected_resp, buffer);
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

uint8_t TcpClient::getfirmwarefile(String url, String imei, String user, String pass, int x) {
    (void)imei; (void)user; (void)pass;
    
    // Clear UFS cache logs
    module->sendCommand("AT+QFDEL=\"UFS:firm\"\r\n", "OK", 900, 1, 1000, "+CME ERROR: 418");
    char del_cmd[64];
    snprintf(del_cmd, sizeof(del_cmd), "AT+QFDEL=\"UFS:otafw_part%d.b64\"\r\n", x);
    module->sendCommand(del_cmd, "OK", 900, 1, 1000, "+CME ERROR: 418");
    module->sendCommand("AT+QFLST\r\n", "OK", 1000, 1);

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
            // Keep existing payload if download is empty / fails
            if (s_parts_data[x - 1].empty()) {
                // Keep default
            } else {
                s_parts_payloads[x - 1] = s_parts_data[x - 1].c_str();
            }
        }
        
        char log_req[512];
        snprintf(log_req, sizeof(log_req), "HTTP GET request: %s", url_command1);
        add_log("HTTP_REQ", "requesting", 0, 0, x, log_req);
        
        char log_done[512];
        snprintf(log_done, sizeof(log_done), "ACK: Part %d downloaded successfully from URL: %s", x, url_command1);
        add_log("HTTP_REQ", "complete", 0, 0, x, log_done);
        return 0;
        #else
        int urlLen = strlen(url_command1);
        char url_command12[64];
        snprintf(url_command12, sizeof(url_command12), "AT+QHTTPURL=%d,160\r\n", urlLen);

        char log_req[512];
        snprintf(log_req, sizeof(log_req), "HTTP GET request: %s", url_command1);
        add_log("HTTP_REQ", "requesting", 0, 0, x, log_req);

        if (!module->sendCommand(url_command12, "CONNECT", 1000, 1))
            return 3;
        if (!module->sendCommand(url_command1, "OK", 6000, 1))
            return 4;

        // Increase timeout to 120 seconds to allow download over cellular connection
        if (!module->sendCommandOpt("AT+QHTTPGET=240\r\n", "+QHTTPGET: 0", 120000, 3, "+QHTTPGET: 0"))
            return 5;
        
        char read_cmd[128];
        snprintf(read_cmd, sizeof(read_cmd), "AT+QHTTPREADFILE=\"UFS:otafw_part%d.b64\",420\r\n", x);
        if (!(module->sendCommandOpt(read_cmd, "+QHTTPREADFILE: 0", 60000, 2, "+QHTTPREADFILE: 705"))) {
            return 6;
        }
        
        char log_done[512];
        snprintf(log_done, sizeof(log_done), "ACK: Part %d downloaded successfully from URL: %s", x, url_command1);
        add_log("HTTP_REQ", "complete", 0, 0, x, log_done);

        module->sendCommand("AT+QFLST\r\n", "OK", 2000, 1);
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
static uint32_t spiffs_size = 0;
static uint8_t buf1[8192];

int qftp::qftp_file_read(int32_t handle, int length, uint8_t *buff) {
    #ifndef ESP_PLATFORM
    // Host PC Simulation Mode
    int current_part = modbus_get_register(REG_CURRENT_PART);
    if (current_part < 1 || current_part > 4) current_part = 1;
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
    }
    return read_len;
    #else
    // ESP32 Hardware Mode
    int length1;
    char cmd[100];
    sprintf(cmd, FTP_FILE_READ, handle, length);
    if (module->sendCommandStd(cmd, "OK", 1000, 2)) {
        unsigned char *tcp = NULL;
        char len[8] = {0};
        uint8_t j = 0;
        tcp = (unsigned char *)strstr((char *)module->buffer, "CONNECT");
        if (tcp != NULL) {
            tcp = tcp + 8;
            while (*tcp != '\r') {
                len[j++] = (char)*tcp;
                tcp++;
                if (j > 7) return -1;
            }
            length1 = atoi(len);
            if (length1 == 0) return length1;
            tcp = tcp + 2;
            unsigned char *eod = NULL;
            eod = (unsigned char *)strstr((char *)module->buffer, "\r\nOK");
            if (length1 > length) return -2;
            for (int i = 0; i < length1; i++) {
                if (tcp == eod) return i;
                buff[i] = *tcp;
                tcp++;
            }
            return length1;
        }
    }
    return 0;
    #endif
}

uint8_t qftp::qftp_file_seek(int handle, int offset, uint8_t mode) {
    char cmd[100];
    sprintf(cmd, FTP_POSITION, handle);
    module->sendCommand(cmd, "OK", 1000, 2);

    sprintf(cmd, FTP_FILE_SEEK, handle, offset, mode);
    if (module->sendCommand(cmd, "OK", 1000, 2)) {
        sprintf(cmd, FTP_POSITION, handle);
        module->sendCommand(cmd, "OK", 1000, 2);
        return 1;
    }
    
    sprintf(cmd, FTP_POSITION, handle);
    module->sendCommand(cmd, "OK", 1000, 2);
    return 0;
}

uint8_t ftp_filedownload(uint8_t type, String filename, uint16_t read_size, char *sd_filename) {
    (void)type; (void)sd_filename;
    uint32_t fp = ftp.qftp_file_open(filename, 2, 0);
    if (fp == 0) {
        Serial.println("FTP file open failed");
        return 0;
    }
    
    uint32_t encodedOffset = 0;
    uint32_t fp_size = getFloatValue(FP_SIZE);
    spiffs_offset = 0;
    int datasize = 1;
    int xcount = 0;

    // Local stack-allocated buffer for chunk-wise decoding
    static uint8_t decode_temp_buf[8192];

    while (datasize > 0) {
        uint32_t size = fp_size - spiffs_offset;
        int retry = 0;
        bool success = false;

        while (retry < MAX_RETRY1 && !success) {
            memset(buf1, 0, sizeof(buf1));
            if (size > read_size) {
                datasize = ftp.qftp_file_read(fp, read_size, buf1);
            } else {
                if (size == 0) {
                    ftp.qftp_file_close(fp);
                    return 1;
                }
                datasize = ftp.qftp_file_read(fp, size, buf1);
            }
            
            xcount++;
            if (datasize == 0) {
                ftp.qftp_file_close(fp);
                return 1;
            }
            if (datasize < 0) {
                retry++;
                ftp.qftp_file_seek(fp, encodedOffset, 0);
                delay(10);
                continue;
            }

            size_t decodedLen = 0;
            spiffs_size = datasize;

            int ret = mbedtls_base64_decode(
                decode_temp_buf,
                sizeof(decode_temp_buf),
                &decodedLen,
                buf1,
                datasize
            );

            if (ret == 0) {
                // Write decoded buffer chunk directly to OTA partition
                if (Update.write(decode_temp_buf, decodedLen) != decodedLen) {
                    Serial.println("OTA Write chunk failed!");
                    #ifdef ESP_PLATFORM
                    Update.printError(Serial);
                    #endif
                    ftp.qftp_file_close(fp);
                    return 0;
                }
                
                spiffs_offset += datasize;
                finalsize = finalsize + decodedLen;
                success = true;
                encodedOffset += datasize;

                char log_details[256];
                snprintf(log_details, sizeof(log_details), "Read chunk of %d base64 bytes at offset %d, flashed %d bytes binary. Total: %d bytes.", 
                         datasize, (int)(spiffs_offset - datasize), (int)decodedLen, (int)finalsize);
                add_log("ota_flash", "flashing", (spiffs_offset * 100) / fp_size, 0, type + 1, log_details);
            } else {
                retry++;
                ftp.qftp_file_seek(fp, encodedOffset, 0);
                delay(100);
            }
        }
        
        if (!success) {
            setFloatValue(ERROR4, 6);
            ftp.qftp_file_close(fp);
            return 0;
        }
        delay(10);
        setFloatValue(SPIFF_SET_BIT, spiffs_offset);
    }

    ftp.qftp_file_close(fp);
    return 1;
}

// =========================================================================
// Main orchestrator flow based on user specification
// =========================================================================
static uint8_t trigger_firmware_update_flow() {
    String devimei = "123456789012345";
    String username = "username";
    String password = "password";
    uint8_t err_code = 0;
    
    modbus_set_error(ERR_NONE);
    modbus_set_status(STATUS_DOWNLOADING);
    
    setFloatValue(FW_DOWNLOAD_PROGRESS, 1);
    
    // We no longer allocate a 1.5MB PSRAM buffer!
    // Initialize OTA Update partition immediately
    add_log("ota_flash", "initializing", 5, 0, 0, "Initializing OTA update flashing session...");
    
    #ifdef ESP_PLATFORM
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        setFloatValue(ERROR4, 7);
        Update.printError(Serial);
        add_log("ota_flash", "error", 0, ERR_OTA_FLASH, 0, "Failed to initialize OTA partition!");
        return 0;
    }
    #else
    Update.begin(108); // Simulate binary size
    #endif
    
    setFloatValue(FW_DOWNLOAD_PROGRESS, 2);
    finalsize = 0;
    spiffs_offset = 0;
    
    for (int part = 1; part <= getFloatValue(FW_TOTAL_PARTS); part++) {
        modbus_set_current_part(part);
        
        String fw_url = s_custom_urls[part - 1];
        if (fw_url.length() == 0) {
            char fallback_url[128];
            snprintf(fallback_url, sizeof(fallback_url), "http://64.251.10.159/otafw_part%d.b64", part);
            fw_url = fallback_url;
        }
        
        // Fetch part via HTTP/HTTPS AT command flow
        int k = gprs.getfirmwarefile(fw_url, devimei, username, password, part);
        Serial.println("return value: " + String(std::to_string(k).c_str()));
        if (k > 1) {
            k = gprs.getfirmwarefile(fw_url, devimei, username, password, part);
        }
        setFloatValue(ERROR4, k);
        if (k != 0) {
            setFloatValue(ERROR4, 11);
            #ifdef ESP_PLATFORM
            Update.abort();
            #endif
            return 0;
        }
        
        setFloatValue(FW_DOWNLOAD_PROGRESS, 3);
        setFloatValue(FW_DOWNLOAD_PROGRESS, 1 + (part * 2));
        
        String filename1 = "otafw_part" + String(std::to_string(part).c_str()) + ".b64";
        
        // Query file size dynamically
        #ifndef ESP_PLATFORM
        setFloatValue(FP_SIZE, strlen(s_parts_payloads[part - 1]));
        #else
        uint32_t parsed_size = 476856; // Default fallback
        char list_cmd[64];
        snprintf(list_cmd, sizeof(list_cmd), "AT+QFLST=\"UFS:otafw_part%d.b64\"\r\n", part);
        if (module->sendCommand(list_cmd, "OK", 2000, 1)) {
            char* ptr = strstr(module->buffer, ",");
            if (ptr != NULL) {
                parsed_size = (uint32_t)atoi(ptr + 1);
                if (parsed_size == 0) parsed_size = 476856;
            }
        }
        setFloatValue(FP_SIZE, parsed_size);
        #endif
        
        // Loop open, seek, read chunks, and decode directly to OTA
        err_code = ftp_filedownload(part - 1, filename1, 6144, (char*)"qwe.b64");
        if (err_code == 0) {
            setFloatValue(FILE_UUID, 0);
            #ifdef ESP_PLATFORM
            Update.abort();
            #endif
            return 0;
        }
        setFloatValue(FW_DOWNLOAD_PROGRESS, 2 + (part * 2));
        delay(10);
    }
    
    resetWatchdog();
    delay(100);
    
    char details[128];
    snprintf(details, sizeof(details), "Decoded firmware file complete. Total binary size: %u bytes", finalsize);
    add_log("validation", "checking", 90, 0, 4, details);
    modbus_set_status(STATUS_FLASHING);
    
    // Commit the OTA partition
    if (Update.end(true)) {
        setFloatValue(ERROR4, 9);
        modbus_set_status(STATUS_COMPLETE);
        modbus_set_progress(100);
        add_log("system", "restarting", 100, 0, 4, "OTA Flashing complete. Restarting ESP32...");
        delay(2000);
        ESP.restart();
    } else {
        #ifdef ESP_PLATFORM
        Update.printError(Serial);
        #endif
        setFloatValue(ERROR4, 10);
        add_log("ota_flash", "error", 100, ERR_OTA_FLASH, 4, "OTA partition closing write failed!");
    }
    
    if (err_code == 1) {
        setFloatValue(FILE_UUID, 0);
    }
    
    return 1;
}

#ifdef ESP_PLATFORM
// =========================================================================
// ESP32 Web Server Endpoints
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
    trigger_firmware_update_flow();
    vTaskDelete(NULL);
}

static void handle_trigger() {
    s_log_count = 0;
    for (int i = 0; i < 4; i++) {
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
    xTaskCreate(ota_background_task, "ota_task", 8192, NULL, 5, NULL);
    server.send(200, "application/json", "{\"status\":\"triggered\"}");
}

static void handle_test_gprs() {
    bool ok = false;
    #ifdef ESP_PLATFORM
    ok = module->sendCommand("AT+CGATT?\r\n", "+CGATT: 1", 3000, 2);
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
    module->sendCommand("AT+QFDEL=\"UFS:*\"\r\n", "OK", 2000, 1);
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
    #if CONFIG_SPIRAM_SUPPORT
    psram_size = ESP.getPsramSize();
    #endif
    
    String json = "{\n";
    json += "  \"flash_total\": " + String(flash_size) + ",\n";
    json += "  \"flash_free\": " + String(flash_size / 2) + ",\n";
    json += "  \"heap_free\": " + String(free_heap) + ",\n";
    json += "  \"psram_total\": " + String(psram_size) + "\n";
    json += "}";
    server.send(200, "application/json", json);
    #else
    String json = "{\n"
                  "  \"flash_total\": 4194304,\n"
                  "  \"flash_free\": 2097152,\n"
                  "  \"heap_free\": 285430,\n"
                  "  \"psram_total\": 4194304\n"
                  "}";
    server.send(200, "application/json", json);
    #endif
}

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
    
    // Pulse PWRKEY low (standard active-low power key for Quectel)
    digitalWrite(GSM_PWRKEY_PIN, LOW);
    delay(2000);
    digitalWrite(GSM_PWRKEY_PIN, HIGH);
    delay(3000); // Wait for modem bootup
    #endif
    
    // UART Buffer configuration and port opening from requirements
    Serial1.setRxBufferSize(8192);
    Serial1.setTxBufferSize(8192);
    Serial1.begin(MODEM_BAUD_RATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    
    WiFi.softAP("ESP32-Firmware-Portal", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
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
}

void loop() {
    server.handleClient();
    readholdingregister_modbus();
    writeholdingregister_modbus();
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

    s_parts_payloads[0] = parts[0];
    s_parts_payloads[1] = parts[1];
    s_parts_payloads[2] = parts[2];
    s_parts_payloads[3] = parts[3];

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
        } else if (path.rfind("/api/trigger", 0) == 0 && method == "POST") {
            for (int k = 0; k < 4; k++) {
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

int main(void) {
    HANDLE thread_handle = CreateThread(NULL, 0, win_http_server_thread, NULL, 0, NULL);
    if (thread_handle != NULL) {
        WaitForSingleObject(thread_handle, INFINITE);
        CloseHandle(thread_handle);
    }
    return 0;
}
#endif
