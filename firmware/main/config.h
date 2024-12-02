#ifndef CONFIG_H
#define CONFIG_H

#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// A Wrapper around ESP nvs_flash stroage

typedef struct config_item_t {
    nvs_entry_info_t info;
    bool ready;
    struct config_item_t *next;
    union {
        int8_t i8;
        uint8_t u8;
        int16_t i16;
        uint16_t u16;
        int32_t i32;
        uint32_t u32;
        int64_t i64;
        uint64_t u64;
        struct { size_t len; char * p; } str;
        struct { size_t len; void * p; } blob;
    };
} config_item_t;

typedef struct config_t {
} config_t;

extern config_t config;

esp_err_t config_init();
esp_err_t config_garbage_collect();

esp_err_t config_set_i32(const char *key, int32_t value);
esp_err_t config_set_u32(const char *key, uint32_t value);
esp_err_t config_set_str(const char *key, const char *value);
esp_err_t config_set_blob(const char *key, const void *value, size_t length);
esp_err_t config_erase_key(const char* key);

esp_err_t config_get_i32(const char *key, int32_t *out_value);
esp_err_t config_get_u32(const char *key, uint32_t *out_value);
esp_err_t config_get_str(const char *key, char *out_value, size_t *length);
esp_err_t config_get_blob(const char *key, void *out_value, size_t *length);
esp_err_t config_get_i32_with_default(const char *key, int32_t *out_value, int32_t default_value);
esp_err_t config_get_u32_with_default(const char *key, uint32_t *out_value, uint32_t default_value);
esp_err_t config_get_str_with_default(const char *key, char *out_value, size_t *length, const char *default_value);
esp_err_t config_get_blob_with_default(const char *key, void *out_value, size_t *length, const void *default_value, size_t default_length);

esp_err_t config_get_item(nvs_iterator_t *iter, config_item_t *item);
esp_err_t config_entry_find(nvs_type_t type, nvs_iterator_t *output_iterator);

esp_err_t config_print_item(config_item_t *item);

//esp_err_t del_config();

extern const char * config_namespace;

#define LOG_CONFIG_VALUE(var) do {ESP_LOGI(TAG, "get config %s = %ld", #var, var);} while (0) 

#endif


