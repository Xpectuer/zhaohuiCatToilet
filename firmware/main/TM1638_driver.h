#ifndef TM1638_DRIVER_H
#define TM1638_DRIVER_H

#include "esp_err.h"

#define TM1638_STB      CONFIG_TM1638_STB
#define TM1638_CLK      CONFIG_TM1638_CLK
#define TM1638_DIO      CONFIG_TM1638_DIO

#define TM1638_CMD_DATA (0x40)
#define TM1638_CMD_DISP (0x80)
#define TM1638_CMD_ADDR (0xc0)

#define TM1638_DATA_READ  (0x00)
#define TM1638_DATA_WRITE (0x02)
#define TM1638_DATA_NOINCR  (0x04)
#define TM1638_DATA_TEST  (0x08)

#define TM1638_DISP_ON  (0x08)

#define TM1638_MEMSIZE (0x10)

extern const uint8_t TM1638_char_table[];
esp_err_t TM1638_init(void);

void TM1638_start(void);
void TM1638_end(void);
void TM1638_send(uint8_t data);
uint8_t TM1638_recv(void);

void TM1638_set_data(uint8_t mode);
void TM1638_set_display(uint8_t disp);
void TM1638_set_addr_raw(uint8_t addr);

void TM1638_write(uint8_t addr, uint8_t data);
void TM1638_write_block(uint8_t data[]);

void TM1638_write_buffer(uint8_t addr, uint8_t data);
void TM1638_flush(void);

void TM1638_write_char(uint8_t addr, char c);
void TM1638_show(char *strbuf);
uint32_t TM1638_getkey();

#endif
