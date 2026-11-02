/**
 * @file board_pins.h
 * @brief Mapa de pinos do EnergyTrack — BitDogLab v7 + Raspberry Pi Pico W (RP2040).
 *
 * FONTE ÚNICA DE VERDADE para números de GPIO (WPS/EAP 3.2).
 *  - Nenhum outro arquivo do firmware deve usar número de GPIO literal.
 *  - Este header NÃO depende do pico-sdk, para poder ser validado no host
 *    (ver test/host/test_pinmap.c).
 *
 * Convenções:
 *  - BOARD_PIN_*   -> número do GPIO (GPn)
 *  - BOARD_*_INDEX -> índice do bloco de hardware (uart0 = 0, i2c1 = 1...)
 *  - BOARD_ADC_CH_* -> canal do ADC (GP26 = ch0, GP27 = ch1, GP28 = ch2)
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* ------------------------------------------------------------------------- */
/* PZEM-004T v3 — UART0 no conector JST (passa pelo conversor 5 V <-> 3,3 V) */
/* Modbus-RTU, 9600 bps, 8N1.                                                */
/* ------------------------------------------------------------------------- */
#define BOARD_PZEM_UART_INDEX        0      /* uart0                          */
#define BOARD_PIN_PZEM_TX            0      /* GP0  UART0 TX -> RX do PZEM    */
#define BOARD_PIN_PZEM_RX            1      /* GP1  UART0 RX <- TX do PZEM    */
#define BOARD_PZEM_BAUD              9600u
#define BOARD_PZEM_MODBUS_ADDR       0xF8u  /* endereço "geral" (1 PZEM no barramento) */

/* ------------------------------------------------------------------------- */
/* Display OLED SSD1306 128x64 — I2C1 (BitDogLab v7; na v6.x era GP14/GP15)  */
/* ------------------------------------------------------------------------- */
#define BOARD_OLED_I2C_INDEX         1      /* i2c1                           */
#define BOARD_PIN_OLED_SDA           2      /* GP2  I2C1 SDA                  */
#define BOARD_PIN_OLED_SCL           3      /* GP3  I2C1 SCL                  */
#define BOARD_OLED_I2C_ADDR          0x3Cu
#define BOARD_OLED_I2C_HZ            400000u

/* ------------------------------------------------------------------------- */
/* Entradas digitais — ativo em nível baixo, pull-up interno                 */
/* ------------------------------------------------------------------------- */
#define BOARD_PIN_BTN_A              5      /* GP5  botão A                   */
#define BOARD_PIN_BTN_B              6      /* GP6  botão B                   */
#define BOARD_PIN_JOY_SW             22     /* GP22 clique do joystick        */

/* ------------------------------------------------------------------------- */
/* Matriz 5x5 de LEDs WS2812 — dirigida por PIO                              */
/* ------------------------------------------------------------------------- */
#define BOARD_PIN_LED_MATRIX         7      /* GP7  DIN da matriz             */
#define BOARD_LED_MATRIX_COUNT       25u
/* Teto de brilho por canal (0-255). 25 LEDs em branco pleno passam de 1 A;  */
/* com 32/255 o pior caso fica perto de 190 mA.                              */
#define BOARD_LED_MATRIX_MAX_LEVEL   32u

/* ------------------------------------------------------------------------- */
/* LED RGB (anodos via PWM)                                                  */
/* PWM slice = (gpio >> 1) & 7 ; canal = gpio & 1 (A = 0, B = 1)             */
/* ------------------------------------------------------------------------- */
#define BOARD_PIN_RGB_G              11     /* GP11 PWM5 B                    */
#define BOARD_PIN_RGB_B              12     /* GP12 PWM6 A                    */
#define BOARD_PIN_RGB_R              13     /* GP13 PWM6 B                    */

/* ------------------------------------------------------------------------- */
/* Buzzer passivo — PWM                                                      */
/* ------------------------------------------------------------------------- */
#define BOARD_PIN_BUZZER             21     /* GP21 PWM2 B                    */

/* ------------------------------------------------------------------------- */
/* Entradas analógicas — o RP2040 tem UM conversor ADC multiplexado          */
/* ------------------------------------------------------------------------- */
#define BOARD_PIN_JOY_Y              26     /* GP26 ADC0                      */
#define BOARD_PIN_JOY_X              27     /* GP27 ADC1                      */
#define BOARD_PIN_MIC                28     /* GP28 ADC2                      */
#define BOARD_ADC_CH_JOY_Y           0u
#define BOARD_ADC_CH_JOY_X           1u
#define BOARD_ADC_CH_MIC             2u
#define BOARD_ADC_CH_TEMP            4u     /* sensor de temperatura interno  */

/* ------------------------------------------------------------------------- */
/* RESERVADOS — não usar                                                     */
/* ------------------------------------------------------------------------- */
/* GP10: buzzer B na BitDogLab v6.x. Mantido reservado até confirmar no      */
/* esquemático da v7. NUNCA usar como PWM: divide o slice 5 com o RGB verde  */
/* (GP11), e um tom no buzzer mudaria a frequência do LED.                   */
#define BOARD_PIN_RESERVED_GP10      10
/* Pico W: pinos internos do módulo Wi-Fi CYW43439 (não expostos na placa).  */
#define BOARD_PIN_CYW43_WL_ON        23
#define BOARD_PIN_CYW43_DATA_IRQ     24
#define BOARD_PIN_CYW43_CS           25
#define BOARD_PIN_CYW43_CLK_VSYS     29     /* também ADC3 (VSYS/3) — exige parar o Wi-Fi */

/* ------------------------------------------------------------------------- */
/* Listas para validação automática (host test e asserts de inicialização)   */
/* ------------------------------------------------------------------------- */
#define BOARD_ASSIGNED_PINS { \
    BOARD_PIN_PZEM_TX, BOARD_PIN_PZEM_RX, \
    BOARD_PIN_OLED_SDA, BOARD_PIN_OLED_SCL, \
    BOARD_PIN_BTN_A, BOARD_PIN_BTN_B, BOARD_PIN_JOY_SW, \
    BOARD_PIN_LED_MATRIX, \
    BOARD_PIN_RGB_G, BOARD_PIN_RGB_B, BOARD_PIN_RGB_R, \
    BOARD_PIN_BUZZER, \
    BOARD_PIN_JOY_Y, BOARD_PIN_JOY_X, BOARD_PIN_MIC }

#define BOARD_RESERVED_PINS { \
    BOARD_PIN_RESERVED_GP10, \
    BOARD_PIN_CYW43_WL_ON, BOARD_PIN_CYW43_DATA_IRQ, \
    BOARD_PIN_CYW43_CS, BOARD_PIN_CYW43_CLK_VSYS }

/* Pinos que usam a função PWM, agrupados por "dono". Grupos diferentes não  */
/* podem compartilhar slice (frequência/wrap são por slice).                 */
#define BOARD_PWM_GROUP_RGB          { BOARD_PIN_RGB_G, BOARD_PIN_RGB_B, BOARD_PIN_RGB_R }
#define BOARD_PWM_GROUP_BUZZER       { BOARD_PIN_BUZZER }

#endif /* BOARD_PINS_H */
