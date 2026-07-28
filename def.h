#include "Firmware_Update_Https_Call.ino"

// Base64_File1_Status
#define Base64_File1_Downloading 1
#define Base64_File1_Decompressing 2
#define Base64_File1_Flashing 3
#define Base64_File1_Error 4
#define Base64_File1_Completed 5

// Base64_File2_Status
#define Base64_File2_Downloading 6
#define Base64_File2_Decompressing 7
#define Base64_File2_Flashing 8
#define Base64_File2_Error 9
#define Base64_File2_Completed 10

// Base64_File3_Status
#define Base64_File3_Downloading 11
#define Base64_File3_Decompressing 12
#define Base64_File3_Flashing 13
#define Base64_File3_Error 14
#define Base64_File3_Completed 15

// Base64_File4_Status
#define Base64_File4_Downloading 16
#define Base64_File4_Decompressing 17
#define Base64_File4_Flashing 18
#define Base64_File4_Error 19
#define Base64_File4_Completed 20

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