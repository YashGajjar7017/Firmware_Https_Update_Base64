#ifndef MODBUS_STATE_H
#define MODBUS_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Modbus Register Map
typedef enum {
    REG_DOWNLOAD_STATUS = 0,
    REG_PROGRESS_PERCENT = 1,
    REG_ERROR_CODE = 2,
    REG_CURRENT_PART = 3,
    NUM_MODBUS_REGISTERS = 4
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

#ifdef __cplusplus
}
#endif

#endif // MODBUS_STATE_H
