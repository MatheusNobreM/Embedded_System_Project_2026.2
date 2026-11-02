#include "board_init.h"

#include "hardware/gpio.h"
#include "pico/cyw43_arch.h"

#include "board_resources.h"

static bool s_cyw43_ok;

void board_init_pzem_uart(void)
{
    uart_init(BOARD_PZEM_UART, BOARD_PZEM_BAUD);
    uart_set_format(BOARD_PZEM_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(BOARD_PZEM_UART, false, false);
    /* FIFO de 32 bytes comporta a resposta inteira (25 bytes): a tarefa do
     * PZEM pode dormir enquanto o quadro chega, sem IRQ nem espera ocupada. */
    uart_set_fifo_enabled(BOARD_PZEM_UART, true);
    gpio_set_function(BOARD_PIN_PZEM_TX, GPIO_FUNC_UART);
    gpio_set_function(BOARD_PIN_PZEM_RX, GPIO_FUNC_UART);
    /* Pull-up no RX: linha em repouso alta se o conversor estiver desligado. */
    gpio_pull_up(BOARD_PIN_PZEM_RX);
}

bool board_init_cyw43(void)
{
    s_cyw43_ok = (cyw43_arch_init() == 0);
    return s_cyw43_ok;
}

void board_led_set(bool on)
{
    if (s_cyw43_ok) {
        cyw43_arch_gpio_put(BOARD_LED_WL_GPIO, on);
    }
}
