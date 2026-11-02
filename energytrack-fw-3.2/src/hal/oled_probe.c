#include "oled_probe.h"

#include <stddef.h>
#include <stdint.h>
#include "board_resources.h"

#define OLED_I2C_TIMEOUT_US 5000u
#define SSD1306_CTRL_CMD    0x00u

static bool send_cmds(const uint8_t *cmds, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const uint8_t buf[2] = {SSD1306_CTRL_CMD, cmds[i]};
        /* Com timeout: um OLED ausente/travado não pode congelar a tarefa de UI. */
        int rc = i2c_write_timeout_us(BOARD_OLED_I2C, BOARD_OLED_I2C_ADDR, buf, 2, false,
                                      OLED_I2C_TIMEOUT_US);
        if (rc != 2) return false;
    }
    return true;
}

bool oled_probe_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE,        /* display off */
        0xD5, 0x80,  /* clock */
        0xA8, 0x3F,  /* multiplex 64 linhas */
        0xD3, 0x00,  /* offset */
        0x40,        /* linha inicial 0 */
        0x8D, 0x14,  /* charge pump ligado */
        0x20, 0x00,  /* endereçamento horizontal */
        0xA1, 0xC8,  /* espelhamento para a orientação da placa */
        0xDA, 0x12,  /* pinos COM */
        0x81, 0x7F,  /* contraste */
        0xD9, 0xF1,  /* pré-carga */
        0xDB, 0x40,  /* VCOMH */
        0xA4,        /* exibe a RAM */
        0xA6,        /* normal (não invertido) */
        0xAF,        /* display on */
    };
    return send_cmds(init_seq, sizeof init_seq);
}

bool oled_probe_set_all_on(bool on)
{
    const uint8_t cmd = on ? 0xA5u : 0xA4u;
    return send_cmds(&cmd, 1);
}
