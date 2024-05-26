#ifndef MAIN_H
#define MAIN_H

void motor_task(void *pvParameters);
void command_task(void *pvParameters);
#define MESSAGEBUFFSIZE 128
extern MessageBufferHandle_t messageBuffer;

#endif
