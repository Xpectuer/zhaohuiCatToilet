#ifndef MOTOR_H
#define MOTOR_H

#include "esp_err.h"

typedef enum motor_state_t {M_Idle=0, M_Forward, M_Reverse, M_Forward_Starting, M_Reverse_Starting, M_Brake, M_Coast} motor_state_t;
#define M_MASK 0xF

typedef enum action_state_t {A_Idle=0, A_Stop, A_Homing_Rev, A_Homing_Fwd, A_Cleaning_Fwd, A_Cleaning_Rev, A_Cleaning_Fwd_2} action_state_t;

//test operations
esp_err_t motor_auto_process();
esp_err_t motor_auto_stop();

//cat toilet operations
esp_err_t motor_start_cleaning();
esp_err_t motor_start_homing();
esp_err_t motor_stop_action();

//motor main task
void motor_task(void *pvParameters);

//motor control functions
esp_err_t motor_forward();
esp_err_t motor_reverse();
esp_err_t motor_brake();
esp_err_t motor_coast();
esp_err_t motor_speed_up();
esp_err_t motor_speed_down();

//get motor states
bool motor_is_busy();
action_state_t motor_get_action();
motor_state_t motor_get_state();
const char * motor_get_state_str();
const char * motor_get_action_str();
int motor_get_speed();

#endif
