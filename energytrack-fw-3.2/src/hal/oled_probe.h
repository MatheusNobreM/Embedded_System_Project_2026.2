/**
 * @file oled_probe.h
 * @brief Verificação mínima do SSD1306 no I2C1 (sem framebuffer).
 *
 * Serve ao self-test: liga o painel e alterna "todos os pixels acesos" /
 * "conteúdo da RAM". O driver gráfico completo do projeto continua o mesmo.
 */
#ifndef OLED_PROBE_H
#define OLED_PROBE_H

#include <stdbool.h>

bool oled_probe_init(void);
/** true = comando aceito (ACK). on = todos os pixels acesos. */
bool oled_probe_set_all_on(bool on);

#endif /* OLED_PROBE_H */
