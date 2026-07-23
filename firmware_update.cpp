#include "firmware_update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#define FREE_SPIRAM_MEM(ptr) heap_caps_free(ptr)
#else
// Simulated environment for local/PC builds
#include <malloc.h>
#define MALLOC_CAP_SPIRAM 0
inline void* heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}
inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) {
    (void)caps;
    return realloc(ptr, size);
}
inline void heap_caps_free(void* ptr) {
    free(ptr);
}
#define FREE_SPIRAM_MEM(ptr) free(ptr)
#define ESP_OK 0
#define ESP_FAIL -1
typedef int esp_ota_handle_t;
#endif

// Base64 value mapping table
static int base64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2; // padding
    return -1; // invalid/ignored character
}

// Bitwise CRC32 calculation (IEEE 802.3)
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

// Ensures the PSRAM buffer has enough space, dynamically resizing if necessary
static bool ensure_buffer_capacity(fw_update_context_t* ctx, size_t needed) {
    if (needed <= ctx->buffer_capacity) {
        return true;
    }
    
    size_t new_capacity = ctx->buffer_capacity;
    while (new_capacity < needed) {
        new_capacity += FW_UPDATE_BUFFER_GROW_STEP;
    }
    
    if (ctx->log_cb) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Expanding PSRAM buffer capacity from %u to %u bytes", 
                 (unsigned int)ctx->buffer_capacity, (unsigned int)new_capacity);
        ctx->log_cb("memory_alloc", "allocating", 0, ERR_NONE, 0, msg);
    }
    
    uint8_t* new_buf = (uint8_t*)heap_caps_realloc(ctx->psram_buffer, new_capacity, MALLOC_CAP_SPIRAM);
    if (!new_buf) {
        if (ctx->log_cb) {
            ctx->log_cb("memory_alloc", "error", 0, ERR_MEMORY_PSRAM, 0, "Failed to reallocate PSRAM buffer!");
        }
        modbus_set_error(ERR_MEMORY_PSRAM);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    ctx->psram_buffer = new_buf;
    ctx->buffer_capacity = new_capacity;
    return true;
}

// Decodes a base64 string stream into the context's PSRAM buffer
static bool decode_base64_stream(fw_update_context_t* ctx, const char* b64_input) {
    fw_b64_decoder_t* dec = &ctx->decoder;
    
    for (size_t i = 0; b64_input[i] != '\0'; i++) {
        char c = b64_input[i];
        int val = base64_char_value(c);
        if (val == -1) {
            // Ignore whitespaces, carriage returns, newlines, etc.
            continue;
        }
        
        // Push character into carry buffer
        dec->carry_buf[dec->carry_cnt++] = (uint8_t)c;
        
        if (dec->carry_cnt == 4) {
            uint8_t c1 = dec->carry_buf[0];
            uint8_t c2 = dec->carry_buf[1];
            uint8_t c3 = dec->carry_buf[2];
            uint8_t c4 = dec->carry_buf[3];
            
            int v1 = base64_char_value(c1);
            int v2 = base64_char_value(c2);
            int v3 = base64_char_value(c3);
            int v4 = base64_char_value(c4);
            
            // Determine actual payload byte size based on padding '='
            size_t bytes_to_write = 3;
            if (c3 == '=') {
                bytes_to_write = 1;
            } else if (c4 == '=') {
                bytes_to_write = 2;
            }
            
            // Make sure we have enough space in dynamic PSRAM
            if (!ensure_buffer_capacity(ctx, ctx->buffer_length + bytes_to_write)) {
                return false;
            }
            
            // Decode on-the-fly and write to PSRAM buffer
            ctx->psram_buffer[ctx->buffer_length++] = (uint8_t)((v1 << 2) | (v2 >> 4));
            if (bytes_to_write > 1) {
                ctx->psram_buffer[ctx->buffer_length++] = (uint8_t)(((v2 & 0x0F) << 4) | (v3 >> 2));
            }
            if (bytes_to_write > 2) {
                ctx->psram_buffer[ctx->buffer_length++] = (uint8_t)(((v3 & 0x03) << 6) | v4);
            }
            
            dec->carry_cnt = 0;
            
            // Stop if padding indicates end-of-stream
            if (bytes_to_write < 3) {
                break;
            }
        }
    }
    return true;
}

void fw_update_init(fw_update_context_t* ctx, fw_uart_log_cb log_cb) {
    ctx->psram_buffer = NULL;
    ctx->buffer_capacity = 0;
    ctx->buffer_length = 0;
    ctx->decoder.carry_cnt = 0;
    ctx->log_cb = log_cb;
    ctx->gprs_connected = false;
    
    modbus_set_status(STATUS_IDLE);
    modbus_set_progress(0);
    modbus_set_error(ERR_NONE);
    modbus_set_current_part(0);
}

bool fw_update_verify_gprs(fw_update_context_t* ctx) {
    if (ctx->log_cb) {
        ctx->log_cb("gprs_check", "checking", 0, ERR_NONE, 0, "Checking cellular GPRS connection...");
    }
    
    // In a real device, query cellular modem status (e.g. PPPoS state or AT+CGATT status)
    if (!ctx->gprs_connected) {
        if (ctx->log_cb) {
            ctx->log_cb("gprs_check", "error", 0, ERR_GPRS_FAIL, 0, "GPRS connection offline!");
        }
        modbus_set_error(ERR_GPRS_FAIL);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    if (ctx->log_cb) {
        ctx->log_cb("gprs_check", "idle", 0, ERR_NONE, 0, "GPRS connection verified (Online).");
    }
    return true;
}

void fw_update_clean_cache(fw_update_context_t* ctx) {
    if (ctx->log_cb) {
        ctx->log_cb("cleanup", "working", 0, ERR_NONE, 0, "Cleaning previous firmware cache & dynamic buffers");
    }
    
    if (ctx->psram_buffer != NULL) {
        FREE_SPIRAM_MEM(ctx->psram_buffer);
        ctx->psram_buffer = NULL;
    }
    ctx->buffer_capacity = 0;
    ctx->buffer_length = 0;
    ctx->decoder.carry_cnt = 0;
}

bool fw_update_init_psram(fw_update_context_t* ctx) {
    fw_update_clean_cache(ctx); // Free any existing buffer first
    
    ctx->buffer_capacity = FW_UPDATE_INITIAL_BUF_SIZE;
    ctx->psram_buffer = (uint8_t*)heap_caps_malloc(ctx->buffer_capacity, MALLOC_CAP_SPIRAM);
    if (!ctx->psram_buffer) {
        if (ctx->log_cb) {
            ctx->log_cb("memory_alloc", "error", 0, ERR_MEMORY_PSRAM, 0, "Failed to allocate initial PSRAM buffer in SPIRAM!");
        }
        modbus_set_error(ERR_MEMORY_PSRAM);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    ctx->buffer_length = 0;
    ctx->decoder.carry_cnt = 0;
    
    if (ctx->log_cb) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Allocated initial %u bytes in external PSRAM", (unsigned int)ctx->buffer_capacity);
        ctx->log_cb("memory_alloc", "idle", 0, ERR_NONE, 0, msg);
    }
    return true;
}

bool fw_update_process_part(fw_update_context_t* ctx, int part_num, const char* base64_payload) {
    if (part_num < 1 || part_num > FW_UPDATE_NUM_PARTS) {
        if (ctx->log_cb) {
            ctx->log_cb("download", "error", 0, ERR_HTTP_FAIL, part_num, "Invalid part index!");
        }
        modbus_set_error(ERR_HTTP_FAIL);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    if (ctx->log_cb) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Downloading part %d of %d over HTTP/HTTPS...", part_num, FW_UPDATE_NUM_PARTS);
        ctx->log_cb("download", "downloading", (part_num - 1) * 25, ERR_NONE, part_num, msg);
    }
    
    modbus_set_status(STATUS_DOWNLOADING);
    modbus_set_current_part(part_num);
    modbus_set_progress((part_num - 1) * 25 + 5); // incremental progress update
    
    // Simulate HTTP download delay or connection timeout check
    if (base64_payload == NULL || strlen(base64_payload) == 0) {
        if (ctx->log_cb) {
            ctx->log_cb("download", "error", 0, ERR_HTTP_FAIL, part_num, "HTTP/HTTPS download connection timed out or empty payload!");
        }
        modbus_set_error(ERR_HTTP_FAIL);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    if (ctx->log_cb) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Decoding Base64 payload for part %d...", part_num);
        ctx->log_cb("decode", "decoding", (part_num - 1) * 25 + 10, ERR_NONE, part_num, msg);
    }
    
    modbus_set_status(STATUS_DECODING);
    
    if (!decode_base64_stream(ctx, base64_payload)) {
        if (ctx->log_cb) {
            ctx->log_cb("decode", "error", 0, ERR_BASE64_DECODE, part_num, "Base64 decode error or corruption!");
        }
        modbus_set_error(ERR_BASE64_DECODE);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    uint16_t completion_progress = part_num * 25;
    modbus_set_progress(completion_progress);
    
    if (ctx->log_cb) {
        char details[128];
        snprintf(details, sizeof(details), "Successfully completed part %d. Decoded payload size: %u bytes", 
                 part_num, (unsigned int)ctx->buffer_length);
        ctx->log_cb("download", "downloading", completion_progress, ERR_NONE, part_num, details);
    }
    
    return true;
}

bool fw_update_validate_image(fw_update_context_t* ctx, uint32_t expected_crc32) {
    if (ctx->log_cb) {
        ctx->log_cb("validation", "checking", 100, ERR_NONE, FW_UPDATE_NUM_PARTS, "Validating complete binary payload...");
    }
    
    if (ctx->buffer_length == 0 || ctx->psram_buffer == NULL) {
        if (ctx->log_cb) {
            ctx->log_cb("validation", "error", 100, ERR_BASE64_DECODE, FW_UPDATE_NUM_PARTS, "Empty payload or buffer uninitialized");
        }
        modbus_set_error(ERR_BASE64_DECODE);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    // Calculate CRC32 of decoded data
    uint32_t actual_crc = calculate_crc32(ctx->psram_buffer, ctx->buffer_length);
    if (actual_crc != expected_crc32) {
        if (ctx->log_cb) {
            char error_msg[128];
            snprintf(error_msg, sizeof(error_msg), "CRC32 verification failed! Expected: 0x%08X, Got: 0x%08X", 
                     (unsigned int)expected_crc32, (unsigned int)actual_crc);
            ctx->log_cb("validation", "error", 100, ERR_BASE64_DECODE, FW_UPDATE_NUM_PARTS, error_msg);
        }
        modbus_set_error(ERR_BASE64_DECODE);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    if (ctx->log_cb) {
        char success_msg[128];
        snprintf(success_msg, sizeof(success_msg), "CRC32 verified successfully: 0x%08X. Binary size: %u bytes", 
                 (unsigned int)actual_crc, (unsigned int)ctx->buffer_length);
        ctx->log_cb("validation", "idle", 100, ERR_NONE, FW_UPDATE_NUM_PARTS, success_msg);
    }
    
    return true;
}

bool fw_update_flash_ota(fw_update_context_t* ctx) {
    if (ctx->log_cb) {
        ctx->log_cb("flash", "flashing", 100, ERR_NONE, FW_UPDATE_NUM_PARTS, "Initializing ESP32 OTA flash...");
    }
    
    modbus_set_status(STATUS_FLASHING);
    
#ifdef ESP_PLATFORM
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        if (ctx->log_cb) {
            ctx->log_cb("flash", "error", 100, ERR_OTA_FLASH, FW_UPDATE_NUM_PARTS, "Failed to find active OTA partition!");
        }
        modbus_set_error(ERR_OTA_FLASH);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, ctx->buffer_length, &ota_handle);
    if (err != ESP_OK) {
        if (ctx->log_cb) {
            ctx->log_cb("flash", "error", 100, ERR_OTA_FLASH, FW_UPDATE_NUM_PARTS, "esp_ota_begin failed!");
        }
        modbus_set_error(ERR_OTA_FLASH);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    // Write full binary from PSRAM to flash partition
    err = esp_ota_write(ota_handle, ctx->psram_buffer, ctx->buffer_length);
    if (err != ESP_OK) {
        if (ctx->log_cb) {
            ctx->log_cb("flash", "error", 100, ERR_OTA_FLASH, FW_UPDATE_NUM_PARTS, "esp_ota_write failed!");
        }
        esp_ota_end(ota_handle);
        modbus_set_error(ERR_OTA_FLASH);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        if (ctx->log_cb) {
            ctx->log_cb("flash", "error", 100, ERR_OTA_FLASH, FW_UPDATE_NUM_PARTS, "esp_ota_end failed!");
        }
        modbus_set_error(ERR_OTA_FLASH);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
    
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        if (ctx->log_cb) {
            ctx->log_cb("flash", "error", 100, ERR_OTA_FLASH, FW_UPDATE_NUM_PARTS, "esp_ota_set_boot_partition failed!");
        }
        modbus_set_error(ERR_OTA_FLASH);
        modbus_set_status(STATUS_ERROR);
        return false;
    }
#else
    // Mock successful flashing in PC test environment
    if (ctx->log_cb) {
        ctx->log_cb("flash", "flashing", 100, ERR_NONE, FW_UPDATE_NUM_PARTS, "[Mock] OTA writing to partition 0x00010000...");
    }
#endif

    modbus_set_status(STATUS_COMPLETE);
    modbus_set_progress(100);
    modbus_set_error(ERR_NONE);
    
    if (ctx->log_cb) {
        ctx->log_cb("flash", "complete", 100, ERR_NONE, FW_UPDATE_NUM_PARTS, "OTA flash write successful! Boot partition updated. Device restart pending...");
    }
    return true;
}

bool fw_update_run_ota_flow(fw_update_context_t* ctx, const char* part_payloads[FW_UPDATE_NUM_PARTS], uint32_t expected_crc32) {
    // 1. Verify GPRS Cellular connection is online
    if (!fw_update_verify_gprs(ctx)) {
        return false;
    }
    
    // 2. Initialize dynamic buffer allocation inside PSRAM (deletes previous buffers/caches)
    if (!fw_update_init_psram(ctx)) {
        return false;
    }
    
    // 3. Download and process the 4 sequential Base64 firmware chunks
    for (int part = 1; part <= FW_UPDATE_NUM_PARTS; part++) {
        if (!fw_update_process_part(ctx, part, part_payloads[part - 1])) {
            return false;
        }
    }
    
    // 4. Validate complete binary payload in PSRAM (e.g. size/CRC32 checks)
    if (!fw_update_validate_image(ctx, expected_crc32)) {
        return false;
    }
    
    // 5. Flash binary directly from PSRAM to ESP32 update boot partition
    if (!fw_update_flash_ota(ctx)) {
        return false;
    }
    
    return true;
}
