/**
 * @file adc_shared.h
 * @brief Acesso serializado ao único conversor ADC do RP2040.
 *
 * Joystick (ADC0/ADC1) e microfone (ADC2) dividem o mesmo conversor: trocar
 * o canal com adc_select_input() no meio da leitura de outra tarefa devolve
 * a amostra do canal errado. Toda leitura passa por aqui, sob mutex.
 */
#ifndef ADC_SHARED_H
#define ADC_SHARED_H

#include <stdbool.h>
#include <stdint.h>

/** Cria o mutex. Chamar depois de periph_init_adc() e antes das tarefas usarem o ADC. */
bool adc_shared_init(void);

/** Lê um canal (0-4). Retorna false se não conseguir o ADC em timeout_ms. */
bool adc_shared_read(uint8_t channel, uint32_t timeout_ms, uint16_t *out);

#endif /* ADC_SHARED_H */
