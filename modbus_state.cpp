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

void modbus_set_status(DownloadStatus status) {
    modbus_set_register(REG_DOWNLOAD_STATUS, (uint16_t)status);
}

void modbus_set_progress(uint16_t progress) {
    if (progress > 100) progress = 100;
    modbus_set_register(REG_PROGRESS_PERCENT, progress);
}

void modbus_set_error(UpdateErrorCode error) {
    modbus_set_register(REG_ERROR_CODE, (uint16_t)error);
}

void modbus_set_current_part(uint16_t part) {
    modbus_set_register(REG_CURRENT_PART, part);
}
