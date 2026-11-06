/**
 * @file rt_tasks.h
 * @brief Tabela de tarefas do firmware: prioridade, período, deadline e orçamento.
 *
 * Fonte única: o firmware cria as tarefas com estes valores, a análise de
 * tempo de resposta usa os mesmos números e o teste confere o diagrama
 * (docs/diagrama-tarefas.md) contra esta tabela.
 *
 * Prioridades: as tarefas do critério de aceite (hard) ficam acima das
 * "soft"; entre as hard, deadline monotônico (deadline menor = prioridade maior).
 */
#ifndef RT_TASKS_H
#define RT_TASKS_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RT_TASK_PROC = 0,
    RT_TASK_ADC,
    RT_TASK_PZEM,
    RT_TASK_STATUS,
    RT_TASK_REPORT,
    RT_TASK_COUNT
} rt_task_id_t;

typedef struct {
    const char *name;
    uint8_t     priority;     /* FreeRTOS: maior = mais prioritária */
    uint32_t    period_us;    /* período ou intervalo mínimo entre liberações */
    uint32_t    deadline_us;  /* relativo à liberação */
    uint32_t    budget_us;    /* orçamento de execução de CPU no pior caso (usado na análise) */
    uint32_t    suspend_us;   /* suspensão própria máxima, espera de E/S (usado na análise) */
    bool        hard;         /* entra no critério de aceite de tempo real */
} rt_task_param_t;

extern const rt_task_param_t RT_TASKS[RT_TASK_COUNT];

/* Interrupções + kernel do núcleo 1 (tick, UARTs, DMA do ADC, gptimer) como tarefa de prioridade máxima. */
extern const rt_task_param_t RT_ISR_LOAD;

#endif /* RT_TASKS_H */
