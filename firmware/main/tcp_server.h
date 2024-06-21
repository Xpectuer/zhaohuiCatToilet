#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"

#define MESSAGEBUFFSIZE 128
extern MessageBufferHandle_t messageBuffer;
void tcp_server_task(void *pvParameters);
esp_err_t tcp_server_init();

#endif

