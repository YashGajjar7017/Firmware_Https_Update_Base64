#include "modbus_state.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static uint16_t s_modbus_registers[NUM_MODBUS_REGISTERS] = {0};
static SemaphoreHandle_t s_modbus_mutex = NULL;
static StaticSemaphore_t s_modbus_mutex_buffer;

static void init_mutex(void) {
    if (s_modbus_mutex == NULL) {
        s_modbus_mutex = xSemaphoreCreateMutexStatic(&s_modbus_mutex_buffer);
    }
}

void modbus_set_register(uint16_t reg_offset, uint16_t value) {
    init_mutex();
    if (reg_offset < NUM_MODBUS_REGISTERS) {
        if (xSemaphoreTake(s_modbus_mutex, portMAX_DELAY) == pdTRUE) {
            s_modbus_registers[reg_offset] = value;
            xSemaphoreGive(s_modbus_mutex);
        }
    }
}

uint16_t modbus_get_register(uint16_t reg_offset) {
    init_mutex();
    uint16_t val = 0;
    if (reg_offset < NUM_MODBUS_REGISTERS) {
        if (xSemaphoreTake(s_modbus_mutex, portMAX_DELAY) == pdTRUE) {
            val = s_modbus_registers[reg_offset];
            xSemaphoreGive(s_modbus_mutex);
        }
    }
    return val;
}
#else
// Non-ESP32 / Simulation Environment implementation
static uint16_t s_modbus_registers[NUM_MODBUS_REGISTERS] = {0};

void modbus_set_register(uint16_t reg_offset, uint16_t value) {
    if (reg_offset < NUM_MODBUS_REGISTERS) {
        s_modbus_registers[reg_offset] = value;
    }
}

uint16_t modbus_get_register(uint16_t reg_offset) {
    if (reg_offset < NUM_MODBUS_REGISTERS) {
        return s_modbus_registers[reg_offset];
    }
    return 0;
}
#endif

static uint16_t s_current_part = 1;

void modbus_set_current_part(uint16_t part) {
    if (part >= 1 && part <= 4) {
        s_current_part = part;
    }
    modbus_set_register(REG_CURRENT_PART, part);
}

void modbus_set_status(DownloadStatus status) {
    // Keep obsolete register updated for code compatibility
    modbus_set_register(REG_DOWNLOAD_STATUS, (uint16_t)status);
    
    if (s_current_part < 1 || s_current_part > 4) return;
    
    uint16_t base_status_val = (s_current_part - 1) * 10;
    uint16_t reg_offset = 1 + (s_current_part - 1) * 5;
    
    if (status == STATUS_DOWNLOADING) {
        modbus_set_register(reg_offset, base_status_val + 1); // Downloading (1, 11, 21, 31)
    } 
    else if (status == STATUS_DECODING) {
        modbus_set_register(reg_offset, base_status_val + 3); // Decompressing (3, 13, 23, 33)
    } 
    else if (status == STATUS_FLASHING) {
        // Set all active parts to Flashing
        for (int p = 1; p <= 4; p++) {
            uint16_t p_stat = modbus_get_register(1 + (p - 1) * 5);
            if (p_stat != 0 && p_stat != (p - 1) * 10 + 7) { // if not idle and not error
                modbus_set_register(1 + (p - 1) * 5, (p - 1) * 10 + 5); // Flashing (5, 15, 25, 35)
            }
        }
    } 
    else if (status == STATUS_COMPLETE) {
        // Set all parts to Completed, progress to 100, and PSRAM flag to 1
        for (int p = 1; p <= 4; p++) {
            modbus_set_register(1 + (p - 1) * 5, (p - 1) * 10 + 9); // Completed (9, 19, 29, 39)
            modbus_set_register(3 + (p - 1) * 5, 100);
            modbus_set_register(5 + (p - 1) * 5, 1);
        }
    } 
    else if (status == STATUS_ERROR) {
        modbus_set_register(reg_offset, base_status_val + 7); // Error (7, 17, 27, 37)
    }
}

void modbus_set_progress(uint16_t progress) {
    if (progress > 100) progress = 100;
    modbus_set_register(REG_PROGRESS_PERCENT, progress);
    
    // Check if in Flashing phase (any status is Flashing)
    bool is_flashing = false;
    for (int p = 1; p <= 4; p++) {
        uint16_t stat = modbus_get_register(1 + (p - 1) * 5);
        if (stat == (p - 1) * 10 + 5) { // Flashing (5, 15, 25, 35)
            is_flashing = true;
            break;
        }
    }
    
    if (is_flashing) {
        // Flashing progress is global, update all parts progress
        for (int p = 1; p <= 4; p++) {
            modbus_set_register(3 + (p - 1) * 5, progress);
        }
    } else {
        if (s_current_part >= 1 && s_current_part <= 4) {
            modbus_set_register(3 + (s_current_part - 1) * 5, progress);
        }
    }
}

void modbus_set_error(UpdateErrorCode error) {
    modbus_set_register(REG_ERROR_CODE, (uint16_t)error);
    
    if (error != ERR_NONE && s_current_part >= 1 && s_current_part <= 4) {
        uint16_t base_status_val = (s_current_part - 1) * 10;
        modbus_set_register(1 + (s_current_part - 1) * 5, base_status_val + 7); // Error (7, 17, 27, 37)
    }
}
