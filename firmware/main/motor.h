#ifndef MOTOR_H
#define MOTOR_H

#include "esp_err.h"
enum motor_state {M_Idle, M_Forward, M_Reverse, M_Forward_Starting, M_Reverse_Starting, M_Brake, M_Coast};
extern enum motor_state motor_next_state;
esp_err_t motor_auto_process();
esp_err_t motor_auto_stop();
void motor_task(void *pvParameters);

#endif
