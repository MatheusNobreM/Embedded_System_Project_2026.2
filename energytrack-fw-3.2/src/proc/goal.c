#include "goal.h"

#include <string.h>

#define US_PER_S 1000000u

void goal_init(goal_state_t *g, const goal_config_t *cfg)
{
    memset(g, 0, sizeof *g);
    g->cfg = *cfg;
}

goal_status_t goal_update_energy(goal_state_t *g, uint32_t day_mwh, uint64_t day_elapsed_us,
                                 uint64_t day_length_us)
{
    const uint64_t goal = g->cfg.daily_goal_mwh;
    if (goal == 0u) {
        g->status = GOAL_OK;
        return g->status;
    }

    g->used_pct = (uint32_t)(((uint64_t)day_mwh * 100u) / goal);

    /* Projeção pelo ritmo médio do dia, em segundos para não estourar 64 bits. */
    g->projected_mwh = 0;
    const uint64_t elapsed_s = day_elapsed_us / US_PER_S;
    if (day_elapsed_us >= g->cfg.min_elapsed_us && elapsed_s > 0u) {
        g->projected_mwh = ((uint64_t)day_mwh * (day_length_us / US_PER_S)) / elapsed_s;
    }

    if (day_mwh >= goal) {
        g->status = GOAL_ESTOURADA;
    } else if ((uint64_t)day_mwh * 100u >= goal * g->cfg.warn_pct) {
        g->status = GOAL_ATENCAO;
    } else if (g->projected_mwh > goal) {
        g->status = GOAL_PROJECAO_ACIMA;
    } else {
        g->status = GOAL_OK;
    }
    return g->status;
}

bool goal_update_power(goal_state_t *g, uint64_t t_us, uint32_t power_dw)
{
    const uint32_t limit = g->cfg.power_limit_dw;
    if (limit == 0u) return false;

    if (power_dw > limit) {
        if (!g->over_pending) {
            g->over_pending = true;
            g->over_since_us = t_us;
        }
        if ((t_us - g->over_since_us) >= g->cfg.power_hold_us) {
            g->power_alert = true;
        }
    } else {
        g->over_pending = false;   /* a sequência acima do limite foi interrompida */
        if ((uint64_t)power_dw * 100u < (uint64_t)limit * g->cfg.power_clear_pct) {
            g->power_alert = false;
        }
    }
    return g->power_alert;
}

const char *goal_status_name(goal_status_t status)
{
    switch (status) {
    case GOAL_OK:             return "OK";
    case GOAL_PROJECAO_ACIMA: return "PROJECAO_ACIMA";
    case GOAL_ATENCAO:        return "ATENCAO";
    case GOAL_ESTOURADA:      return "ESTOURADA";
    }
    return "?";
}
