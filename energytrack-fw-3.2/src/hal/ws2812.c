#include "ws2812.h"

#include <string.h>
#include "hardware/pio.h"
#include "board_pins.h"
#include "ws2812.pio.h"

#define WS2812_FREQ_HZ 800000.0f

static PIO s_pio;
static uint s_sm;
static uint32_t s_grb[BOARD_LED_MATRIX_COUNT];

bool ws2812_init(unsigned gpio)
{
    uint offset;
    /* Procura um PIO com espaço de instrução e SM livres para este GPIO.
     * Nunca fixar pio0/sm0: o driver do CYW43 (Wi-Fi da Pico W) também usa PIO. */
    if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &s_pio, &s_sm,
                                                          &offset, gpio, 1, true)) {
        return false;
    }
    ws2812_program_init(s_pio, s_sm, offset, gpio, WS2812_FREQ_HZ);
    ws2812_clear();
    ws2812_show();
    return true;
}

static uint8_t limit(uint8_t level)
{
    return (uint8_t)(((uint32_t)level * BOARD_LED_MATRIX_MAX_LEVEL) / 255u);
}

void ws2812_set_pixel(unsigned index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= BOARD_LED_MATRIX_COUNT) return;
    s_grb[index] = ((uint32_t)limit(g) << 16) | ((uint32_t)limit(r) << 8) | limit(b);
}

unsigned ws2812_xy_to_index(unsigned x, unsigned y)
{
    /* Cadeia em serpentina começando no canto inferior. A orientação exata
     * (qual canto é o LED 0) deve ser conferida na placa. */
    unsigned row = 4u - y;
    return (row % 2u == 0u) ? row * 5u + (4u - x) : row * 5u + x;
}

void ws2812_clear(void)
{
    memset(s_grb, 0, sizeof s_grb);
}

void ws2812_show(void)
{
    for (unsigned i = 0; i < BOARD_LED_MATRIX_COUNT; ++i) {
        pio_sm_put_blocking(s_pio, s_sm, s_grb[i] << 8u);  /* 24 bits alinhados à esquerda */
    }
}
