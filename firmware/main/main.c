#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "lwip/sockets.h"

#include "tcp_server.h"
#include "TM1638_driver.h"
#include "drv8871_driver.h"
#include "motor.h"
#include "command.h"

void app_main(void)
{
    ESP_ERROR_CHECK(TM1638_init());
    ESP_ERROR_CHECK(DRV8871_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(tcp_server_init());

    //gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);

    xTaskCreate(motor_task, "motor", 4096, NULL, 5, NULL);
    xTaskCreate(command_task, "command", 4096, NULL, 5, NULL);

#ifdef CONFIG_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
#endif
#ifdef CONFIG_IPV6
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET6, 5, NULL);
#endif

    ESP_ERROR_CHECK(motor_auto_process());
}
