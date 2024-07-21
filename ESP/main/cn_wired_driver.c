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

enum State {
    TX_IDLE,
    TX_DATA,
    TX_END
};

struct CN_Wired_Transmitter {
    uint8_t buffer[CNW_PKT_LEN];
    enum State tx_state;
    int        line_state;
    int        tx_bytes;
    int        tx_bits;
    int        next_bit;  // Time of the next bit
    int        gpio_mask; // Pre-cooked GPIO mask
    int        t_space;   // Pre-cooked delays in ticks
    int        t_zero;
    int        t_one;
    int        t_delay;
    int        t_end;
};

// We need to act quickly-quickly-quickly for better timings. Standard drivers
// do lots of unnecessary stuff (like argument checks), here we're avoiding
// all that. All the HW-specific values are pre-cooked in our struct, so we're
// just banging the metal.
// This code is basically shamelessly stolen from SDK's gpio and hw_timer drivers
// respectively
static inline void tx_set_high(const struct CN_Wired_Transmitter* tx)
{
    GPIO.out_w1ts |= tx->gpio_mask;
}

static inline void tx_set_low(const struct CN_Wired_Transmitter* tx)
{
    GPIO.out_w1tc |= tx->gpio_mask;
}

static inline void tx_timer_reload(const struct CN_Wired_Transmitter* tx, uint32_t ticks)
{
    portENTER_CRITICAL();
    frc1.load.data = ticks;
    frc1.ctrl.en   = 0x01;
    portEXIT_CRITICAL();
}

static uint32_t timer_ticks(uint32_t usecs) {
    return ((TIMER_BASE_CLK >> hw_timer_get_clkdiv()) / 1000000) * usecs;
}

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

    if (!new_state) {
        // HIGH -> LOW. Space starts
        tx_timer_reload(tx, tx->t_space);
        tx_set_low(tx);
    } else if (tx->next_bit) {
        // LOW ->HIGH. Bit starts
        tx_timer_reload(tx, tx->next_bit);
        tx_set_high(tx);

        // We've set our line and timer, now we have some time for housekeeping.
        if (tx->tx_bytes < CNW_PKT_LEN) {
            // Ticks value for the next bit.
            tx->next_bit = ((tx->buffer[tx->tx_bytes] >> tx->tx_bits) & 1) ? tx->t_one : tx->t_zero;
            // Advance bit/byte counters
            if (tx->tx_bits < 7) {
                tx->tx_bits++;
            } else {
                tx->tx_bits = 0;
                tx->tx_bytes++;
            }
        } else if (tx->tx_state == TX_DATA) {
            // The current bit we've just started is the final one, no more data.
            // After the space, issue a t_delay HIGH
            tx->tx_state = TX_END;
            tx->next_bit = tx->t_delay;
        } else {
            // tx->tx_state == TX_END. We've just started our DELAY.
            // It will be followed by t_end LOW, after which we're done
            tx->t_space = tx->t_end;
            tx->next_bit = 0;
        }
    } else {
        // LOW->HIGH, no next_bit set. We've just completed our final pulse and turning idle.
        tx_set_high(tx);
        tx->tx_state = TX_IDLE;
    }

    tx->line_state = new_state;
}

esp_err_t cn_wired_driver_install (gpio_num_t rx, gpio_num_t tx)
{
    esp_err_t err;

    rx_obj.state    = 1;
    rx_obj.rx_bytes = -1; // "Wait for sync" state
    rx_obj.pin      = rx;
    rx_obj.task     = xTaskGetCurrentTaskHandle();

    xTaskNotifyStateClear(rx_obj.task);

    tx_obj.tx_state  = TX_IDLE;
    tx_obj.gpio_mask = 1 << tx;

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

esp_err_t cn_wired_read_bytes (uint8_t *buffer, TickType_t timeout)
{
    if (!xTaskNotifyWait(0x00, ULONG_MAX, NULL, timeout ))
        return ESP_ERR_TIMEOUT;

    // Drop the prefix 0x80 byte, containing start bit
    memcpy(buffer, &rx_obj.buffer[1], CNW_PKT_LEN);
    rx_obj.rx_bytes = -1; // "Wait for sync" state

    return ESP_OK;
}

esp_err_t cn_wired_write_bytes (const uint8_t *buffer)
{
    if (tx_obj.tx_state != TX_IDLE)
        return ESP_FAIL; // Busy, retry plz

    memcpy(tx_obj.buffer, buffer, CNW_PKT_LEN);
    tx_obj.tx_state   = TX_DATA;
    tx_obj.tx_bits    = 0;
    tx_obj.tx_bytes   = 0;
    tx_obj.line_state = 0;                         // We start with SYNC low

    // Use hw_timer_alarm_us() to guarantee that the timer is set up correctly.
    // This will kickstart the transmitter. When the timer fires, the SYNC pulse
    // will be over and start bit will be transmitted by our state machine.
    // Constant of -20 was fine-tuned by trial and error using DUMP_TIMINGS feature
    // in Arduino bridge
    hw_timer_alarm_us (SYNC_LENGTH - 20, false);
    tx_set_low(&tx_obj);

    // hw_timer_alarm_us() has set up clock divider, so that hw_timer_get_clkdiv()
    // now returns a proper value. We rely on this behavior in order to pre-cook
    // bit timings
    tx_obj.next_bit = timer_ticks(START_LENGTH);
    tx_obj.t_one    = timer_ticks(BIT_1_LENGTH);
    tx_obj.t_zero   = timer_ticks(BIT_0_LENGTH);
    tx_obj.t_space  = timer_ticks(SPACE_LENGTH);
    tx_obj.t_delay  = timer_ticks(END_DELAY);
    tx_obj.t_end    = timer_ticks(END_LENGTH);

    return ESP_OK;
}
