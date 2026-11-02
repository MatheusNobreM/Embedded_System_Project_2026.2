/**
 * Teste de host do mapa de pinos e do protocolo do PZEM (sem placa, sem SDK).
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal \
 *       test/host/test_pinmap.c src/hal/pzem_modbus.c -o build-host/test_pinmap
 *   ./build-host/test_pinmap
 *
 * Regras de hardware do RP2040 verificadas:
 *  - PWM: slice = (gpio >> 1) & 7, canal = gpio & 1
 *  - UART0: TX em {0,12,16,28}, RX em {1,13,17,29}
 *  - I2Cn: SDA quando gpio % 4 == 2n, SCL quando gpio % 4 == 2n + 1
 *  - ADC: GP26..GP29 -> canal gpio - 26
 */
#include <stdio.h>
#include <string.h>
#include "board_pins.h"
#include "pzem_modbus.h"

static int g_failures = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { g_failures++; printf("  FALHA: " __VA_ARGS__); printf("\n"); } \
} while (0)
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static unsigned pwm_slice(int gpio) { return ((unsigned)gpio >> 1) & 7u; }

static int in_list(int v, const int *list, size_t n)
{
    for (size_t i = 0; i < n; ++i) if (list[i] == v) return 1;
    return 0;
}

static void test_pins_unique_and_not_reserved(void)
{
    printf("[pinos] unicidade e reservados\n");
    const int assigned[] = BOARD_ASSIGNED_PINS;
    const int reserved[] = BOARD_RESERVED_PINS;
    for (size_t i = 0; i < COUNT(assigned); ++i) {
        CHECK(assigned[i] >= 0 && assigned[i] <= 29, "GP%d fora do RP2040", assigned[i]);
        for (size_t j = i + 1; j < COUNT(assigned); ++j)
            CHECK(assigned[i] != assigned[j], "GP%d atribuido duas vezes", assigned[i]);
        CHECK(!in_list(assigned[i], reserved, COUNT(reserved)), "GP%d esta reservado", assigned[i]);
    }
}

static void test_uart_and_i2c_function_pins(void)
{
    printf("[pinos] funcoes UART0 / I2C1 validas\n");
    const int uart0_tx[] = {0, 12, 16, 28};
    const int uart0_rx[] = {1, 13, 17, 29};
    CHECK(BOARD_PZEM_UART_INDEX == 0, "PZEM deveria estar na uart0");
    CHECK(in_list(BOARD_PIN_PZEM_TX, uart0_tx, COUNT(uart0_tx)), "GP%d nao e UART0 TX", BOARD_PIN_PZEM_TX);
    CHECK(in_list(BOARD_PIN_PZEM_RX, uart0_rx, COUNT(uart0_rx)), "GP%d nao e UART0 RX", BOARD_PIN_PZEM_RX);

    CHECK(BOARD_PIN_OLED_SDA % 4 == 2 * BOARD_OLED_I2C_INDEX, "GP%d nao e SDA do I2C%d",
          BOARD_PIN_OLED_SDA, BOARD_OLED_I2C_INDEX);
    CHECK(BOARD_PIN_OLED_SCL % 4 == 2 * BOARD_OLED_I2C_INDEX + 1, "GP%d nao e SCL do I2C%d",
          BOARD_PIN_OLED_SCL, BOARD_OLED_I2C_INDEX);
}

static void test_adc_channels(void)
{
    printf("[pinos] canais do ADC\n");
    CHECK(BOARD_PIN_JOY_Y - 26 == (int)BOARD_ADC_CH_JOY_Y, "canal do JOY_Y incoerente");
    CHECK(BOARD_PIN_JOY_X - 26 == (int)BOARD_ADC_CH_JOY_X, "canal do JOY_X incoerente");
    CHECK(BOARD_PIN_MIC   - 26 == (int)BOARD_ADC_CH_MIC,   "canal do MIC incoerente");
    /* Nenhum pino de ADC pode estar ocupado por UART (preservar ADCs internos). */
    CHECK(BOARD_PIN_PZEM_TX < 26 && BOARD_PIN_PZEM_RX < 26, "UART do PZEM ocupando pino de ADC");
}

static void test_pwm_slices(void)
{
    printf("[pwm] grupos diferentes nao compartilham slice\n");
    const int rgb[] = BOARD_PWM_GROUP_RGB;
    const int buz[] = BOARD_PWM_GROUP_BUZZER;
    for (size_t i = 0; i < COUNT(rgb); ++i)
        for (size_t j = 0; j < COUNT(buz); ++j)
            CHECK(pwm_slice(rgb[i]) != pwm_slice(buz[j]), "RGB GP%d e buzzer GP%d no slice %u",
                  rgb[i], buz[j], pwm_slice(rgb[i]));
    /* GP10 (reservado) divide slice com o RGB verde: registrar o motivo da reserva. */
    CHECK(pwm_slice(BOARD_PIN_RESERVED_GP10) == pwm_slice(BOARD_PIN_RGB_G),
          "premissa da reserva do GP10 mudou; revisar comentario em board_pins.h");
    printf("  slices: G=%u B=%u R=%u buzzer=%u\n", pwm_slice(BOARD_PIN_RGB_G),
           pwm_slice(BOARD_PIN_RGB_B), pwm_slice(BOARD_PIN_RGB_R), pwm_slice(BOARD_PIN_BUZZER));
}

static void test_modbus_crc(void)
{
    printf("[pzem] CRC-16/MODBUS\n");
    /* Vetor classico da especificacao Modbus: 01 03 00 00 00 0A -> CRC C5 CD */
    const uint8_t ref[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint16_t crc = pzem_crc16(ref, sizeof ref);
    CHECK((crc & 0xFF) == 0xC5 && (crc >> 8) == 0xCD, "CRC de referencia = %02X %02X",
          crc & 0xFF, crc >> 8);

    uint8_t req[PZEM_REQ_LEN];
    pzem_build_read_request(BOARD_PZEM_MODBUS_ADDR, req);
    printf("  requisicao: ");
    for (unsigned i = 0; i < PZEM_REQ_LEN; ++i) printf("%02X ", req[i]);
    printf("\n");
    CHECK(pzem_crc16(req, PZEM_REQ_LEN) == 0, "CRC do quadro completo deveria zerar");
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
    printf("[pzem] decodificacao da resposta\n");
    /* 220,5 V; 1,234 A; 271,9 W; 70000 Wh (cruza 16 bits); 60,0 Hz; FP 0,98 */
    const uint16_t regs[PZEM_REG_COUNT] = {
        2205, 1234, 0, 2719, 0, (uint16_t)(70000u & 0xFFFF), (uint16_t)(70000u >> 16), 600, 98, 0};
    uint8_t frame[PZEM_RESP_LEN];
    build_response(frame, regs);

    pzem_measurement_t m;
    memset(&m, 0, sizeof m);
    CHECK(pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, frame, sizeof frame, &m) == PZEM_PARSE_OK,
          "quadro valido rejeitado");
    CHECK(m.voltage_dv == 2205 && m.current_ma == 1234 && m.power_dw == 2719, "V/I/P errados");
    CHECK(m.energy_wh == 70000u && m.freq_dhz == 600 && m.pf_centi == 98, "E/f/FP errados");

    frame[10] ^= 0x01;  /* um bit trocado na linha */
    CHECK(pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, frame, sizeof frame, &m) == PZEM_PARSE_BAD_CRC,
          "bit corrompido nao detectado");
    CHECK(pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, frame, 12, &m) == PZEM_PARSE_SHORT,
          "quadro curto nao detectado");

    uint8_t exc[5] = {BOARD_PZEM_MODBUS_ADDR, PZEM_FUNC_READ_IN | 0x80, 0x02, 0, 0};
    uint16_t crc = pzem_crc16(exc, 3);
    exc[3] = (uint8_t)(crc & 0xFF);
    exc[4] = (uint8_t)(crc >> 8);
    CHECK(pzem_parse_response(BOARD_PZEM_MODBUS_ADDR, exc, sizeof exc, &m) == PZEM_PARSE_EXCEPTION,
          "excecao Modbus nao detectada");
}

int main(void)
{
    test_pins_unique_and_not_reserved();
    test_uart_and_i2c_function_pins();
    test_adc_channels();
    test_pwm_slices();
    test_modbus_crc();
    test_modbus_parse();
    printf(g_failures ? "\nRESULTADO: %d falha(s)\n" : "\nRESULTADO: OK\n", g_failures);
    return g_failures ? 1 : 0;
}
