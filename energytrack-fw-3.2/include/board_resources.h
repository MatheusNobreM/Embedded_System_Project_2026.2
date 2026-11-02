/**
 * @file board_resources.h
 * @brief Instâncias de hardware do pico-sdk associadas ao mapa de pinos.
 *
 * Separado de board_pins.h porque depende do SDK (uart0, i2c1...).
 * Também barra, em tempo de compilação, a configuração de stdio que
 * colidiria com o PZEM na UART0.
 */
#ifndef BOARD_RESOURCES_H
#define BOARD_RESOURCES_H

#include "hardware/uart.h"
#include "hardware/i2c.h"
#include "board_pins.h"

#define BOARD_PZEM_UART   uart0
#define BOARD_OLED_I2C    i2c1

_Static_assert(BOARD_PZEM_UART_INDEX == 0, "BOARD_PZEM_UART precisa acompanhar BOARD_PZEM_UART_INDEX");
_Static_assert(BOARD_OLED_I2C_INDEX == 1,  "BOARD_OLED_I2C precisa acompanhar BOARD_OLED_I2C_INDEX");

/*
 * CONFLITO CRÍTICO: o stdio padrão do pico-sdk na Pico W é a uart0 em GP0/GP1.
 * Se pico_stdio_uart estiver habilitado, todo printf vai para o PZEM e
 * corrompe os quadros Modbus. Use stdio via USB:
 *   pico_enable_stdio_usb(<alvo> 1)
 *   pico_enable_stdio_uart(<alvo> 0)
 */
#if defined(LIB_PICO_STDIO_UART) && LIB_PICO_STDIO_UART
#error "pico_stdio_uart habilitado: a uart0 (GP0/GP1) e do PZEM. Use pico_enable_stdio_uart(<alvo> 0) e stdio USB."
#endif

#endif /* BOARD_RESOURCES_H */
