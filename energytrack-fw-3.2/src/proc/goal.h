/**
 * @file goal.h
 * @brief Meta diária de energia (com projeção) e limite de potência sustentada.
 *
 * Estado da meta, do mais grave para o menos grave:
 *   ESTOURADA       consumo do dia >= meta
 *   ATENCAO         consumo do dia >= warn_pct da meta
 *   PROJECAO_ACIMA  ritmo médio do dia projeta passar da meta (só depois de
 *                   min_elapsed_us de dia, para não alarmar com pouca amostra)
 *   OK
 *
 * Limite de potência: alerta quando a potência fica acima do limite por
 * hold_us seguidos; só some abaixo de clear_pct do limite (histerese).
 *
 * C puro: roda igual no host e na placa.
 */
#ifndef GOAL_H
#define GOAL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GOAL_OK = 0,
    GOAL_PROJECAO_ACIMA,
    GOAL_ATENCAO,
    GOAL_ESTOURADA,
} goal_status_t;

typedef struct {
    uint32_t daily_goal_mwh;        /* 0 = meta desligada */
    uint32_t warn_pct;              /* aviso de atenção (80) */
    uint64_t min_elapsed_us;        /* tempo mínimo de dia para projetar (1 h) */
    uint32_t power_limit_dw;        /* 0 = limite desligado */
    uint32_t power_hold_us;         /* tempo acima do limite para alertar (10 s) */
    uint32_t power_clear_pct;       /* alerta some abaixo deste % do limite (95) */
} goal_config_t;

typedef struct {
    goal_config_t cfg;
    goal_status_t status;
    uint32_t used_pct;              /* consumo do dia em % da meta */
    uint64_t projected_mwh;         /* 0 enquanto não há dia suficiente */

    bool     over_pending;          /* acima do limite, ainda sem completar hold_us */
    uint64_t over_since_us;
    bool     power_alert;
} goal_state_t;

void goal_init(goal_state_t *g, const goal_config_t *cfg);

/** Reavalia a meta com o consumo do dia e o tempo decorrido nele. */
goal_status_t goal_update_energy(goal_state_t *g, uint32_t day_mwh, uint64_t day_elapsed_us,
                                 uint64_t day_length_us);

/** Avalia o limite com uma leitura válida de potência. Retorna se o alerta está ativo. */
bool goal_update_power(goal_state_t *g, uint64_t t_us, uint32_t power_dw);

const char *goal_status_name(goal_status_t status);

#endif /* GOAL_H */
