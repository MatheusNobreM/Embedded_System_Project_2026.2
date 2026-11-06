/**
 * @file acq_timer.h
 * @brief Timer de hardware que libera a aquisição periódica do PZEM.
 *
 * Usa um gptimer do ESP32 (grupo de timers TIMG) com contador de 1 MHz e
 * alarme auto-recarregado pelo hardware: taxa fixa, sem deriva. O callback
 * roda na ISR do timer — no núcleo de quem chamou acq_timer_start — com o
 * instante da liberação: a aplicação acorda a tarefa dona (fila) e usa o
 * instante para medir o tempo de resposta.
 */
#ifndef ACQ_TIMER_H
#define ACQ_TIMER_H

#include <stdbool.h>
#include <stdint.h>

/** Chamado na ISR. Retorna true se acordou tarefa de prioridade maior (o driver troca de contexto na saída). */
typedef bool (*acq_timer_cb)(uint64_t release_us);

/** Começa a chamar on_release a cada period_us. false = sem timer livre. */
bool acq_timer_start(uint32_t period_us, acq_timer_cb on_release);

#endif /* ACQ_TIMER_H */
