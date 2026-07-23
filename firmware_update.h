#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "modbus_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration parameters
#define FW_UPDATE_NUM_PARTS         4
#define FW_UPDATE_INITIAL_BUF_SIZE  (256 * 1024) // 256 KB initial PSRAM buffer
#define FW_UPDATE_BUFFER_GROW_STEP  (256 * 1024) // Grow by 256 KB increments

/**
 * @brief Logger function type for structured UART output
 */
typedef void (*fw_uart_log_cb)(const char* event, const char* state, int progress, int error_code, int part, const char* details);

/**
 * @brief Base64 streaming decoder state
 */
typedef struct {
    uint8_t carry_buf[4];
    size_t carry_cnt;
} fw_b64_decoder_t;

/**
 * @brief Main firmware update context
 */
typedef struct {
    uint8_t* psram_buffer;       // Dynamic buffer allocated in SPIRAM
    size_t buffer_capacity;      // Total capacity of the PSRAM buffer
    size_t buffer_length;        // Actual length of decoded binary data
    fw_b64_decoder_t decoder;    // Base64 streaming decoder state
    fw_uart_log_cb log_cb;       // Logging callback
    bool gprs_connected;         // GPRS connection simulation status
} fw_update_context_t;

/**
 * @brief Initialize the firmware update context
 * @param ctx Pointer to the context struct
 * @param log_cb Callback for outputting structured UART logs
 */
void fw_update_init(fw_update_context_t* ctx, fw_uart_log_cb log_cb);

/**
 * @brief Simulates verifying the GPRS connection
 * @param ctx Pointer to the context struct
 * @return true if GPRS is connected, false otherwise
 */
bool fw_update_verify_gprs(fw_update_context_t* ctx);

/**
 * @brief Cleanly deletes previously stored/cached firmware file or temporary buffers
 * @param ctx Pointer to the context struct
 */
void fw_update_clean_cache(fw_update_context_t* ctx);

/**
 * @brief Allocates the initial PSRAM buffer
 * @param ctx Pointer to the context struct
 * @return true on success, false on allocation failure
 */
bool fw_update_init_psram(fw_update_context_t* ctx);

/**
 * @brief Handles downloading and processing a base64-encoded chunk/part
 * @param ctx Pointer to the context struct
 * @param part_num Current part index (1 to 4)
 * @param base64_payload Null-terminated base64 payload received from HTTP/HTTPS server
 * @return true if successful, false on decode/memory/network error
 */
bool fw_update_process_part(fw_update_context_t* ctx, int part_num, const char* base64_payload);

/**
 * @brief Validates the complete downloaded image (CRC32 and partition sizing)
 * @param ctx Pointer to the context struct
 * @param expected_crc32 CRC32 to validate against
 * @return true if image is valid, false otherwise
 */
bool fw_update_validate_image(fw_update_context_t* ctx, uint32_t expected_crc32);

/**
 * @brief Flashes the downloaded binary image in PSRAM to the ESP32 OTA partition
 * @param ctx Pointer to the context struct
 * @return true on success, false on write or commit failure
 */
bool fw_update_flash_ota(fw_update_context_t* ctx);

/**
 * @brief Main orchestrator function that runs the complete update flow
 * @param ctx Pointer to the context struct
 * @param part_payloads Array of 4 Base64 strings representing the update parts
 * @param expected_crc32 CRC32 to validate the firmware against
 * @return true on complete success, false if any step fails
 */
bool fw_update_run_ota_flow(fw_update_context_t* ctx, const char* part_payloads[FW_UPDATE_NUM_PARTS], uint32_t expected_crc32);

#ifdef __cplusplus
}
#endif

#endif // FIRMWARE_UPDATE_H
