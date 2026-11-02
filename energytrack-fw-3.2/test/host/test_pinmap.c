/**
 * Teste de host do mapa de pinos e do protocolo do PZEM (sem placa, sem SDK).
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal -Itest/host \
 *       test/host/test_pinmap.c src/hal/pzem_modbus.c -o build-host/test_pinmap
 *   ./build-host/test_pinmap
 *
 * Regras de hardware do RP2040 verificadas:
 *  - UART0: TX em {0,12,16,28}, RX em {1,13,17,29}
 *  - ADC: GP26..GP29 -> canal gpio - 26; canal 4 = sensor de temperatura
 */
#include <string.h>
#include "board_pins.h"
#include "pzem_modbus.h"
#include "tap.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int in_list(int v, const int *list, size_t n)
{
    for (size_t i = 0; i < n; ++i) if (list[i] == v) return 1;
    return 0;
}

static void test_only_picow_pins(void)
{
    const int assigned[] = BOARD_ASSIGNED_PINS;
    const int reserved[] = BOARD_RESERVED_PINS;
    const int expected_reserved[] = {23, 24, 25, 29};

    TAP_CHECK(COUNT(assigned) == 2, "esperava só GP0/GP1 atribuídos, há %u pinos", (unsigned)COUNT(assigned));
    TAP_CHECK(in_list(0, assigned, COUNT(assigned)) && in_list(1, assigned, COUNT(assigned)),
              "GP0/GP1 deveriam estar atribuídos ao PZEM");
    for (size_t i = 0; i < COUNT(expected_reserved); ++i)
        TAP_CHECK(in_list(expected_reserved[i], reserved, COUNT(reserved)),
                  "GP%d (CYW43) deveria estar reservado", expected_reserved[i]);
    for (size_t i = 0; i < COUNT(assigned); ++i)
        TAP_CHECK(!in_list(assigned[i], reserved, COUNT(reserved)), "GP%d está reservado", assigned[i]);
}

static void test_no_bitdoglab_leftovers(void)
{
    int leftovers = 0;
#if defined(BOARD_PIN_OLED_SDA) || defined(BOARD_PIN_OLED_SCL) || defined(BOARD_OLED_I2C_INDEX)
    leftovers++;
#endif
#if defined(BOARD_PIN_BTN_A) || defined(BOARD_PIN_BTN_B) || defined(BOARD_PIN_JOY_SW)
    leftovers++;
#endif
#if defined(BOARD_PIN_LED_MATRIX) || defined(BOARD_PIN_RGB_R) || defined(BOARD_PIN_RGB_G) || defined(BOARD_PIN_RGB_B)
    leftovers++;
#endif
#if defined(BOARD_PIN_BUZZER) || defined(BOARD_PIN_RESERVED_GP10)
    leftovers++;
#endif
#if defined(BOARD_PIN_JOY_X) || defined(BOARD_PIN_JOY_Y) || defined(BOARD_PIN_MIC)
    leftovers++;
#endif
    TAP_CHECK(leftovers == 0, "%d grupo(s) de pinos da BitDogLab ainda no mapa", leftovers);
}

static void test_uart_function_pins(void)
{
    const int uart0_tx[] = {0, 12, 16, 28};
    const int uart0_rx[] = {1, 13, 17, 29};
    TAP_CHECK(BOARD_PZEM_UART_INDEX == 0, "PZEM deveria estar na uart0");
    TAP_CHECK(in_list(BOARD_PIN_PZEM_TX, uart0_tx, COUNT(uart0_tx)), "GP%d não é UART0 TX", BOARD_PIN_PZEM_TX);
    TAP_CHECK(in_list(BOARD_PIN_PZEM_RX, uart0_rx, COUNT(uart0_rx)), "GP%d não é UART0 RX", BOARD_PIN_PZEM_RX);
    /* Nenhum pino de ADC ocupado pela UART (ADC0..ADC2 ficam livres). */
    TAP_CHECK(BOARD_PIN_PZEM_TX < 26 && BOARD_PIN_PZEM_RX < 26, "UART do PZEM ocupando pino de ADC");
    TAP_CHECK(BOARD_ADC_CH_TEMP == 4u, "sensor de temperatura interno é o canal 4 do ADC");
}

static void test_modbus_crc(void)
{
    /* Vetor clássico da especificação Modbus: 01 03 00 00 00 0A -> CRC C5 CD */
    const uint8_t ref[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint16_t crc = pzem_crc16(ref, sizeof ref);
    TAP_CHECK((crc & 0xFF) == 0xC5 && (crc >> 8) == 0xCD, "CRC de referência = %02X %02X",
              crc & 0xFF, crc >> 8);

    uint8_t req[PZEM_REQ_LEN];
    pzem_build_read_request(BOARD_PZEM_MODBUS_ADDR, req);
    TAP_CHECK(pzem_crc16(req, PZEM_REQ_LEN) == 0, "CRC do quadro completo deveria zerar");
}

static void build_response(uint8_t frame[PZEM_RESP_LEN], const uint16_t regs[PZEM_REG_COUNT])
{
    frame[0] = BOARD_PZEM_MODBUS_ADDR;
    frame[1] = PZEM_FUNC_READ_IN;
    frame[2] = 2 * PZEM_REG_COUNT;
    for (unsigned i = 0; i < PZEM_REG_COUNT; ++i) {
        frame[3 + 2 * i] = (uint8_t)(regs[i] >> 8);
        frame[4 + 2 * i] = (uint8_t)(regs[i] & 0xFF);
    }
    uint16_t crc = pzem_crc16(frame, PZEM_RESP_LEN - 2);
    frame[23] = (uint8_t)(crc & 0xFF);
    frame[24] = (uint8_t)(crc >> 8);
}

static void test_modbus_parse(void)
{
    /* 220,5 V; 1,234 A; 271,9 W; 70000 Wh (cruza 16 bits); 60,0 Hz; FP 0,98 */
    const uint16_t regs[PZEM_REG_COUNT] = {
        2205, 1234, 0, 2719, 0, (uint16_t)(70000u & 0xFFFF), (uint16_t)(70000u >> 16), 600, 98, 0};
    uint8_t frame[PZEM_RESP_LEN];
    build_response(frame, regs);

    pzem_measurement_t m;
    memset(&m, 0, sizeof m);
    TAP_CHECK(pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, frame, sizeof frame, &m) == PZEM_PARSE_OK,
              "quadro válido rejeitado");
    TAP_CHECK(m.voltage_dv == 2205 && m.current_ma == 1234 && m.power_dw == 2719, "V/I/P errados");
    TAP_CHECK(m.energy_wh == 70000u && m.freq_dhz == 600 && m.pf_centi == 98, "E/f/FP errados");

    frame[10] ^= 0x01;  /* um bit trocado na linha */
    TAP_CHECK(pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, frame, sizeof frame, &m) == PZEM_PARSE_BAD_CRC,
              "bit corrompido não detectado");
    TAP_CHECK(pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, frame, 12, &m) == PZEM_PARSE_SHORT,
              "quadro curto não detectado");

    uint8_t exc[5] = {BOARD_PZEM_MODBUS_ADDR, PZEM_FUNC_READ_IN | 0x80, 0x02, 0, 0};
    uint16_t crc = pzem_crc16(exc, 3);
    exc[3] = (uint8_t)(crc & 0xFF);
    exc[4] = (uint8_t)(crc >> 8);
    TAP_CHECK(pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, exc, sizeof exc, &m) == PZEM_PARSE_EXCEPTION,
              "exceção Modbus não detectada");
}

int main(void)
{
    tap_run("AC-019 só GP0/GP1 atribuídos e pinos do CYW43 reservados", test_only_picow_pins);
    tap_run("AC-019 nenhum pino da BitDogLab no mapa", test_no_bitdoglab_leftovers);
    tap_run("pinos do PZEM têm função UART0 e o ADC fica livre", test_uart_function_pins);
    tap_run("CRC-16/MODBUS do PZEM", test_modbus_crc);
    tap_run("decodificação da resposta do PZEM", test_modbus_parse);
    return tap_done();
}
