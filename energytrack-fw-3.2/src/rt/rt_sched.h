/**
 * @file rt_sched.h
 * @brief Análise de tempo de resposta (RTA) para prioridade fixa preemptiva.
 *
 * Para cada tarefa i, o pior tempo de resposta é o menor ponto fixo de
 *
 *   R_i = C_i + S_i + soma_{j em hp(i)} ceil((R_i + J_j) / T_j) * C_j
 *
 * C = execução no pior caso (CPU), S = suspensão própria máxima (ex.: dormir
 * esperando a UART), T = período ou intervalo mínimo entre liberações,
 * hp(i) = tarefas de prioridade maior ou igual. Uma tarefa de maior
 * prioridade que se suspende entra com jitter J_j = R_j - C_j (modelo de
 * suspensão como jitter, seguro); sem suspensão, J_j = 0.
 *
 * C puro: roda igual no host e na placa.
 */
#ifndef RT_SCHED_H
#define RT_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RT_SCHED_MAX_TASKS 16u

typedef struct {
    uint32_t c_us;      /* execução no pior caso (CPU) */
    uint32_t s_us;      /* suspensão própria máxima */
    uint32_t t_us;      /* período / intervalo mínimo */
    uint32_t d_us;      /* deadline relativo à liberação */
    uint8_t  priority;  /* maior = mais prioritária */
} rt_sched_task_t;

/**
 * Calcula o pior tempo de resposta de cada tarefa em response_us[i].
 * Retorna true se todas cabem no deadline. Uma tarefa que não cabe recebe
 * o primeiro valor acima do deadline encontrado na iteração.
 */
bool rt_sched_rta(const rt_sched_task_t *tasks, size_t n, uint32_t *response_us);

/** Utilização de CPU em partes por mil (soma de C/T). */
uint32_t rt_sched_utilization_permille(const rt_sched_task_t *tasks, size_t n);

#endif /* RT_SCHED_H */
