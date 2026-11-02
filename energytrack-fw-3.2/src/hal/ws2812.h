/**
 * @file ws2812.h
 * @brief Matriz 5x5 WS2812 via PIO, com teto de brilho aplicado no driver.
 */
#ifndef WS2812_H
#define WS2812_H

#include <stdbool.h>
#include <stdint.h>

/** Reserva PIO/SM livres dinamicamente (convive com o PIO do CYW43). */
bool ws2812_init(unsigned gpio);

/** Define um pixel no buffer local (não envia). Cores 0-255; o driver limita o brilho. */
void ws2812_set_pixel(unsigned index, uint8_t r, uint8_t g, uint8_t b);

/** Posição (x, y) da matriz, 0..4, para o índice da cadeia em serpentina. */
unsigned ws2812_xy_to_index(unsigned x, unsigned y);

void ws2812_clear(void);

/** Envia o buffer inteiro. Bloqueia no máximo ~0,8 ms (25 LEDs x 30 µs). */
void ws2812_show(void);

#endif /* WS2812_H */
