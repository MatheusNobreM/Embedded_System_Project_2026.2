/**
 * Teste de host do mapa de pinos e do protocolo do PZEM (sem placa, sem SDK).
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc/hal -Itest/host \
 *       test/host/test_pinmap.c src/hal/pzem_modbus.c -o build-host/test_pinmap
 *   ./build-host/test_pinmap
 *
 * Regras de hardware do ESP32 (ESP32-WROOM-32) verificadas:
 *  - GPIOs que existem: 0..19, 21..23, 25..27, 32..39
 *  - GPIO34..39 só entrada (não servem para TX nem LED)
 *  - UART pela matriz de GPIO: TX em pino com saída, RX em qualquer pino
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

static int gpio_exists(int gpio)
{
    return (gpio >= 0 && gpio <= 19) || (gpio >= 21 && gpio <= 23) ||
           (gpio >= 25 && gpio <= 27) || (gpio >= 32 && gpio <= 39);
}

static int gpio_can_output(int gpio)
{
    return gpio_exists(gpio) && gpio < 34;
}

static void test_only_devkit_pins(void)
{
    const int assigned[] = BOARD_ASSIGNED_PINS;
    const int reserved[] = BOARD_RESERVED_PINS;
    const int expected_assigned[] = {17, 16, 2};
    const int expected_reserved[] = {6, 7, 8, 9, 10, 11, 1, 3, 0, 12};

    TAP_CHECK(COUNT(assigned) == COUNT(expected_assigned), "esperava %u pinos atribuídos, há %u",
              (unsigned)COUNT(expected_assigned), (unsigned)COUNT(assigned));
    for (size_t i = 0; i < COUNT(expected_assigned); ++i)
        TAP_CHECK(in_list(expected_assigned[i], assigned, COUNT(assigned)),
                  "GPIO%d deveria estar atribuído", expected_assigned[i]);
    TAP_CHECK(BOARD_PIN_PZEM_TX == 17 && BOARD_PIN_PZEM_RX == 16, "PZEM deveria estar em GPIO17 TX / GPIO16 RX");
    TAP_CHECK(BOARD_PIN_LED == 2, "LED da placa deveria ser o GPIO2");

    TAP_CHECK(COUNT(reserved) == COUNT(expected_reserved), "esperava %u pinos reservados, há %u",
              (unsigned)COUNT(expected_reserved), (unsigned)COUNT(reserved));
    for (size_t i = 0; i < COUNT(expected_reserved); ++i)
        TAP_CHECK(in_list(expected_reserved[i], reserved, COUNT(reserved)),
                  "GPIO%d (flash, console ou boot) deveria estar reservado", expected_reserved[i]);
    for (size_t i = 0; i < COUNT(assigned); ++i)
        TAP_CHECK(!in_list(assigned[i], reserved, COUNT(reserved)), "GPIO%d está reservado", assigned[i]);
    for (size_t i = 0; i < COUNT(assigned); ++i)
        for (size_t j = i + 1; j < COUNT(assigned); ++j)
            TAP_CHECK(assigned[i] != assigned[j], "GPIO%d atribuído duas vezes", assigned[i]);
}

static void test_pins_fit_their_function(void)
{
    const int assigned[] = BOARD_ASSIGNED_PINS;
    for (size_t i = 0; i < COUNT(assigned); ++i)
        TAP_CHECK(gpio_exists(assigned[i]), "GPIO%d não existe no ESP32", assigned[i]);

    TAP_CHECK(BOARD_PZEM_UART_INDEX == 2, "PZEM deveria estar na UART2");
    TAP_CHECK(BOARD_CONSOLE_UART_INDEX == 0, "console deveria estar na UART0 (ponte USB-serial)");
    TAP_CHECK(BOARD_PZEM_UART_INDEX != BOARD_CONSOLE_UART_INDEX, "PZEM dividindo a UART do console");
    TAP_CHECK(gpio_can_output(BOARD_PIN_PZEM_TX), "GPIO%d não tem saída: não serve para TX", BOARD_PIN_PZEM_TX);
    TAP_CHECK(gpio_exists(BOARD_PIN_PZEM_RX), "GPIO%d não serve para RX", BOARD_PIN_PZEM_RX);
    TAP_CHECK(gpio_can_output(BOARD_PIN_LED), "GPIO%d não tem saída: não acende LED", BOARD_PIN_LED);

}

static void test_no_leftovers(void)
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

    int picow = 0;
#if defined(BOARD_PIN_CYW43_WL_ON) || defined(BOARD_PIN_CYW43_DATA_IRQ) || defined(BOARD_PIN_CYW43_CS) || defined(BOARD_PIN_CYW43_CLK_VSYS)
    picow++;
#endif
#if defined(BOARD_LED_WL_GPIO) || defined(BOARD_ADC_CH_TEMP)
    picow++;
#endif
    TAP_CHECK(picow == 0, "%d grupo(s) de definições da Pico W ainda no mapa", picow);

#if defined(BOARD_PIN_LM35) || defined(BOARD_ADC_UNIT_LM35) || defined(BOARD_ADC_CH_LM35)
    TAP_CHECK(0, "definições do LM35 ainda no mapa");
#endif
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
    tap_run("AC-019 só PZEM (GPIO17/16) e LED da placa (GPIO2) atribuídos; flash, console e boot reservados",
            test_only_devkit_pins);
    tap_run("AC-019 cada pino atribuído é capaz da sua função (UART2 e saída)", test_pins_fit_their_function);
    tap_run("AC-019 nenhum pino da BitDogLab, da Pico W nem do LM35 no mapa", test_no_leftovers);
    tap_run("CRC-16/MODBUS do PZEM", test_modbus_crc);
    tap_run("decodificação da resposta do PZEM", test_modbus_parse);
    return tap_done();
}
