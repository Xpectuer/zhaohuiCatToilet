#ifndef DRV8871_DRIVER_H
#define DRV8871_DRIVER_H

#include "esp_err.h"

//#define DRV8871_A      CONFIG_DRV8871_A
//#define DRV8871_B      CONFIG_DRV8871_B

//extern const uint8_t DRV8871_char_table[];
esp_err_t DRV8871_init(void);

esp_err_t DRV8871_set_speed(uint32_t speed);
esp_err_t DRV8871_forward(void);
esp_err_t DRV8871_reverse(void);
esp_err_t DRV8871_forward_brake(void);
esp_err_t DRV8871_reverse_brake(void);
esp_err_t DRV8871_coast(void);
esp_err_t DRV8871_brake(void);

#endif
