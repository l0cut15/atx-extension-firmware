#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "uart_tx.pio.h"
#include "uart_rx.pio.h"

#define BAUD_RATE 115200

#define UART_TX_PIN 17
#define UART_RX_PIN 16

#define PIO_INST    pio0
#define SM_TX       0
#define SM_RX       1
#define BTN_RST_PIN 18
#define BTN_PWR_PIN 19
#define LED_HDD_PIN 20
#define LED_PWR_PIN 21

#define UART_BUF_SIZE 128
static char uart_buf[UART_BUF_SIZE];
static int uart_buf_pos = 0;

void on_uart_line(const char *line)
{
    printf("UART LINE: %s\n", line);
    if (strcmp(line, "BTN_RST_ON\n") == 0)
    {
        gpio_put(BTN_RST_PIN, 1);
    }
    else if (strcmp(line, "BTN_RST_OFF\n") == 0)
    {
        gpio_put(BTN_RST_PIN, 0);
    }
    else if (strcmp(line, "BTN_PWR_ON\n") == 0)
    {
        gpio_put(BTN_PWR_PIN, 1);
    }
    else if (strcmp(line, "BTN_PWR_OFF\n") == 0)
    {
        gpio_put(BTN_PWR_PIN, 0);
    }
}

void on_uart_rx()
{
    while (!pio_sm_is_rx_fifo_empty(PIO_INST, SM_RX))
    {
        uint8_t ch = (uint8_t)uart_rx_program_getc(PIO_INST, SM_RX);

        if (uart_buf_pos < UART_BUF_SIZE - 1)
        {
            uart_buf[uart_buf_pos++] = ch;
        }

        if (ch == '\n' || uart_buf_pos >= UART_BUF_SIZE - 1)
        {
            uart_buf[uart_buf_pos] = '\0';
            on_uart_line(uart_buf);
            uart_buf_pos = 0;
        }
    }
}

int main()
{
    stdio_init_all();

    if (watchdog_caused_reboot())
    {
        printf("Rebooted by Watchdog!\n");
    }

    watchdog_enable(8388, true);

    uint offset_tx = pio_add_program(PIO_INST, &uart_tx_program);
    uart_tx_program_init(PIO_INST, SM_TX, offset_tx, UART_TX_PIN, BAUD_RATE);

    uint offset_rx = pio_add_program(PIO_INST, &uart_rx_program);
    uart_rx_program_init(PIO_INST, SM_RX, offset_rx, UART_RX_PIN, BAUD_RATE);

    sleep_ms(500);
    printf("PIO UART ready: TX=GPIO%d RX=GPIO%d\n", UART_TX_PIN, UART_RX_PIN);

    pio_set_irq0_source_enabled(PIO_INST, pis_sm1_rx_fifo_not_empty, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, on_uart_rx);
    irq_set_enabled(PIO0_IRQ_0, true);

    gpio_init(BTN_RST_PIN);
    gpio_set_dir(BTN_RST_PIN, GPIO_OUT);
    gpio_put(BTN_RST_PIN, 0);

    gpio_init(BTN_PWR_PIN);
    gpio_set_dir(BTN_PWR_PIN, GPIO_OUT);
    gpio_put(BTN_PWR_PIN, 0);

    gpio_init(LED_HDD_PIN);
    gpio_pull_up(LED_HDD_PIN);
    gpio_set_dir(LED_HDD_PIN, GPIO_IN);

    gpio_init(LED_PWR_PIN);
    gpio_pull_up(LED_PWR_PIN);
    gpio_set_dir(LED_PWR_PIN, GPIO_IN);

    bool btn_rst_state = false;
    bool btn_pwr_state = false;
    bool led_hdd_state = false;
    bool led_pwr_state = false;
    uint64_t last_update_sent = 0;
    uint64_t last_watchdog_reset = 0;
    char message[6];
    while (true)
    {
        uint64_t now = time_us_64();

        bool new_led_hdd_state = gpio_get(LED_HDD_PIN);
        bool new_led_pwr_state = gpio_get(LED_PWR_PIN);
        bool new_btn_rst_state = gpio_get(BTN_RST_PIN);
        bool new_btn_pwr_state = gpio_get(BTN_PWR_PIN);

        // printf("LED_HDD_PIN: %d\n", new_led_hdd_state);
        // printf("LED_PWR_PIN: %d\n", new_led_pwr_state);

        bool states_changed = new_led_hdd_state != led_hdd_state ||
                              new_led_pwr_state != led_pwr_state ||
                              new_btn_rst_state != btn_rst_state ||
                              new_btn_pwr_state != btn_pwr_state;

        // if states changed or 1000ms passed since last update
        if (states_changed || (now - last_update_sent > 1000000))
        {
            snprintf(message, 6, "%d%d%d%d\n", new_led_hdd_state, new_led_pwr_state, new_btn_rst_state, new_btn_pwr_state);
            uart_tx_program_puts(PIO_INST, SM_TX, message);
            last_update_sent = now;
            printf("Sent at %llu: %s", now, message);

            led_hdd_state = new_led_hdd_state;
            led_pwr_state = new_led_pwr_state;
            btn_rst_state = new_btn_rst_state;
            btn_pwr_state = new_btn_pwr_state;
        }

        if (now - last_watchdog_reset > 1000000)
        {
            watchdog_update();
            last_watchdog_reset = now;
        }

        sleep_ms(10);

    }
}
