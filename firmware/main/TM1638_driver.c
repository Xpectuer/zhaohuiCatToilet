#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "TM1638_driver.h"

static const char *TAG = "TM1638";
static uint8_t mode_s;
static uint8_t buffer[16] = {
	0x01, 0x01, 0x02, 0x00,
	0x04, 0x01, 0x08, 0x00,
	0x10, 0x00, 0x20, 0x01,
	0x40, 0x00, 0x80, 0x01,
};

const uint8_t TM1638_char_table[128]={
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x82, 0x22, 0xfe, 0x2d, 0xd2, 0xda, 0x20, 0x38, 0x0e, 0xf6, 0x46, 0x10, 0x40, 0x80, 0x52, 
	0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x48, 0xc8, 0x46, 0x48, 0x70, 0x53, 
	0xdd, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, 0x3d, 0x76, 0x06, 0x1e, 0xf2, 0x38, 0xf2, 0x37, 0x3f, 
	0x73, 0xbf, 0xf7, 0x6d, 0x31, 0x3e, 0x3a, 0x7e, 0x49, 0x6e, 0x1b, 0x39, 0x64, 0x0f, 0x23, 0x08, 
	0x02, 0xdc, 0x7c, 0x58, 0x5e, 0xd8, 0x71, 0x6f, 0x74, 0x04, 0x0d, 0x75, 0x86, 0xd4, 0x54, 0x5c, 
	0x73, 0x67, 0x50, 0xc8, 0x78, 0x1c, 0x18, 0x9c, 0xc6, 0x6e, 0xc4, 0x31, 0x30, 0x07, 0xc0, 0x00, 
};

esp_err_t TM1638_init(void){
	gpio_config_t io_conf = {};
	
	//configure GPIO for STB and CLK
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pin_bit_mask = ((1ULL << TM1638_STB) | (1ULL << TM1638_CLK));
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	ESP_ERROR_CHECK(gpio_config(&io_conf));
	
	//configure GPIO for DIO
	io_conf.intr_type = GPIO_INTR_DISABLE;
	io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
	io_conf.pin_bit_mask = (1ULL << TM1638_DIO);
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	ESP_ERROR_CHECK(gpio_config(&io_conf));

	gpio_set_level(TM1638_STB, 1);
	gpio_set_level(TM1638_CLK, 1);
	gpio_set_level(TM1638_DIO, 1);

	TM1638_set_data(TM1638_DATA_WRITE);
	TM1638_flush();
	TM1638_set_display(TM1638_DISP_ON | 0x07);
	TM1638_set_data(TM1638_DATA_NOINCR | TM1638_DATA_WRITE);

	return ESP_OK;
}

void TM1638_start(void){
	gpio_set_level(TM1638_STB, 0);
}

void TM1638_end(void){
	for (int j = 0; j < 10; ++j)
		asm volatile ("nop");
	gpio_set_level(TM1638_STB, 1);
	for (int j = 0; j < 25; ++j)
		asm volatile ("nop");
}

void TM1638_send(uint8_t data){
	//ESP_LOGI(TAG, "send data: %02x", data);
	for (int i = 0; i < 8; ++i){
		gpio_set_level(TM1638_CLK, 0);
		gpio_set_level(TM1638_DIO, data & 0x1);
		for (int j = 0; j < 10; ++j)
			asm volatile ("nop");
		gpio_set_level(TM1638_CLK, 1);
		data >>= 1;
		for (int j = 0; j < 10; ++j)
			asm volatile ("nop");
	}
}

uint8_t TM1638_recv(void){
	uint8_t data = 0;
	gpio_set_level(TM1638_DIO, 1);
	for (int i = 0; i < 8; ++i){
		gpio_set_level(TM1638_CLK, 0);
		for (int j = 0; j < 10; ++j)
			asm volatile ("nop");
		gpio_set_level(TM1638_CLK, 1);
		data |= gpio_get_level(TM1638_DIO) << i;
		for (int j = 0; j < 10; ++j)
			asm volatile ("nop");
	}
	return data;
}

void TM1638_set_data(uint8_t mode){
	mode_s = mode & 0x0e;
	TM1638_start();
	TM1638_send(TM1638_CMD_DATA | mode_s);
	TM1638_end();
}

void TM1638_set_display(uint8_t disp){
	TM1638_start();
	TM1638_send(TM1638_CMD_DISP | (disp & 0x0f));
	TM1638_end();
}

void TM1638_set_addr_raw(uint8_t addr){
	TM1638_send(TM1638_CMD_ADDR | (addr & 0x0f));
}

void TM1638_write(uint8_t addr, uint8_t data){
	//ESP_LOGI(TAG, "write addr: %02x data: %02x", addr, data);
	if (!(mode_s & TM1638_DATA_NOINCR))
		TM1638_set_data(mode_s | TM1638_DATA_NOINCR);
	TM1638_start();
	TM1638_set_addr_raw(addr);
	TM1638_send(data);
	TM1638_end();
}

void TM1638_write_block(uint8_t data[]){
	ESP_LOGI(TAG, "write block data: %02x %02x %02x %02x %02x %02x %02x %02x ", data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
	ESP_LOGI(TAG, "write block data: %02x %02x %02x %02x %02x %02x %02x %02x ", data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
	if (mode_s & TM1638_DATA_NOINCR)
		TM1638_set_data(mode_s & ~TM1638_DATA_NOINCR);
	TM1638_start();
	TM1638_set_addr_raw(0x00);
	for (int i = 0; i < 16; ++i)
		TM1638_send(data[i]);
	TM1638_end();
}

void TM1638_write_buffer(uint8_t addr, uint8_t data){
	buffer[addr & 0x0f] = data;
}

void TM1638_flush(void){
	TM1638_write_block(buffer);
}

void TM1638_write_char(uint8_t addr, char c){
}

void TM1638_show(char * strbuf){
}
