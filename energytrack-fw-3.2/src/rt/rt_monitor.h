/**
 * @file rt_monitor.h
 * @brief Monitor de deadline por tarefa: tempo de resposta e estouros.
 *
 * Cada job tem um instante de liberação (timer, IRQ do DMA ou chegada da
 * amostra) e um instante de término. Tempo de resposta = término - liberação;
 * acima do deadline é estouro (miss). Uma liberação que chega com o job
 * anterior ainda pendente também é estouro (liberação perdida).
 *
 * Um escritor por monitor (a tarefa dona); a report só lê campos de 32 bits.
 * C puro: roda igual no host e na placa.
 */
#ifndef RT_MONITOR_H
#define RT_MONITOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t          deadline_us;
    volatile uint32_t jobs;              /* jobs concluídos */
    volatile uint32_t misses;            /* jobs atrasados + liberações perdidas */
    volatile uint32_t max_response_us;   /* maior tempo de resposta observado */
} rt_monitor_t;

void rt_monitor_init(rt_monitor_t *m, uint32_t deadline_us);

/** Job liberado em release_us terminou em done_us. Retorna true se cumpriu o deadline. */
bool rt_monitor_job_done(rt_monitor_t *m, uint64_t release_us, uint64_t done_us);

/** n liberações chegaram com o job anterior ainda pendente. */
void rt_monitor_releases_lost(rt_monitor_t *m, uint32_t n);

/**
 * Formata "nome:jobs=N,miss=M,rmax=Xus,d=Dus" — o formato que o teste de
 * bancada lê do log serial. Retorna o tamanho escrito (como snprintf).
 */
int rt_monitor_format(char *buf, size_t size, const char *name, const rt_monitor_t *m);

#endif /* RT_MONITOR_H */
