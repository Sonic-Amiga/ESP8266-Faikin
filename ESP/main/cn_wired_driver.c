/* GPIO-based Daikin CN_WIRED protocol driver for ESP866 */
/* Copyright ©2024 Pavel Fedin. See LICENCE file for details .GPL 3.0 */

#include <esp_timer.h>
#include <FreeRTOS.h>
#include <limits.h>
#include <string.h>
#include <task.h>

#include <driver/hw_timer.h>
#include <esp8266/gpio_struct.h>
#include <esp8266/timer_struct.h>
#include <rom/gpio.h>

#include "cn_wired.h"
#include "cn_wired_driver.h"

// Our RX buffers are prefixed by one byte, where start bit is stored.
// See start()
#define RX_LEN CNW_PKT_LEN + 1

// Pulse lengths in microseconds
#define SYNC_LENGTH  2600
#define START_LENGTH 1000
#define SPACE_LENGTH 300
#define BIT_1_LENGTH 900
#define BIT_0_LENGTH 400
#define END_DELAY    16000
#define END_LENGTH   2000
// Detection tolerance
#define THRESHOLD 200

struct CN_Wired_Receiver {
    uint8_t    buffer[RX_LEN];
    int64_t    pulse_start; // Pulse start time
    int        state;       // Current line state
    int        rx_bytes;    // Bytes counter
    int        rx_bits;     // Bits counter
    gpio_num_t pin;
    TaskHandle_t task;
};

static struct CN_Wired_Receiver rx_obj;

static int isReceiving (const struct CN_Wired_Receiver* rx) {
    return rx->rx_bytes >= 0 && rx->rx_bytes < RX_LEN;
}

static void rx_interrupt (void* arg)
{
    struct CN_Wired_Receiver* rx = arg;
    int new_state = gpio_get_level(rx->pin);

    if (new_state == rx->state) {
        return; // Some rubbish
    }

    int64_t now = esp_timer_get_time();

    if (new_state) {
        // LOW->HIGH, start of data bit
        uint64_t length = now - rx->pulse_start;

        if (length > SYNC_LENGTH - THRESHOLD) {
            // Got SYNC pulse, start receiving data
            // The packet always starts with a single HIGH pulse of 1000 usecs, which can be
            // interpreted as a value 1, so the 0th byte will contain this single bit.
            // The next bit is going to be LSB of 1st byte.
            rx->rx_bytes  = 0;
            rx->rx_bits   = 7;
            rx->buffer[0] = 0;
        }
    } else {
        // HIGH->LOW, start of SYNC or end of data bit
        if (isReceiving (rx)) {
            // High bit - 900us, low bit - 400us
            uint8_t v = (now - rx->pulse_start) > BIT_1_LENGTH - THRESHOLD;

            rx->buffer[rx->rx_bytes] |= (v << rx->rx_bits);

            if (rx->rx_bits == 7) {
                rx->rx_bits = 0;
                rx->rx_bytes++;

                if (rx->rx_bytes == RX_LEN) {
                    // Packet complete, signal ready to read
                    vTaskNotifyGiveFromISR(rx->task, NULL);
                } else {
                    // Prepare the next byte. We don't check for overflow for speed, that's
                    // why we need one spare trailing byte
                    rx->buffer[rx->rx_bytes] = 0;
                }
            } else {
                rx->rx_bits++;
            }
        }
    }

      // A new state has begun at 'now' microseconds
    rx->pulse_start = now;
    rx->state       = new_state;
}

static gpio_num_t tx_pin;

esp_err_t cn_wired_driver_install (gpio_num_t rx, gpio_num_t tx)
{
    esp_err_t err;

    rx_obj.state    = 1;
    rx_obj.rx_bytes = -1; // "Wait for sync" state
    rx_obj.pin      = rx;
    rx_obj.task     = xTaskGetCurrentTaskHandle();

    xTaskNotifyStateClear(rx_obj.task);

    tx_pin = tx;

    gpio_pad_select_gpio(rx);
    gpio_pad_select_gpio(tx);

    err = gpio_set_direction(rx, GPIO_MODE_INPUT);
    if (err)
        return err;
    err = gpio_set_pull_mode(rx, GPIO_FLOATING);
    if (err)
        return err;
    err = gpio_set_direction(tx, GPIO_MODE_OUTPUT);
    if (err)
        return err;
    err = gpio_set_level(tx, 1);
    if (err)
        return err;
    err = gpio_install_isr_service(0);
    if (err)
        return err;
    err = gpio_isr_handler_add(rx, rx_interrupt, &rx_obj);
    if (err)
        return err;
    err = gpio_set_intr_type(rx, GPIO_INTR_ANYEDGE);

    return err;
}

void cn_wired_driver_delete (void)
{
    gpio_uninstall_isr_service ();
}

int cn_wired_read_bytes (uint8_t *buffer, TickType_t timeout)
{
    if (!xTaskNotifyWait(0x00, ULONG_MAX, NULL, timeout ))
        return 0;

    // Drop the prefix 0x80 byte, containing start bit
    memcpy(buffer, &rx_obj.buffer[1], CNW_PKT_LEN);
    rx_obj.rx_bytes = -1; // "Wait for sync" state

    return CNW_PKT_LEN;
}

// We need to act quickly-quickly-quickly for better timings. Standard drivers
// does lots of unnecessary stuff (like argument checks), here we're avoiding
// all that. All the HW-specific values are pre-cooked in our struct, so we're
// just banging the metal.
// This code is basically shamelessly stolen from SDK's gpio and hw_timer drivers
// respectively
#define tx_set_high GPIO.out_w1ts |= gpio_mask
#define tx_set_low  GPIO.out_w1tc |= gpio_mask

#define SEND_BIT(byte, bit)                                                \
{                                                                          \
    uint16_t bit_time = (byte & (1 << bit)) ? BIT_1_LENGTH : BIT_0_LENGTH; \
    tx_set_high;                                                           \
    os_delay_us(bit_time);                                                 \
    tx_set_low;                                                            \
    os_delay_us(SPACE_LENGTH);                                             \
}

#define SEND_BYTE(data, offset) \
{                               \
  uint8_t byte = data[offset];  \
  SEND_BIT(byte, 0)             \
  SEND_BIT(byte, 1)             \
  SEND_BIT(byte, 2)             \
  SEND_BIT(byte, 3)             \
  SEND_BIT(byte, 4)             \
  SEND_BIT(byte, 5)             \
  SEND_BIT(byte, 6)             \
  SEND_BIT(byte, 7)             \
}

// We have to be ve-e-e-ry precise with timings, so we disable interrupts and use
// os_delay_us() for delay loops. Cheaper approaches, like using timer interrupts,
// failed to provide precision we need.
int cn_wired_write_bytes (const uint8_t *data)
{
    uint32_t gpio_mask = 1 << tx_pin;

    portENTER_CRITICAL();

    tx_set_low;
    os_delay_us(SYNC_LENGTH); // SYNC low
    tx_set_high;
    os_delay_us(START_LENGTH); // Start bit high
    tx_set_low;
    os_delay_us(SPACE_LENGTH); // Space low
    // Extremely unrolled for better precision
    SEND_BYTE(data, 0)
    SEND_BYTE(data, 1)
    SEND_BYTE(data, 2)
    SEND_BYTE(data, 3)
    SEND_BYTE(data, 4)
    SEND_BYTE(data, 5)
    SEND_BYTE(data, 6)
    SEND_BYTE(data, 7)
    tx_set_high; // Idle high

    portEXIT_CRITICAL();

    // 2ms LOW pulse in the end proved optional, conditioner perfectly
    // accepts data without it. So for simplicity we don't bother.

    return CNW_PKT_LEN;
}
