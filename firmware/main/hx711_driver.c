#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "assert.h"

#define ADDO_GPIO CONFIG_HX711_DT   // GPIO number for ADDO
#define ADSK_GPIO CONFIG_HX711_SCK  // GPIO number for ADSK

#define CHANNEL_A_x128 1 // channel A, 128x amplification
#define CHANNEL_B_x32  2 // channel B,  32x amplification
#define CHANNEL_A_x64  3 // channel A,  64x amplification

#define MAX(a,b) (((a)>(b)) ? (a) : (b))

static const char *TAG = "weight";
static const UBaseType_t dio_notify_index = 1;

static bool HX711_initialized = false;
static TaskHandle_t HX711_task_handle = NULL;
static long HX711_data = 0;

void HX711_DO_isr(void *pvParameters){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    assert(HX711_task_handle != NULL);
    vTaskNotifyGiveIndexedFromISR(HX711_task_handle, dio_notify_index, &xHigherPriorityTaskWoken);
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
	
	//configure GPIO for DO
	io_conf.intr_type = GPIO_INTR_NEGEDGE;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = (1ULL << ADDO_GPIO);
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "configure GPIO failed for DO");

    //install interrupt callback
    esp_err_t err_code = gpio_install_isr_service(ESP_INTR_FLAG_LOWMED);
    if (err_code == ESP_ERR_INVALID_STATE){
        ESP_LOGI(TAG, "GPIO ISR service already installed");
    }
    else {
        ESP_RETURN_ON_ERROR(err_code, TAG, "install GPIO ISR service GPIO failed");
    }

	ESP_RETURN_ON_ERROR(gpio_isr_handler_add(ADDO_GPIO, HX711_DO_isr, NULL), TAG, "configure GPIO failed for DO");

    gpio_set_level(ADSK_GPIO, 0);

    HX711_initialized = true;
    return ESP_OK;
}

long HX711_get_data(){
    return HX711_data;
}

long HX711_read(int next_sense){
    long count;
    unsigned char i;

    count=0;
    while(gpio_get_level(ADDO_GPIO));
    for (i=0;i<24;i++)
    {
        gpio_set_level(ADSK_GPIO, 1);
        count = count<<1; 
        gpio_set_level(ADSK_GPIO, 0); 
        if(gpio_get_level(ADDO_GPIO)) count++;
    }

    //setting transformation mode for next data
    for (i=0;i<24;i++)
    {
    gpio_set_level(ADSK_GPIO, 1);
    gpio_set_level(ADSK_GPIO, 0);
    }

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
            ulTaskNotifyTakeIndexed(dio_notify_index, pdTRUE, portMAX_DELAY);
        //enable interrupt on DO
        gpio_intr_disable(ADDO_GPIO);
        //read data from HX711
        HX711_data = HX711_read(CHANNEL_A_x128);
        //enable interrupt on DO
        gpio_intr_enable(ADDO_GPIO);
        printf("%s: %lx", TAG, HX711_data);
    }
}

