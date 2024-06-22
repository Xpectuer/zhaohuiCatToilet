#ifndef COMMAND_H
#define COMMAND_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MESSAGEBUFFSIZE 128
extern MessageBufferHandle_t messageBuffer;
void command_task(void *pvParameters);

#endif

