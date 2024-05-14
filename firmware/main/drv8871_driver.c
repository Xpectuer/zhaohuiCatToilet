#include <inttypes.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "bdc_motor.h"
#include "bdc_motor_interface.h"
#include "driver/mcpwm_prelude.h"

#include "drv8871_driver.h"

#define BDC_MCPWM_TIMER_RESOLUTION_HZ 1000000 // 1MHz, 1 tick = 1us
#define BDC_MCPWM_FREQ_HZ             50000    // 50KHz PWM
#define BDC_MCPWM_DUTY_TICK_MAX       (BDC_MCPWM_TIMER_RESOLUTION_HZ / BDC_MCPWM_FREQ_HZ) // maximum value we can set for the duty cycle, in ticks
#define BDC_MCPWM_GPIO_A              CONFIG_DRV8871_A
#define BDC_MCPWM_GPIO_B              CONFIG_DRV8871_B

static esp_err_t bdc_motor_mcpwm_forward_brake(bdc_motor_t *motor);
static esp_err_t bdc_motor_mcpwm_reverse_brake(bdc_motor_t *motor);


static const char *TAG = "motor";
static bdc_motor_handle_t motor = NULL;
static bool brake_mode = false;
static uint32_t speed_percent = 0;

esp_err_t DRV8871_init(void){

    ESP_LOGI(TAG, "Create DC motor");
    bdc_motor_config_t motor_config = {
        .pwm_freq_hz = BDC_MCPWM_FREQ_HZ,
        .pwma_gpio_num = BDC_MCPWM_GPIO_A,
        .pwmb_gpio_num = BDC_MCPWM_GPIO_B,
    };
    bdc_motor_mcpwm_config_t mcpwm_config = {
        .group_id = 0,
        .resolution_hz = BDC_MCPWM_TIMER_RESOLUTION_HZ,
    };
    ESP_RETURN_ON_ERROR(bdc_motor_new_mcpwm_device(&motor_config, &mcpwm_config, &motor), TAG, "Initialize bdc motor failed");

    ESP_LOGI(TAG, "Enable motor");
    ESP_RETURN_ON_ERROR(bdc_motor_enable(motor), TAG, "Enable motor failed");
    ESP_RETURN_ON_ERROR(bdc_motor_coast(motor), TAG, "Coast motor failed");
    brake_mode = false;
    speed_percent = 0;
    return ESP_OK;
}

esp_err_t DRV8871_set_speed(uint32_t speed){
    ESP_RETURN_ON_FALSE(speed <= 100, ESP_ERR_INVALID_ARG, TAG, "invalid argument: %"PRIu32, speed);
    speed_percent = speed;
    ESP_LOGI(TAG, "Set motor speed: %"PRIu32, speed);
    uint32_t comp_value = speed * BDC_MCPWM_DUTY_TICK_MAX /100;
    if (brake_mode)
        comp_value = BDC_MCPWM_DUTY_TICK_MAX - comp_value;
    return bdc_motor_set_speed(motor, comp_value);
};

esp_err_t DRV8871_forward(void){
    brake_mode = false;
    ESP_LOGI(TAG, "Forward motor");
    ESP_RETURN_ON_ERROR(DRV8871_set_speed(speed_percent), TAG, "Setting speed failed");
    return bdc_motor_forward(motor);
}

esp_err_t DRV8871_reverse(void){
    brake_mode = false;
    ESP_LOGI(TAG, "Reverse motor");
    ESP_RETURN_ON_ERROR(DRV8871_set_speed(speed_percent), TAG, "Setting speed failed");
    return bdc_motor_reverse(motor);
}

esp_err_t DRV8871_forward_brake(void){
    brake_mode = true;
    ESP_LOGI(TAG, "Forward motor");
    ESP_RETURN_ON_ERROR(DRV8871_set_speed(speed_percent), TAG, "Setting speed failed");
    return bdc_motor_mcpwm_forward_brake(motor);
}

esp_err_t DRV8871_reverse_brake(void){
    brake_mode = true;
    ESP_LOGI(TAG, "Reverse motor");
    ESP_RETURN_ON_ERROR(DRV8871_set_speed(speed_percent), TAG, "Setting speed failed");
    return bdc_motor_mcpwm_reverse_brake(motor);
}

esp_err_t DRV8871_coast(void){
    ESP_LOGI(TAG, "Coast motor");
    return bdc_motor_coast(motor);
}

esp_err_t DRV8871_brake(void){
    ESP_LOGI(TAG, "Brake motor");
    return bdc_motor_brake(motor);
}

//extend bdc_motor mode
typedef struct {
    bdc_motor_t base;
    mcpwm_timer_handle_t timer;
    mcpwm_oper_handle_t operator;
    mcpwm_cmpr_handle_t cmpa;
    mcpwm_cmpr_handle_t cmpb;
    mcpwm_gen_handle_t gena;
    mcpwm_gen_handle_t genb;
} bdc_motor_mcpwm_obj;

static esp_err_t bdc_motor_mcpwm_forward_brake(bdc_motor_t *motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->genb, -1, true), TAG, "disable force level for genb failed");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->gena, 1, true), TAG, "set force level for gena failed");
    return ESP_OK;
}

static esp_err_t bdc_motor_mcpwm_reverse_brake(bdc_motor_t *motor)
{
    ESP_RETURN_ON_FALSE(motor, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    bdc_motor_mcpwm_obj *mcpwm_motor = __containerof(motor, bdc_motor_mcpwm_obj, base);
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->gena, -1, true), TAG, "disable force level for gena failed");
    ESP_RETURN_ON_ERROR(mcpwm_generator_set_force_level(mcpwm_motor->genb, 1, true), TAG, "set force level for genb failed");
    return ESP_OK;
}
