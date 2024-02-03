#ifndef _CN_WIRED_DRIVER_H
#define _CN_WIRED_DRIVER_H

#include <driver/gpio.h>

esp_err_t cn_wired_driver_install (gpio_num_t rx, gpio_num_t tx);
void cn_wired_driver_delete (void);
int cn_wired_read_bytes (uint8_t *buffer, TickType_t timeout);
int cn_wired_write_bytes (const uint8_t *buffer);

#endif
