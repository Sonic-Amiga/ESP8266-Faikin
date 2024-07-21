#ifndef _CN_WIRED_DRIVER_H
#define _CN_WIRED_DRIVER_H

#include <driver/gpio.h>
#include "revk.h"

esp_err_t cn_wired_driver_install (gpio_num_t rx, gpio_num_t tx);
void cn_wired_driver_delete (void);
esp_err_t cn_wired_read_bytes (uint8_t *rx, TickType_t timeout);
esp_err_t cn_wired_write_bytes (const uint8_t *buf);

static inline void cn_wired_stats (jo_t j)
{
    // 8266 driver doesn't have any stats. Empty function for source compatibility.
}

#endif
