#ifndef CONNECTION_DUP_H
#define CONNECTION_DUP_H

#include "esp_err.h"

bool connection_dup_initialized();

// register vfs driver for duplicate fds
esp_err_t init_connection_dup(void);

// works like dup(), duplicates an existing fd, but only for lwip tcp
// connections, must be closed before the original fd
int connection_dup(int conn_fd);

#endif

