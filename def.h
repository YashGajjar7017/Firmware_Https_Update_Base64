#include "Firmware_Update_Https_Call.ino"

#define MODBUS_DEvice_Address 10
#define REG_DOWNLOAD_STATUS 0
#define REG_PROGRESS_PERCENT 1
#define REG_ERROR_CODE 2
#define REG_CURRENT_PART 3

#define STATUS_IDLE 0
#define STATUS_DOWNLOADING 1
#define STATUS_DECODING 2
#define STATUS_FLASHING 3
#define STATUS_COMPLETE 4
#define STATUS_ERROR 5

#define ERR_NONE 0
#define ERR_GPRS_FAIL 1
#define ERR_HTTP_FAIL 2
#define ERR_BASE64_DECODE 3
#define ERR_MEMORY_PSRAM 4
#define ERR_OTA_FLASH 5

#define File1Download 20
#define File2Download 21
#define File3Download 22
#define File4Download 23

#define REG_FILE1_Download_COMPLETE 0
#define REG_FILE2_Download_COMPLETE 1
#define REG_FILE3_Download_COMPLETE 2
#define REG_FILE4_Download_COMPLETE 3

void readholdingregister_modbus();
void writeholdingregister_modbus();

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