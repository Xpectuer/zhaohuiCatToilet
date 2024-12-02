#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_check.h"
#include "motor.h"
#include "config.h"
#include "drv8871_driver.h"
#include "TM1638_driver.h"

#define BUTTON0 CONFIG_BUTTON0
#define BUTTON1 CONFIG_BUTTON1
#define BUTTON2 CONFIG_BUTTON2
#define BUTTON3 CONFIG_BUTTON3

#define BUTTON_PRESS_0      0x0100
#define BUTTON_PRESS_1      0x0200
#define BUTTON_PRESS_2      0x0400
#define BUTTON_PRESS_3      0x0800
#define ACT_START_CLEANING  0x1000
#define ACT_START_HOMING    0x2000
#define ACT_STOP_ACTION     0x4000
#define ACTION_MASK         0xFF00

static TaskHandle_t motor_task_handle = NULL;
static const UBaseType_t notify_index = 1;

//main action state
static action_state_t action_state = A_Idle;
//main motor state
static motor_state_t motor_state = M_Idle;

static int counter = 0;
static bool motor_auto = true;
//static int motor_auto_count = 0;
static const char *TAG = "motor";

const int PERIOD = 100;
const TickType_t xPeriod = PERIOD / portTICK_PERIOD_MS;
static int32_t BRAKE_TIME = 1000;
static int32_t START_TIME = 2000;
static int32_t HOMING_FORWARD_TIME = 5*1000;
static int32_t CLEANING_REVERSE_TIME_1 = 5*1000; //retry dumping
static int32_t CLEANING_REVERSE_TIME_2 = 15*1000; //return to normal
static int32_t CLEANING_FORWARD_TIME = 2*1000; //forward a little to level the catsand
static int32_t CLEANING_RETRY_NUM = 1; // retry 1 time(s) after first dumping attempt

// speed range from 0 to 100
static int32_t max_speed = 50;
const int SPEED_STEP = 5;

const char * const state_string[]={
    "Idle", "Forward", "Reverse", "Forward_Starting", "Reverse_Starting", "Brake", "Coast",
};

const char * const action_string[]={
    "Idle", "Stop", "Homing_Reverse", "Homing_Forward", "Cleaing_Forward", "Cleaning_Reverse", "Cleaning_Forward_2",
};

motor_state_t motor_get_state(){
    return motor_state;
}

action_state_t motor_get_action(){
    return action_state;
}

const char * motor_get_state_str(){
    return state_string[motor_state];
}

const char * motor_get_action_str(){
    return action_string[action_state];
}

int motor_get_speed(){
    return max_speed;
}

esp_err_t motor_auto_process(){
    motor_auto = true;
    //motor_auto_count = 0;
    //for (int i=0; i < 10; i++) {
    //    if (!motor_auto) break;
    //    motor_forward();
    //    vTaskDelay(5000 / portTICK_PERIOD_MS);
    //    motor_coast();
    //    vTaskDelay(1000 / portTICK_PERIOD_MS);
    //    if (!motor_auto) break;
    //    motor_reverse();
    //    vTaskDelay(5000 / portTICK_PERIOD_MS);
    //    motor_coast();
    //    vTaskDelay(1000 / portTICK_PERIOD_MS);
    //}
    return motor_start_homing();

    //return ESP_OK;
}

esp_err_t motor_auto_stop(){
    motor_auto = false;
    return ESP_OK;
}

esp_err_t motor_forward(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_reverse(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_brake(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Brake, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_coast(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Coast, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_speed_up(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    int max_speed_temp = max_speed + SPEED_STEP;
    max_speed = (max_speed_temp <= 100) ? max_speed_temp : 100;
    if (motor_state == M_Forward)
        xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward_Starting, eSetValueWithOverwrite);
    else if (motor_state == M_Reverse)
        xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse_Starting, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_speed_down(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    int max_speed_temp = max_speed - SPEED_STEP;
    max_speed = (max_speed_temp >= 0) ? max_speed_temp : 0;
    if (motor_state == M_Forward)
        xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward_Starting, eSetValueWithOverwrite);
    else if (motor_state == M_Reverse)
        xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse_Starting, eSetValueWithOverwrite);
    return ESP_OK;
}

esp_err_t motor_start_cleaning(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    ESP_RETURN_ON_FALSE((!motor_is_busy()), ESP_ERR_INVALID_STATE, TAG, "motor is busy: %s", motor_get_action_str());
    motor_auto = false;
    xTaskNotifyIndexed(motor_task_handle, notify_index, ACT_START_CLEANING, eSetBits);
    return ESP_OK;
}

esp_err_t motor_start_homing(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    ESP_RETURN_ON_FALSE((!motor_is_busy()), ESP_ERR_INVALID_STATE, TAG, "motor is busy: %s", motor_get_action_str());
    motor_auto = false;
    xTaskNotifyIndexed(motor_task_handle, notify_index, ACT_START_HOMING, eSetBits);
    return ESP_OK;
}

esp_err_t motor_stop_action(){
    ESP_RETURN_ON_FALSE((NULL != motor_task_handle), ESP_FAIL, TAG, "motor task not initialized");
    motor_auto = false;
    xTaskNotifyIndexed(motor_task_handle, notify_index, ACT_STOP_ACTION, eSetBits);
    return ESP_OK;
}

void button_isr(void *pvParameters){
    uint32_t button_pressed = (uint32_t)pvParameters;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    assert(NULL != motor_task_handle);
    // send button press notification
    xTaskNotifyIndexedFromISR(motor_task_handle, notify_index, button_pressed, eSetBits, &xHigherPriorityTaskWoken);
    // request a context switch if a higher priority task is woken
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

esp_err_t init_sensors(){
    gpio_config_t io_conf = {};

    //configure GPIO for SENSORS
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON0) | (1ULL << BUTTON1) | (1ULL << BUTTON2) | (1ULL << BUTTON3);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configure GPIO failed for SENSORS");

    //first dump
    gpio_dump_io_configuration(stdout, io_conf.pin_bit_mask);

    //install callback service for all GPIOs
    esp_err_t err_code = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED);
    if (err_code == ESP_ERR_INVALID_STATE){
        ESP_LOGW(TAG, "GPIO ISR service already installed");
    }
    else {
        ESP_RETURN_ON_ERROR(err_code, TAG, "install GPIO ISR service GPIO failed");
    }

    //install interrupt callback
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTTON0, button_isr, (void*)BUTTON_PRESS_0), TAG, "configure GPIO failed for BUTTON0");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTTON1, button_isr, (void*)BUTTON_PRESS_1), TAG, "configure GPIO failed for BUTTON1");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTTON2, button_isr, (void*)BUTTON_PRESS_2), TAG, "configure GPIO failed for BUTTON2");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTTON3, button_isr, (void*)BUTTON_PRESS_3), TAG, "configure GPIO failed for BUTTON3");

    //enable interrupt
    gpio_intr_enable(BUTTON0);
    gpio_intr_enable(BUTTON1);
    gpio_intr_enable(BUTTON2);
    gpio_intr_enable(BUTTON3);

    //enable pullup
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(BUTTON0, GPIO_PULLUP_ONLY), TAG, "enable pullop on pin %d failed", BUTTON0);
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(BUTTON1, GPIO_PULLUP_ONLY), TAG, "enable pullop on pin %d failed", BUTTON1);
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(BUTTON2, GPIO_PULLUP_ONLY), TAG, "enable pullop on pin %d failed", BUTTON2);
    ESP_RETURN_ON_ERROR(gpio_set_pull_mode(BUTTON3, GPIO_PULLUP_ONLY), TAG, "enable pullop on pin %d failed", BUTTON3);

    //second dump
    gpio_dump_io_configuration(stdout, io_conf.pin_bit_mask);

    return ESP_OK;
}

//process button and command actions
motor_state_t process_actions(uint32_t notify_value){
    motor_state_t next_state = notify_value & M_MASK;
    static int cleaning_reverse_count = 0;

    if (notify_value & ~M_MASK) {
        ESP_LOGI(TAG, "get action event: %04lx", notify_value & ~M_MASK);
        //clearing command
        ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ~M_MASK);
    }
    if (ACT_STOP_ACTION & notify_value) {
        action_state = A_Stop;
        next_state = M_Coast;
    }
    switch (action_state){
        case A_Idle:
            //start action if corresponding notify triggered
            if (ACT_START_HOMING & notify_value) {
                action_state = A_Homing_Rev;
                next_state = M_Reverse;
                ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, M_MASK);
                xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse, eSetBits);
            }
            if (ACT_START_CLEANING & notify_value) {
                action_state = A_Cleaning_Fwd;
                next_state = M_Forward;
                cleaning_reverse_count = 0;
                ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, M_MASK);
                xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward, eSetBits);
            }
            break;
        case A_Homing_Rev:
            //reverse until hit the switches
            if (BUTTON_PRESS_0 & notify_value) {
                action_state = A_Homing_Fwd;
                next_state = M_Forward;
                ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, M_MASK);
                xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward, eSetBits);
            }
            if (BUTTON_PRESS_2 & notify_value) {
                action_state = A_Homing_Fwd;
                next_state = M_Forward;
                ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, M_MASK);
                xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward, eSetBits);
            }
            break;
        case A_Homing_Fwd:
            //forward for a predefined period of time
            if (M_Forward == motor_state && counter * PERIOD >= HOMING_FORWARD_TIME) {
                action_state = A_Stop;
                next_state = M_Coast;
            }
            break;
        case A_Cleaning_Fwd:
            //reverse until hit the switches
            if (BUTTON_PRESS_1 & notify_value) {
                action_state = A_Cleaning_Rev;
                next_state = M_Reverse;
                ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, M_MASK);
                xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse, eSetBits);
            }
            if (BUTTON_PRESS_3 & notify_value) {
                action_state = A_Cleaning_Rev;
                next_state = M_Reverse;
                ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, M_MASK);
                xTaskNotifyIndexed(motor_task_handle, notify_index, M_Reverse, eSetBits);
            }
            break;
        case A_Cleaning_Rev:
            //retry dumping for CLEANING_RETRY_NUM times
            if (cleaning_reverse_count < CLEANING_RETRY_NUM){
                //reverse for a predefined period of time
                if (M_Reverse == motor_state && counter * PERIOD >= CLEANING_REVERSE_TIME_1) {
                    action_state = A_Cleaning_Fwd;
                    next_state = M_Forward;
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, M_MASK);
                    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward, eSetBits);
                    cleaning_reverse_count++;
                }
            } else {
                //reverse for a predefined period of time
                if ((M_Reverse == motor_state && counter * PERIOD >= CLEANING_REVERSE_TIME_2)
                        || ((BUTTON_PRESS_0 | BUTTON_PRESS_2) & notify_value)){
                    action_state = A_Cleaning_Fwd_2;
                    next_state = M_Forward;
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, M_MASK);
                    xTaskNotifyIndexed(motor_task_handle, notify_index, M_Forward, eSetBits);
                    cleaning_reverse_count = 0;
                }
            }
            break;
        case A_Cleaning_Fwd_2:
            if (M_Forward == motor_state && counter * PERIOD >= CLEANING_FORWARD_TIME) {
                action_state = A_Stop;
                next_state = M_Coast;
            }
            break;
        case A_Stop:
            if (M_Idle == motor_state) {
                action_state = A_Idle;
                next_state = M_Idle;
            }
            break;
    }
    static action_state_t prev_action = A_Idle;
    if (action_state != prev_action)
        ESP_LOGI(TAG, "action: %s", motor_get_action_str());
    prev_action = action_state;
    return next_state;
}

bool motor_is_busy(){
    return A_Idle != action_state;
}

static esp_err_t motor_get_config(){
    esp_err_t ret = ESP_OK;
    if (ESP_OK != (ret = config_get_i32_with_default("mtr_speed", &max_speed, 50))) return ret;
    LOG_CONFIG_VALUE(max_speed);
    if (ESP_OK != (ret = config_get_i32_with_default("mtr_brake_time", &BRAKE_TIME, 1000))) return ret;
    LOG_CONFIG_VALUE(BRAKE_TIME);
    if (ESP_OK != (ret = config_get_i32_with_default("mtr_start_time", &START_TIME, 2000))) return ret;
    LOG_CONFIG_VALUE(START_TIME);
    if (ESP_OK != (ret = config_get_i32_with_default("mtr_hom_t", &HOMING_FORWARD_TIME, 5*1000))) return ret;
    LOG_CONFIG_VALUE(HOMING_FORWARD_TIME);
    if (ESP_OK != (ret = config_get_i32_with_default("mtr_cln_t1", &CLEANING_REVERSE_TIME_1, 5*1000))) return ret;
    LOG_CONFIG_VALUE(CLEANING_REVERSE_TIME_1);
    if (ESP_OK != (ret = config_get_i32_with_default("mtr_cln_t2", &CLEANING_REVERSE_TIME_2, 15*1000))) return ret;
    LOG_CONFIG_VALUE(CLEANING_REVERSE_TIME_2);
    if (ESP_OK != (ret = config_get_i32_with_default("mtr_cln_retry", &CLEANING_RETRY_NUM, 1))) return ret;
    LOG_CONFIG_VALUE(CLEANING_RETRY_NUM);
    return ESP_OK;
}

void motor_task(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    motor_task_handle = xTaskGetCurrentTaskHandle();
    esp_err_t ret = ESP_OK;

    int speed;
    motor_state_t next_state;
    static motor_state_t prev_motor_state = M_Idle;
    
    ret = motor_get_config();
    if (ESP_OK != ret) ESP_LOGW(TAG, "get_config failed for motor: %s", esp_err_to_name(ret));

    if(ESP_OK != (ret = init_sensors())){
        ESP_LOGE(TAG, "initialize limit switches and infrared sensors failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
    }

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
        counter++;

        //get and don't clear next motor state
        uint32_t temp_state;
        temp_state = ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, 0);
        //process button events
        next_state = process_actions(temp_state);
        //clear pending state for notification
        xTaskNotifyStateClearIndexed(motor_task_handle, notify_index);

        switch(motor_state) {
            case M_Idle:
                //wait for motor command
                if (next_state == M_Idle){
                    xTaskNotifyWaitIndexed(notify_index, 0, 0, &temp_state, portMAX_DELAY);
                    //process button events
                    next_state = process_actions(temp_state);
                    //update wake timer
                    xLastWakeTime += (xTaskGetTickCount() - xLastWakeTime) / xPeriod * xPeriod;
                }
                //handle commands
                if (next_state == M_Forward){
                    motor_state = M_Forward_Starting;
                    DRV8871_set_speed(0);
                    DRV8871_forward_brake();
                    counter = 0;
                }
                else if (next_state == M_Reverse){
                    motor_state = M_Reverse_Starting;
                    DRV8871_set_speed(0);
                    DRV8871_reverse_brake();
                    counter = 0;
                }
                //clearing command
                ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                break;
            case M_Forward_Starting:
                if (next_state == M_Brake){
                    motor_state = M_Brake;
                    DRV8871_brake();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Coast) {
                    motor_state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Reverse) {
                    //coast and reverse
                    motor_state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //keeping command
                } else {
                    speed = DRV8871_get_speed();
                    if (speed - 100 * PERIOD / START_TIME > max_speed){
                        //too fast, speed down
                        speed = speed - 100 * PERIOD / START_TIME;
                    } else if (speed + 100 * PERIOD / START_TIME < max_speed) {
                        //too slow, speed up
                        speed = speed + 100 * PERIOD / START_TIME;
                    } else {
                        speed = max_speed;
                        motor_state = M_Forward;
                    }
                    DRV8871_set_speed(speed);
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                }
                break;
            case M_Reverse_Starting:
                if (next_state == M_Brake){
                    motor_state = M_Brake;
                    DRV8871_brake();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Coast) {
                    motor_state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Forward) {
                    //coast and forward
                    motor_state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //keeping command
                } else {
                    speed = DRV8871_get_speed();
                    if (speed - 100 * PERIOD / START_TIME > max_speed){
                        //too fast, speed down
                        speed = speed - 100 * PERIOD / START_TIME;
                    } else if (speed + 100 * PERIOD / START_TIME < max_speed) {
                        //too slow, speed up
                        speed = speed + 100 * PERIOD / START_TIME;
                    } else {
                        speed = max_speed;
                        motor_state = M_Reverse;
                    }
                    DRV8871_set_speed(speed);
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                }
                break;
            case M_Forward:
                if (next_state == M_Forward_Starting){
                    //start changing speed
                    motor_state = M_Forward_Starting;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Brake) {
                    motor_state = M_Brake;
                    DRV8871_brake();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Coast) {
                    motor_state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Reverse){
                    //coast and reverse
                    motor_state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //keeping command
                } else {
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                }
                break;
            case M_Reverse:
                if (next_state == M_Reverse_Starting){
                    //start changing speed
                    motor_state = M_Reverse_Starting;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Brake) {
                    motor_state = M_Brake;
                    DRV8871_brake();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Coast) {
                    motor_state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                } else if (next_state == M_Forward){
                    //coast and forward
                    motor_state = M_Coast;
                    DRV8871_coast();
                    counter = 0;
                    //keeping command
                } else {
                    //clearing command
                    ulTaskNotifyValueClearIndexed(motor_task_handle, notify_index, ULONG_MAX);
                }
                break;
            case M_Brake:
                //don't clear comand until braking for enough time
                if (counter >= BRAKE_TIME / PERIOD) {
                    motor_state = M_Idle;
                    DRV8871_coast();
                    counter = 0;
                }
                break;
            case M_Coast:
                //don't clear comand until coasting for enough time
                if (counter >= BRAKE_TIME / PERIOD) {
                    motor_state = M_Idle;
                    DRV8871_coast();
                    counter = 0;
                }
                break;
        }
        if (motor_state != prev_motor_state)
            ESP_LOGI(TAG, "state: %s", motor_get_state_str());
        prev_motor_state = motor_state;

    }

}
