#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"

void tcp_server_task(void *pvParameters);

#endif

