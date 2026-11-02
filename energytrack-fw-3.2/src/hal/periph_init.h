/**
 * @file periph_init.h
 * @brief Inicialização dos periféricos da placa a partir do mapa em board_pins.h.
 *
 * Cada função inicializa UM periférico e não cria tarefas: a posse em tempo
 * de execução (qual tarefa usa o quê) é decidida pela aplicação.
 */
#ifndef PERIPH_INIT_H
#define PERIPH_INIT_H

#include <stdbool.h>
#include <stdint.h>

/* Bits de periph_init_all(): bit ligado = falhou. */
typedef enum {
    PERIPH_ERR_NONE   = 0,
    PERIPH_ERR_OLED   = 1u << 0,  /* OLED não respondeu ACK no I2C1 */
    PERIPH_ERR_MATRIX = 1u << 1,  /* sem state machine de PIO livre */
} periph_err_t;

void periph_init_pzem_uart(void);
void periph_init_oled_i2c(void);
void periph_init_inputs(void);       /* botões A/B + clique do joystick */
void periph_init_rgb_pwm(void);
void periph_init_buzzer_pwm(void);
void periph_init_adc(void);          /* joystick X/Y + microfone */

/** Inicializa tudo na ordem segura. Chamar DEPOIS de cyw43_arch_init(). */
uint32_t periph_init_all(void);

/* Atuadores simples usados pela UI. */
void rgb_set(uint8_t r, uint8_t g, uint8_t b);   /* 0-255 por canal */
void buzzer_tone(uint32_t freq_hz);              /* 0 = desliga */

#endif /* PERIPH_INIT_H */
