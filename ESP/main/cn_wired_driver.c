/* GPIO-based Daikin CN_WIRED protocol driver for ESP866 */
/* Copyright ©2024 Pavel Fedin. See LICENCE file for details .GPL 3.0 */

#include <esp_timer.h>
#include <FreeRTOS.h>
#include <limits.h>
#include <string.h>
#include <task.h>

#include <driver/hw_timer.h>
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

#define TX_STATE_IDLE  0
#define TX_STATE_SYNC  1
#define TX_STATE_DATA  2
#define TX_STATE_DELAY 3
#define TX_STATE_END   4

struct CN_Wired_Transmitter {
    uint8_t    buffer[CNW_PKT_LEN];
    int        line_state;
    int        tx_state;
    int        tx_bytes;
    int        tx_bits;
    gpio_num_t pin;
};

static struct CN_Wired_Receiver rx_obj;
static struct CN_Wired_Transmitter tx_obj;

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

void tx_interrupt (void* arg)
{
    struct CN_Wired_Transmitter* tx = arg;
    int new_state = !tx->line_state;
    int pulse_time;

    // Flip the line and remember new state
    tx->line_state = new_state;
    gpio_set_level(tx->pin, new_state);

    if (new_state) {
        // LOW -> HIGH
        if (tx->tx_state == TX_STATE_SYNC)
        {
            pulse_time = START_LENGTH;
            tx->tx_state = TX_STATE_DATA;
        }
        else if (tx->tx_state == TX_STATE_DATA)
        {
            pulse_time = ((tx->buffer[tx->tx_bytes] >> tx->tx_bits) & 1) ? BIT_1_LENGTH : BIT_0_LENGTH;
            
            if (tx->tx_bits < 7)
                tx->tx_bits++;
            else if (tx->tx_bytes == CNW_PKT_LEN)
                tx->tx_state = TX_STATE_DELAY;
            else {
                tx->tx_bits = 0;
                tx->tx_bytes++;
            }
        }
        else if (tx->tx_state == TX_STATE_DELAY)
        {
            pulse_time = END_DELAY;
            tx->tx_state = TX_STATE_END;
        }
        else
        {
            // The final transition to idle after END 2ms pulse has been sent.
            // Do not re-arm the timer
            tx->tx_state = TX_STATE_IDLE;
            return;
        }
    } else {
        // HIGH -> LOW
        pulse_time = (tx->tx_state == TX_STATE_DELAY) ? END_LENGTH : SPACE_LENGTH;
    }

    hw_timer_alarm_us (pulse_time, false);
}

esp_err_t cn_wired_driver_install (gpio_num_t rx, gpio_num_t tx)
{
    esp_err_t err;

    rx_obj.state    = 1;
    rx_obj.rx_bytes = -1; // "Wait for sync" state
    rx_obj.pin      = rx;
    rx_obj.task     = xTaskGetCurrentTaskHandle();

    xTaskNotifyStateClear(rx_obj.task);

    tx_obj.tx_bytes = CNW_PKT_LEN; // Transmitter idle state
    tx_obj.pin      = tx;

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
    if (err)
        return err;
    err = hw_timer_init(tx_interrupt, &tx_obj);
 
    return err;
}

void cn_wired_driver_delete (void)
{
    hw_timer_deinit ();
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

int cn_wired_write_bytes (const uint8_t *buffer)
{
    if (tx_obj.tx_state != TX_STATE_IDLE)
        return 0; // Busy, retry plz

    memcpy(tx_obj.buffer, buffer, CNW_PKT_LEN);
    tx_obj.tx_bits    = 0;
    tx_obj.tx_bytes   = 0;
    tx_obj.tx_state   = TX_STATE_SYNC;
    tx_obj.line_state = 0;

    // Start of the SYNC low pulse. This will kickstart the transmitter.
    gpio_set_level (tx_obj.pin, 0);
    hw_timer_alarm_us (SYNC_LENGTH, false);

    return CNW_PKT_LEN;
}
