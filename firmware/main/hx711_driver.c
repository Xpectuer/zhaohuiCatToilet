#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "assert.h"
#include "stdio.h"
#include "inttypes.h"

#include "hx711_driver.h"

#define ADDO_GPIO CONFIG_HX711_DT   // GPIO number for ADDO
#define ADSK_GPIO CONFIG_HX711_SCK  // GPIO number for ADSK

#define CHANNEL_A_x128 1 // channel A, 128x amplification
#define CHANNEL_B_x32  2 // channel B,  32x amplification
#define CHANNEL_A_x64  3 // channel A,  64x amplification

#define MAX(a,b) (((a)>(b)) ? (a) : (b))

static const char *TAG = "weight";
static const UBaseType_t do_notify_index = 1;

static bool HX711_initialized = false;
static TaskHandle_t HX711_task_handle = NULL;
static long HX711_data = 0;
static bool output_enable = false;
static FILE* output_stream = NULL;

void HX711_DO_isr(void *pvParameters){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    assert(HX711_task_handle != NULL);
    vTaskNotifyGiveIndexedFromISR(HX711_task_handle, do_notify_index, &xHigherPriorityTaskWoken);
    // request a context switch if a higher priority task is woken
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

esp_err_t HX711_init(){
    gpio_config_t io_conf = {};
    
    //configure GPIO for CLK
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << ADSK_GPIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configure GPIO failed for CLK");
    ESP_RETURN_ON_ERROR(gpio_set_drive_capability(ADSK_GPIO, GPIO_DRIVE_CAP_0), TAG, "setting GPIO driving capability failed for CLK");
    
    //configure GPIO for DO
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ADDO_GPIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configure GPIO failed for DO");

    //install callback service for all GPIOs
    esp_err_t err_code = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED);
    if (err_code == ESP_ERR_INVALID_STATE){
        ESP_LOGW(TAG, "GPIO ISR service already installed");
    }
    else {
        ESP_RETURN_ON_ERROR(err_code, TAG, "install GPIO ISR service GPIO failed");
    }

    //install interrupt callback
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(ADDO_GPIO, HX711_DO_isr, NULL), TAG, "configure GPIO failed for DO");

    //set CLK=0
    gpio_set_level(ADSK_GPIO, 0);

    //set output stream
    output_stream = stdout;

    HX711_initialized = true;
    return ESP_OK;
}

long HX711_get_data(){
    return HX711_data;
}

void HX711_toggle_output(FILE* stream){
    if (NULL != stream && NULL != output_stream && stream != output_stream)
        HX711_enable_output(stream);
    else if (output_enable)
        HX711_disable_output();
    else
        HX711_enable_output(stream);
}

void HX711_enable_output(FILE* stream){
    if (NULL != stream){
        if (NULL != output_stream && stream != output_stream){
            //redirect to another terminal
            fprintf(output_stream, "%s: output redirected to another terminal\n", TAG);
            fflush(output_stream);
        }
        output_stream = stream;
    }
    output_enable = true;
}

void HX711_disable_output(){
    output_enable = false;
}

long HX711_read(int next_sense){
    long count;
    unsigned char i;

    assert(next_sense >= 1 && next_sense <= 3);

    count = 0;
    //shift in data while setting transformation mode for next data
    taskDISABLE_INTERRUPTS();
    for (i = 0; i < 24+next_sense; i++) {
        gpio_set_level(ADSK_GPIO, 1);
        count = count << 1; 
        gpio_set_level(ADSK_GPIO, 0); 
        count |= gpio_get_level(ADDO_GPIO) & 0x1;
    }
    taskENABLE_INTERRUPTS();

    //check input data at last 1~3 clocks
    if (~count & ((0x1<<next_sense) - 1)) {
        //DO should be 1 at the last few clocks
        //a constant 0 signal on DO may indicate no connection
        count = HX711_READ_ERROR;
        return count;
    }
    //discard extra bits
    count = count >> next_sense;

    //handle 2's compliment for 24 bit input
    if (count > 0x800000)
        count = count - 0x1000000;

    return count;
}

void HX711_reset(){
    //hold SCK high for > 60us to reset HX711
    gpio_set_level(ADSK_GPIO, 1);
    vTaskDelay(MAX(60 / portTICK_PERIOD_MS / 1000,1));
    gpio_set_level(ADSK_GPIO, 0);
}

void HX711_task(void *pvParameters){
    //get current task handle for ISR
    HX711_task_handle = xTaskGetCurrentTaskHandle();

    //initialize HX711
    if (!HX711_initialized)
        HX711_init();

    HX711_reset();

    gpio_intr_enable(ADDO_GPIO);

    while (1) {
        //wait for notification from ISR if not ready
        if (gpio_get_level(ADDO_GPIO) == 1)
            ulTaskNotifyTakeIndexed(do_notify_index, pdTRUE, portMAX_DELAY);
        //disable interrupt on DO
        gpio_intr_disable(ADDO_GPIO);
        //read data from HX711
        HX711_data = HX711_read(CHANNEL_A_x128);
        //enable interrupt on DO
        gpio_intr_enable(ADDO_GPIO);
        //handling read error
        if (HX711_data == HX711_READ_ERROR) {
            ESP_LOGE(TAG, "Error Reading HX711 data");
            //wait 10s for retry
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            HX711_reset();
            continue;
        }
        if (output_enable){
            fprintf(output_stream, "(%"PRIu32") %s: %ld\n", esp_log_timestamp(), TAG, HX711_data);
            fflush(output_stream);
        }
        //delay 50 ms to prevent spamming output
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

