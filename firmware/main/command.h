#ifndef COMMAND_H
#define COMMAND_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#define COMMANDBUFFSIZE 1024

//initialize console, redirect log functions to stderr
esp_err_t command_init(void);

// Use UART for default command console, and use socket fds for web console
typedef struct command_parameter command_parameter;
struct command_parameter{
    // console identifier
    char* id;

    // Streams of Socket opened by tcp_server
    FILE* stream_in;
    FILE* stream_out;   

    // Callback function to be run at console exit.
    // The pointer to command_parameter itself will be passed as argument
    void (*at_exit) (command_parameter * para); 
};

// Fill in a command_parameter* in vTaskCreate for a web console, or NULL for
// default UART console. The command_parameter must be allocated in heap and be
// freed by the callback function
void command_task(void *pvParameters);

#endif

