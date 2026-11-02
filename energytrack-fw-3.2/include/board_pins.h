/**
 * @file board_pins.h
 * @brief Mapa de pinos do EnergyTrack — Raspberry Pi Pico W (RP2040) + PZEM-004T v3.
 *
 * FONTE ÚNICA DE VERDADE para números de GPIO (WPS/EAP 3.2; revisado no 3.1
 * para a Pico W pura, sem BitDogLab).
 *  - Nenhum outro arquivo do firmware deve usar número de GPIO literal.
 *  - Este header NÃO depende do pico-sdk, para poder ser validado no host
 *    (ver test/host/test_pinmap.c).
 *
 * Convenções:
 *  - BOARD_PIN_*    -> número do GPIO (GPn)
 *  - BOARD_*_INDEX  -> índice do bloco de hardware (uart0 = 0...)
 *  - BOARD_ADC_CH_* -> canal do ADC (GP26 = ch0 ... ch4 = sensor interno)
 *
 * Livres para expansão: GP2 a GP22 e GP26 a GP28 (ADC0 a ADC2).
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* ------------------------------------------------------------------------- */
/* PZEM-004T v3 — UART0 (passa pelo conversor de nível 5 V <-> 3,3 V)        */
/* Modbus-RTU, 9600 bps, 8N1. Pino físico 1 = GP0, 2 = GP1, 3 = GND.         */
/* O 5 V do lado TTL vem do VBUS (pino 40): só existe com a Pico no USB.     */
/* ------------------------------------------------------------------------- */
#define BOARD_PZEM_UART_INDEX        0      /* uart0                          */
#define BOARD_PIN_PZEM_TX            0      /* GP0  UART0 TX -> RX do PZEM    */
#define BOARD_PIN_PZEM_RX            1      /* GP1  UART0 RX <- TX do PZEM    */
#define BOARD_PZEM_BAUD              9600u
#define BOARD_PZEM_MODBUS_ADDR       0xF8u  /* endereço "geral" (1 PZEM no barramento) */

/* ------------------------------------------------------------------------- */
/* Recursos internos da Pico W                                               */
/* ------------------------------------------------------------------------- */
#define BOARD_ADC_CH_TEMP            4u     /* sensor de temperatura interno (sem pino) */
#define BOARD_LED_WL_GPIO            0u     /* LED da placa: WL_GPIO0 do CYW43, não é GPIO do RP2040 */

/* ------------------------------------------------------------------------- */
/* RESERVADOS — não usar                                                     */
/* ------------------------------------------------------------------------- */
/* Pinos internos do módulo Wi-Fi CYW43439 (não expostos na placa).          */
#define BOARD_PIN_CYW43_WL_ON        23
#define BOARD_PIN_CYW43_DATA_IRQ     24
#define BOARD_PIN_CYW43_CS           25
#define BOARD_PIN_CYW43_CLK_VSYS     29     /* também ADC3 (VSYS/3) — exige parar o Wi-Fi */

/* ------------------------------------------------------------------------- */
/* Listas para validação automática (host test)                              */
/* ------------------------------------------------------------------------- */
#define BOARD_ASSIGNED_PINS { BOARD_PIN_PZEM_TX, BOARD_PIN_PZEM_RX }

#define BOARD_RESERVED_PINS { \
    BOARD_PIN_CYW43_WL_ON, BOARD_PIN_CYW43_DATA_IRQ, \
    BOARD_PIN_CYW43_CS, BOARD_PIN_CYW43_CLK_VSYS }

#endif /* BOARD_PINS_H */
