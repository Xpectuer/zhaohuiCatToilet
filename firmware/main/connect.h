#ifndef CONNECT_H
#define CONNECT_H

#include "esp_err.h"
esp_err_t wifi_connect();
esp_err_t wifi_start();
esp_err_t wifi_stop();
esp_err_t print_ip_info();

#endif


