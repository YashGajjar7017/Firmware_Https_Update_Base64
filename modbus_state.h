#ifndef MODBUS_STATE_H
#define MODBUS_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Modbus Register Map
typedef enum {
    REG_FILE1_STATUS = 1,
    REG_FILE1_PROGRESS = 3,
    REG_FILE1_PSRAM = 5,
    
    REG_FILE2_STATUS = 6,
    REG_FILE2_PROGRESS = 8,
    REG_FILE2_PSRAM = 10,
    
    REG_FILE3_STATUS = 11,
    REG_FILE3_PROGRESS = 13,
    REG_FILE3_PSRAM = 15,
    
    REG_FILE4_STATUS = 16,
    REG_FILE4_PROGRESS = 18,
    REG_FILE4_PSRAM = 20,
    // Obsolete but kept for code compatibility
    REG_DOWNLOAD_STATUS = 21,
    REG_PROGRESS_PERCENT = 22,
    REG_ERROR_CODE = 23,
    REG_CURRENT_PART = 0,

    REG_START_FIRMWARE_PROCESS = 41,

    REG_ERROR_COUNT_1 = 51,
    REG_ERROR_COUNT_2 = 53,
    REG_ERROR_COUNT_3 = 55,
    REG_ERROR_COUNT_4 = 57,

    REG_PSRAM_BUFFER_SIZE = 59,
    REG_PSRAM_CHUNK_SIZE = 61,
    
    NUM_MODBUS_REGISTERS = 62
} ModbusRegisterOffset;

// Download Status values (Register 0)
typedef enum {
    STATUS_IDLE = 0,
    STATUS_DOWNLOADING = 1,
    STATUS_DECODING = 2,
    STATUS_FLASHING = 3,
    STATUS_COMPLETE = 4,
    STATUS_ERROR = 5
} DownloadStatus;

// Error Code values (Register 2)
typedef enum {
    ERR_NONE = 0,
    ERR_GPRS_FAIL = 1,
    ERR_HTTP_FAIL = 2,
    ERR_BASE64_DECODE = 3,
    ERR_MEMORY_PSRAM = 4,
    ERR_OTA_FLASH = 5
} UpdateErrorCode;

/**
 * @brief Thread-safe register write
 */
void modbus_set_register(uint16_t reg_offset, uint16_t value);

/**
 * @brief Thread-safe register read
 */
uint16_t modbus_get_register(uint16_t reg_offset);

// Specialized wrappers for convenience
void modbus_set_status(DownloadStatus status);
void modbus_set_progress(uint16_t progress);
void modbus_set_error(UpdateErrorCode error);
void modbus_set_current_part(uint16_t part);

uint16_t get_virtual_file(uint16_t part);
uint16_t get_virtual_progress(uint16_t part, uint16_t part_progress);

void modbus_init_mutex(void);

#ifdef __cplusplus
}
#endif

#endif // MODBUS_STATE_H
