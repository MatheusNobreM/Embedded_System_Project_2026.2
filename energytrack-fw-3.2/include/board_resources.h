/**
 * @file board_resources.h
 * @brief Instâncias de hardware do ESP-IDF associadas ao mapa de pinos.
 *
 * Separado de board_pins.h porque depende do ESP-IDF (uart_port_t,
 * adc_channel_t...). Também barra, em tempo de compilação, a configuração
 * de console que colidiria com o PZEM.
 */
#ifndef BOARD_RESOURCES_H
#define BOARD_RESOURCES_H

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "hal/adc_types.h"

#include "board_pins.h"

#define BOARD_PZEM_UART        ((uart_port_t)BOARD_PZEM_UART_INDEX)
#define BOARD_CONSOLE_UART     ((uart_port_t)BOARD_CONSOLE_UART_INDEX)
#define BOARD_GPIO_PZEM_TX     ((gpio_num_t)BOARD_PIN_PZEM_TX)
#define BOARD_GPIO_PZEM_RX     ((gpio_num_t)BOARD_PIN_PZEM_RX)
#define BOARD_GPIO_LED         ((gpio_num_t)BOARD_PIN_LED)
#define BOARD_LM35_ADC_UNIT    ADC_UNIT_1
#define BOARD_LM35_ADC_CHANNEL ((adc_channel_t)BOARD_ADC_CH_LM35)

_Static_assert(BOARD_ADC_UNIT_LM35 == 1, "o LM35 precisa ficar no ADC1 (o ADC2 não convive com o Wi-Fi)");
_Static_assert(BOARD_PZEM_UART_INDEX != BOARD_CONSOLE_UART_INDEX, "o PZEM não pode dividir a UART do console");

/*
 * CONFLITO CRÍTICO: se o console do ESP-IDF (logs e printf) estiver na mesma
 * UART do PZEM, todo printf vira lixo nos quadros Modbus. O console fica na
 * UART0 (ponte USB-serial); o PZEM, na UART2.
 */
#if defined(CONFIG_ESP_CONSOLE_UART_NUM) && (CONFIG_ESP_CONSOLE_UART_NUM == BOARD_PZEM_UART_INDEX)
#error "console do ESP-IDF na UART do PZEM: use CONFIG_ESP_CONSOLE_UART_DEFAULT (UART0) no sdkconfig."
#endif

#endif /* BOARD_RESOURCES_H */
