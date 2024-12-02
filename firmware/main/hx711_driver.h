#ifndef HX711_DRIVER_H
#define HX711_DRIVER_H

#include "esp_err.h"
#include "stdio.h"
long HX711_get_data();
void HX711_task(void *pvParameters);
esp_err_t HX711_init();

void HX711_toggle_output(FILE* stream);
void HX711_enable_output(FILE* stream);
void HX711_disable_output();

#define HX711_READ_ERROR 0x1FFFFFF

#endif

