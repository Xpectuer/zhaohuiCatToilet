#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "motor.h"
#include "drv8871_driver.h"
#include "TM1638_driver.h"

static enum motor_state state = M_Idle;
static int counter = 0;
enum motor_state motor_next_state = M_Idle;
static bool motor_auto = true;
static const char *TAG = "motor";

const int PERIOD = 100;
const int BRAKE_TIME = 1000;
const int START_TIME = 1000;

esp_err_t motor_auto_process(){
    motor_auto = true;
    for (int i=0; i < 10; i++) {
        if (!motor_auto) break;
        motor_next_state = M_Forward_Starting;
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        motor_next_state = M_Coast;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        if (!motor_auto) break;
        motor_next_state = M_Reverse_Starting;
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        motor_next_state = M_Coast;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    return ESP_OK;
}

esp_err_t motor_auto_stop(){
    motor_auto = false;
    return ESP_OK;
}

void motor_task(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = PERIOD / portTICK_PERIOD_MS;
    uint32_t speed;

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        counter++;

        switch(state) {
            case M_Idle:
                if (motor_next_state == M_Forward_Starting){
                    state = M_Forward_Starting;
                    DRV8871_set_speed(0);
                    DRV8871_forward_brake();
                    motor_next_state = M_Idle;
                    counter = 0;
                    break;
                }
                if (motor_next_state == M_Reverse_Starting){
                    state = M_Reverse_Starting;
                    DRV8871_set_speed(0);
                    DRV8871_reverse_brake();
                    motor_next_state = M_Idle;
                    counter = 0;
                    break;
                }
                if (motor_next_state == M_Forward){
                    state = M_Forward;
                    DRV8871_forward();
                    motor_next_state = M_Idle;
                    counter = 0;
                    break;
                }
                if (motor_next_state == M_Reverse){
                    state = M_Reverse;
                    DRV8871_reverse();
                    motor_next_state = M_Idle;
                    counter = 0;
                    break;
                }
                motor_next_state = M_Idle;
                break;
            case M_Forward:
            case M_Reverse:
                if (motor_next_state == M_Brake){
                    state = M_Brake;
                    DRV8871_brake();
                    motor_next_state = M_Idle;
                    counter = 0;
                    break;
                }
                if (motor_next_state == M_Coast){
                    state = M_Coast;
                    DRV8871_coast();
                    motor_next_state = M_Idle;
                    counter = 0;
                    break;
                }
                break;
            case M_Forward_Starting:
                speed = DRV8871_get_speed() + 100 * PERIOD/START_TIME;
                if (speed > 100) speed = 100;
                DRV8871_set_speed(speed);
                if (speed == 100) {
                    state = M_Forward;
                    DRV8871_forward();
                    counter = 0;
                }
                break;
            case M_Reverse_Starting:
                speed = DRV8871_get_speed() + 100 * PERIOD/START_TIME;
                if (speed > 100) speed = 100;
                DRV8871_set_speed(speed);
                if (speed == 100) {
                    state = M_Reverse;
                    DRV8871_reverse();
                    counter = 0;
                }
                break;
            case M_Brake:
                if (counter >= BRAKE_TIME / PERIOD) {
                    state = M_Idle;
                    DRV8871_coast();
                    counter = 0;
                    break;
                }
                break;
            case M_Coast:
                if (counter >= BRAKE_TIME / PERIOD) {
                    state = M_Idle;
                    DRV8871_coast();
                    counter = 0;
                    break;
                }
                break;
        }

    }

}
