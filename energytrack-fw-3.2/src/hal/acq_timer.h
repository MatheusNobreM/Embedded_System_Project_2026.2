/**
 * @file acq_timer.h
 * @brief Timer de hardware que libera a aquisição periódica do PZEM.
 *
 * Usa um alarme do timer do RP2040 em taxa fixa. O callback roda em contexto
 * de interrupção, com o instante da liberação: a aplicação acorda a tarefa
 * dona (notificação) e usa o instante para medir o tempo de resposta.
 */
#ifndef ACQ_TIMER_H
#define ACQ_TIMER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*acq_timer_cb)(uint64_t release_us);

/** Começa a chamar on_release a cada period_us. false = sem alarme livre. */
bool acq_timer_start(uint32_t period_us, acq_timer_cb on_release);

#endif /* ACQ_TIMER_H */
