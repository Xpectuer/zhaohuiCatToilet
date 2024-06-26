/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/message_buffer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "TM1638_driver.h"
#include "drv8871_driver.h"
#include "driver/gpio.h"

#define PORT                        CONFIG_PORT
#define KEEPALIVE_IDLE              CONFIG_KEEPALIVE_IDLE
#define KEEPALIVE_INTERVAL          CONFIG_KEEPALIVE_INTERVAL
#define KEEPALIVE_COUNT             CONFIG_KEEPALIVE_COUNT


static const char *TAG = "example";
static MessageBufferHandle_t messageBuffer;
#define BUFFSIZE 128
#define STREAMBUFFSIZE 256

static void do_retransmit(const int sock)
{
    int len;
    char rx_buffer[BUFFSIZE];
	size_t cmdlen = 0;
	char cmdbuf[BUFFSIZE];

	char c = 0;
	bool escape = false;
	bool overflow = false;

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            //ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);
            ESP_LOGI(TAG, "Received %d bytes", len);

			for (size_t i = 0; i < len; ++i){
				c = rx_buffer[i];
				if (escape) {
					if (cmdlen >= BUFFSIZE-1){
						if (!overflow){
							overflow = true;
							ESP_LOGW("stream", "Max command length %d exceeded", BUFFSIZE);
						}
					} else cmdbuf[cmdlen++] = c;
					escape = false;
				} else {
					switch (c) {
						case '\\':
							escape = true;
							break;
						case '\n':
							// send data to command processing task
							xMessageBufferSend(messageBuffer, cmdbuf, cmdlen, portMAX_DELAY);
							overflow = false;
							cmdlen = 0;
							break;
						default:
							if (cmdlen >= BUFFSIZE){
								if (!overflow){
									overflow = true;
									ESP_LOGW("stream", "Max command length %d exceeded", BUFFSIZE);
								}
							} else cmdbuf[cmdlen++] = c;
							break;
					}
				}
			}


            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
			/*
            int to_write = len;
            while (to_write > 0) {
                int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
                to_write -= written;
            }
			*/
        }
    } while (len > 0);
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }
#ifdef CONFIG_IPV6
    else if (addr_family == AF_INET6) {
        struct sockaddr_in6 *dest_addr_ip6 = (struct sockaddr_in6 *)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un, sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_IPV4) && defined(CONFIG_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
#ifdef CONFIG_IPV6
        else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

uint8_t hex_to_nibble(char c){
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return 0;
}

static void command_task(void *pvParameters)
{
	size_t cmdlen = 0;
	char cmdbuf[BUFFSIZE];
	char *p = cmdbuf;
	uint8_t b;
	while (1) {
		cmdlen = xMessageBufferReceive(messageBuffer, cmdbuf, BUFFSIZE-1, portMAX_DELAY);
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
				break;
			case 'r':
				break;
			default:
				ESP_LOGW(TAG, "Unrecognized command: %c", cmdbuf[0]);
				break;
		}
	}
}


void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(TM1638_init());
    ESP_ERROR_CHECK(DRV8871_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

	messageBuffer = xMessageBufferCreate(STREAMBUFFSIZE);
	if (NULL == messageBuffer){
		ESP_LOGE("main", "Failed to create message buffer");
		abort();
	}

    xTaskCreate(command_task, "command", 4096, NULL, 5, NULL);

#ifdef CONFIG_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
#endif
#ifdef CONFIG_IPV6
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET6, 5, NULL);
#endif

    for (int i=0; i < 100; i++){
        ESP_ERROR_CHECK(DRV8871_set_speed(0));
        if (i & 1)
            ESP_ERROR_CHECK(DRV8871_forward_brake());
        else
            ESP_ERROR_CHECK(DRV8871_reverse_brake());
        for (int j=0; j <= 100; j=j+5 ){
            ESP_ERROR_CHECK(DRV8871_set_speed(j));
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        if (i & 1)
            ESP_ERROR_CHECK(DRV8871_forward());
        else
            ESP_ERROR_CHECK(DRV8871_reverse());
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK(DRV8871_coast());
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}
