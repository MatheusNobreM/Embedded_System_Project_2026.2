#include "board_init.h"

#include <stdio.h>

#include "driver/uart_vfs.h"
#include "esp_err.h"

#include "board_resources.h"

#define CONSOLE_RX_BUF   256u    /* mínimo do driver (> FIFO de 128 B); o firmware não lê o console */
#define CONSOLE_TX_BUF   2048u   /* um relatório inteiro (~1 KB) com folga */
#define PZEM_RX_BUF      256u    /* resposta do PZEM = 25 B */

void board_init_console(void)
{
    fflush(stdout);
    ESP_ERROR_CHECK(uart_driver_install(BOARD_CONSOLE_UART, CONSOLE_RX_BUF, CONSOLE_TX_BUF, 0, NULL, 0));
    uart_vfs_dev_use_driver(BOARD_CONSOLE_UART_INDEX);
}

void board_init_pzem_uart(void)
{
    const uart_config_t cfg = {
        .baud_rate = (int)BOARD_PZEM_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    /* Sem buffer de TX: os 8 bytes da requisição cabem na FIFO de 128 B e
     * uart_write_bytes volta na hora. A recepção cai no buffer do driver
     * (IRQ), e a tarefa do PZEM dorme em uart_read_bytes até o quadro chegar. */
    ESP_ERROR_CHECK(uart_driver_install(BOARD_PZEM_UART, PZEM_RX_BUF, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(BOARD_PZEM_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(BOARD_PZEM_UART, BOARD_PIN_PZEM_TX, BOARD_PIN_PZEM_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* O driver repassa os bytes da FIFO depois de N símbolos de silêncio na
     * linha; 3 (≈ 3 ms a 9600 bps) entrega o fim do quadro mais cedo que o padrão de 10. */
    ESP_ERROR_CHECK(uart_set_rx_timeout(BOARD_PZEM_UART, 3));
    /* Pull-up no RX: linha em repouso alta se o conversor estiver desligado. */
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOARD_GPIO_PZEM_RX, GPIO_PULLUP_ONLY));
}

void board_init_led(void)
{
    ESP_ERROR_CHECK(gpio_reset_pin(BOARD_GPIO_LED));
    ESP_ERROR_CHECK(gpio_set_direction(BOARD_GPIO_LED, GPIO_MODE_OUTPUT));
    board_led_set(false);
}

void board_led_set(bool on)
{
    (void)gpio_set_level(BOARD_GPIO_LED, on ? BOARD_LED_ON_LEVEL : !BOARD_LED_ON_LEVEL);
}
