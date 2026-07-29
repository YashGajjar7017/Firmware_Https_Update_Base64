#ifndef DEF_H
#define DEF_H

// Firmware_Process_Start
#define Start_Firmware_Process 1
#define Abort_Firmware_Process 3
#define GPRS_Restart 5
#define Firmware_Update_Restart 7
#define End_Firmware_Update_Process 9

// Base64_File1_Status
#define Base64_File1_Downloading 11
#define Base64_File1_Decompressing 13
#define Base64_File1_Flashing 15
#define Base64_File1_Error 17
#define Base64_File1_Completed 19

// Base64_File2_Status
#define Base64_File2_Downloading 21
#define Base64_File2_Decompressing 23
#define Base64_File2_Flashing 25
#define Base64_File2_Error 27
#define Base64_File2_Completed 29

// Base64_File3_Status
#define Base64_File3_Downloading 31
#define Base64_File3_Decompressing 33
#define Base64_File3_Flashing 35
#define Base64_File3_Error 37
#define Base64_File3_Completed 39

// Base64_File4_Status
#define Base64_File4_Downloading 41
#define Base64_File4_Decompressing 43
#define Base64_File4_Flashing 45
#define Base64_File4_Error 47
#define Base64_File4_Completed 49

// PSRAM_Buffer_Intailsed_Value 
#define PSRAM_Buffer_Size 2094
#define PSRAM_Chunk_Size 20045

#ifdef __cplusplus
extern "C" {
#endif

void readholdingregister_modbus();
void writeholdingregister_modbus();

#ifdef __cplusplus
}
#endif

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

#endif // DEF_H