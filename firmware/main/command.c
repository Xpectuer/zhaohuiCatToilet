#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "TM1638_driver.h"
#include "drv8871_driver.h"
#include "freertos/message_buffer.h"
#include "motor.h"
#include "command.h"

MessageBufferHandle_t messageBuffer;
static const char *TAG = "command";

uint8_t hex_to_nibble(char c){
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return 0;
}

void command_task(void *pvParameters)
{
	size_t cmdlen = 0;
	char cmdbuf[MESSAGEBUFFSIZE];
	char *p = cmdbuf;
	uint8_t b;
	while (1) {
		cmdlen = xMessageBufferReceive(messageBuffer, cmdbuf, MESSAGEBUFFSIZE-1, portMAX_DELAY);
		if (0 == cmdlen) {
			ESP_LOGE(TAG, "Command reception failed from message buffer");
			continue;
		}
		cmdbuf[cmdlen] = '\0';
		ESP_LOGI(TAG, "Command (%d bytes): %s", cmdlen, cmdbuf);
		switch (cmdbuf[0]) {
			case 'w':
				p = cmdbuf + 1;
				for (int i = 0; i < TM1638_MEMSIZE; ++i){
					while (' ' == *p) p++;
					if (p >= cmdbuf + cmdlen){
						ESP_LOGW(TAG, "Command syntax warning: w: insufficient data length");
						break;
					}
					b = hex_to_nibble(*p++);

					while (' ' == *p) p++;
					if (p >= cmdbuf + cmdlen){
						ESP_LOGW(TAG, "Command syntax warning: w: insufficient data length");
						break;
					}
					b = (b << 4) | hex_to_nibble(*p++);

					TM1638_write_buffer(i, b);
				}
				TM1638_flush();
				break;
			case 's':
				p = cmdbuf + 2;
				for (int i = 0; i < TM1638_MEMSIZE; i=i+2){
                    if ((p >= cmdbuf + cmdlen) || (*p >= 128)) {
                        TM1638_write_buffer(i, TM1638_char_table[(int)' ']);
                        continue;
                    }
					TM1638_write_buffer(i, TM1638_char_table[(int)*p]);
                    p++;
                }
				TM1638_flush();
				break;
			case 'f':
                motor_next_state = M_Forward_Starting;
                motor_auto_stop();
                //DRV8871_forward_brake();
				break;
			case 'r':
                motor_next_state = M_Reverse_Starting;
                motor_auto_stop();
                //DRV8871_reverse_brake();
				break;
			case 'b':
                motor_next_state = M_Brake;
                motor_auto_stop();
                //DRV8871_brake();
				break;
			case 'c':
                motor_next_state = M_Coast;
                motor_auto_stop();
                //DRV8871_coast();
				break;
			case '+':
                DRV8871_speed_up();
				break;
			case '-':
                DRV8871_speed_down();
				break;
			default:
				ESP_LOGW(TAG, "Unrecognized command: %c", cmdbuf[0]);
				break;
		}
	}
}
