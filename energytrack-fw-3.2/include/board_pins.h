/**
 * @file board_pins.h
 * @brief Mapa de pinos do EnergyTrack — ESP32 DevKit (ESP32-WROOM-32) + PZEM-004T v3 + LM35.
 *
 * FONTE ÚNICA DE VERDADE para números de GPIO (WPS/EAP 3.2; revisado no 3.1
 * e migrado da Pico W para o ESP32 em 05/11/2026 — a versão da Pico W está
 * na branch pico-w).
 *  - Nenhum outro arquivo do firmware deve usar número de GPIO literal.
 *  - Este header NÃO depende do ESP-IDF, para poder ser validado no host
 *    (ver test/host/test_pinmap.c).
 *
 * Convenções:
 *  - BOARD_PIN_*         -> número do GPIO (GPIOn)
 *  - BOARD_*_UART_INDEX  -> número da UART (UART_NUM_n)
 *  - BOARD_ADC_*         -> unidade e canal do ADC
 *
 * Regras do ESP32 que valem aqui (conferidas no teste de host):
 *  - GPIO34 a GPIO39 são só entrada (sem saída e sem pull-up/pull-down).
 *  - ADC1 = GPIO32..GPIO39. O ADC2 não funciona com o Wi-Fi ligado: não use.
 *  - As UARTs saem por qualquer pino pela matriz de GPIO.
 *
 * Livres para expansão: GPIO4, 5, 13, 14, 15, 18, 19, 21, 22, 23, 25, 26, 27,
 * 32, 33 e as entradas GPIO35, 36 e 39 (ADC1). GPIO5 e GPIO15 também são
 * pinos de boot (strapping): não os prenda em nível fixo no reset.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* ------------------------------------------------------------------------- */
/* PZEM-004T v3 — UART2 (passa pelo conversor de nível 5 V <-> 3,3 V)        */
/* Modbus-RTU, 9600 bps, 8N1. O 5 V do lado TTL vem do pino VIN/5V do        */
/* DevKit, que só existe com a placa no USB.                                 */
/* GPIO16/17 são os pinos padrão da UART2 no WROOM-32 (no WROVER são PSRAM). */
/* ------------------------------------------------------------------------- */
#define BOARD_PZEM_UART_INDEX        2      /* UART_NUM_2                     */
#define BOARD_PIN_PZEM_TX            17     /* GPIO17 U2TXD -> RX do PZEM     */
#define BOARD_PIN_PZEM_RX            16     /* GPIO16 U2RXD <- TX do PZEM     */
#define BOARD_PZEM_BAUD              9600u
#define BOARD_PZEM_MODBUS_ADDR       0xF8u  /* endereço "geral" (1 PZEM no barramento) */

/* ------------------------------------------------------------------------- */
/* LED azul da placa (DevKit de 30/38 pinos), aceso em nível alto.           */
/* GPIO2 também é pino de boot: o LED com resistor o mantém em 0 no reset,   */
/* como o modo de gravação exige.                                            */
/* ------------------------------------------------------------------------- */
#define BOARD_PIN_LED                2
#define BOARD_LED_ON_LEVEL           1

/* ------------------------------------------------------------------------- */
/* LM35 — 10 mV/°C, alimentado em 5 V (VIN). A saída nunca passa de 1,5 V:   */
/* vai direto no GPIO34, sem divisor. ADC1 canal 6, atenuação de 2,5 dB.     */
/* ------------------------------------------------------------------------- */
#define BOARD_PIN_LM35               34     /* só entrada: ótimo para sinal analógico */
#define BOARD_ADC_UNIT_LM35          1      /* ADC1 (funciona junto com o Wi-Fi) */
#define BOARD_ADC_CH_LM35            6u     /* ADC1_CH6 = GPIO34 */

/* ------------------------------------------------------------------------- */
/* Console (printf do relatório): UART0 pela ponte USB-serial da placa       */
/* (CP2102/CH340), 115200 bps.                                               */
/* ------------------------------------------------------------------------- */
#define BOARD_CONSOLE_UART_INDEX     0      /* UART_NUM_0 */
#define BOARD_PIN_CONSOLE_TX         1      /* GPIO1 U0TXD */
#define BOARD_PIN_CONSOLE_RX         3      /* GPIO3 U0RXD */
#define BOARD_CONSOLE_BAUD           115200u

/* ------------------------------------------------------------------------- */
/* RESERVADOS — não usar                                                     */
/* ------------------------------------------------------------------------- */
/* Flash SPI do módulo WROOM-32 (ligados por dentro do módulo).              */
#define BOARD_PIN_FLASH_CLK          6      /* SD_CLK / SPICLK */
#define BOARD_PIN_FLASH_SD0          7      /* SD_DATA0 / SPIQ */
#define BOARD_PIN_FLASH_SD1          8      /* SD_DATA1 / SPID */
#define BOARD_PIN_FLASH_SD2          9      /* SD_DATA2 / SPIHD */
#define BOARD_PIN_FLASH_SD3          10     /* SD_DATA3 / SPIWP */
#define BOARD_PIN_FLASH_CMD          11     /* SD_CMD / SPICS0 */
/* Pinos de boot críticos.                                                   */
#define BOARD_PIN_BOOT               0      /* botão BOOT: em 0 no reset = modo de gravação */
#define BOARD_PIN_MTDI               12     /* em 1 no reset troca a tensão da flash para 1,8 V */

/* ------------------------------------------------------------------------- */
/* Listas para validação automática (host test)                              */
/* ------------------------------------------------------------------------- */
#define BOARD_ASSIGNED_PINS { \
    BOARD_PIN_PZEM_TX, BOARD_PIN_PZEM_RX, BOARD_PIN_LED, BOARD_PIN_LM35 }

#define BOARD_RESERVED_PINS { \
    BOARD_PIN_FLASH_CLK, BOARD_PIN_FLASH_SD0, BOARD_PIN_FLASH_SD1, \
    BOARD_PIN_FLASH_SD2, BOARD_PIN_FLASH_SD3, BOARD_PIN_FLASH_CMD, \
    BOARD_PIN_CONSOLE_TX, BOARD_PIN_CONSOLE_RX, \
    BOARD_PIN_BOOT, BOARD_PIN_MTDI }

#endif /* BOARD_PINS_H */
