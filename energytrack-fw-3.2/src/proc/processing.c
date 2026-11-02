#include "processing.h"

#include <string.h>

#include "energy_config.h"

#define US_PER_S              1000000ull
#define BLINK_SLOW_HALF_US    500000ull   /* 1 Hz */
#define BLINK_FAST_HALF_US    100000ull   /* 5 Hz */

void proc_default_config(proc_config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);

    cfg->energy.day_length_us = (uint64_t)CFG_DAY_LENGTH_S * US_PER_S;
    cfg->energy.max_gap_us = CFG_ENERGY_MAX_GAP_S * (uint32_t)US_PER_S;

    cfg->goal.daily_goal_mwh = CFG_DAILY_GOAL_MWH;
    cfg->goal.warn_pct = CFG_GOAL_WARN_PCT;
    cfg->goal.min_elapsed_us = (uint64_t)CFG_GOAL_MIN_ELAPSED_S * US_PER_S;
    cfg->goal.power_limit_dw = CFG_POWER_LIMIT_DW;
    cfg->goal.power_hold_us = CFG_POWER_HOLD_S * (uint32_t)US_PER_S;
    cfg->goal.power_clear_pct = CFG_POWER_CLEAR_PCT;

    cfg->anomaly.v_adequate_min_dv = CFG_V_ADEQUATE_MIN_DV;
    cfg->anomaly.v_adequate_max_dv = CFG_V_ADEQUATE_MAX_DV;
    cfg->anomaly.v_precarious_min_dv = CFG_V_PRECARIOUS_MIN_DV;
    cfg->anomaly.v_precarious_max_dv = CFG_V_PRECARIOUS_MAX_DV;
    cfg->anomaly.f_min_dhz = CFG_FREQ_MIN_DHZ;
    cfg->anomaly.f_max_dhz = CFG_FREQ_MAX_DHZ;
    cfg->anomaly.pf_min_centi = CFG_PF_MIN_CENTI;
    cfg->anomaly.pf_min_power_dw = CFG_PF_MIN_POWER_DW;
    cfg->anomaly.persist = CFG_ALERT_PERSIST;
    cfg->anomaly.sensor_fail_limit = CFG_SENSOR_FAIL_LIMIT;
    cfg->anomaly.temp_set_mc = CFG_TEMP_SET_MC;
    cfg->anomaly.temp_clear_mc = CFG_TEMP_CLEAR_MC;
    cfg->anomaly.spike_warmup = CFG_SPIKE_WARMUP;
    cfg->anomaly.spike_k = CFG_SPIKE_K;
    cfg->anomaly.spike_min_step_dw = CFG_SPIKE_MIN_STEP_DW;
}

void proc_init(proc_state_t *p, const proc_config_t *cfg, uint64_t now_us)
{
    memset(p, 0, sizeof *p);
    energy_init(&p->energy, &cfg->energy, now_us);
    goal_init(&p->goal, &cfg->goal);
    anomaly_init(&p->anomaly, &cfg->anomaly);
}

static void update_goal_energy(proc_state_t *p, uint64_t t_us)
{
    goal_update_energy(&p->goal, energy_day_mwh(&p->energy), energy_day_elapsed_us(&p->energy, t_us),
                       p->energy.cfg.day_length_us);
}

void proc_on_pzem(proc_state_t *p, uint64_t t_us, bool ok, const pzem_measurement_t *m)
{
    if (ok && m != NULL) {
        p->pzem_ok++;
        p->last = *m;
        p->has_reading = true;
        energy_on_sample(&p->energy, t_us, m->power_dw, m->energy_wh);
        anomaly_on_reading(&p->anomaly, m);
        goal_update_power(&p->goal, t_us, m->power_dw);
    } else {
        /* Falha não mexe na energia nem na última leitura: só conta e alimenta o alerta. */
        p->pzem_fail++;
        energy_tick(&p->energy, t_us);
        anomaly_on_read_failure(&p->anomaly);
    }
    update_goal_energy(p, t_us);
}

void proc_on_temperature(proc_state_t *p, uint64_t t_us, int32_t temp_mc)
{
    p->temp_mc = temp_mc;
    anomaly_on_temperature(&p->anomaly, temp_mc);
    energy_tick(&p->energy, t_us);
    update_goal_energy(p, t_us);
}

void proc_snapshot(const proc_state_t *p, uint64_t now_us, proc_snapshot_t *out)
{
    memset(out, 0, sizeof *out);
    out->last = p->last;
    out->has_reading = p->has_reading;
    out->pzem_ok = p->pzem_ok;
    out->pzem_fail = p->pzem_fail;
    out->temp_mc = p->temp_mc;

    out->day_index = p->energy.day_index;
    out->day_mwh = energy_day_mwh(&p->energy);
    out->prev_day_mwh = energy_prev_day_mwh(&p->energy);
    out->total_mwh = energy_total_mwh(&p->energy);
    out->day_elapsed_us = energy_day_elapsed_us(&p->energy, now_us);
    out->avg_power_dw = energy_day_avg_power_dw(&p->energy, now_us);
    out->gaps_recovered = p->energy.gaps_recovered;

    out->goal_status = p->goal.status;
    out->goal_used_pct = p->goal.used_pct;
    out->projected_mwh = p->goal.projected_mwh;
    out->power_alert = p->goal.power_alert;

    out->anomalies = anomaly_flags(&p->anomaly);
    out->v_severity = p->anomaly.v_severity;
    out->spikes = p->anomaly.spikes;
}

led_pattern_t proc_led_pattern(const proc_snapshot_t *s)
{
    const uint32_t others = s->anomalies & ~(uint32_t)ANOM_SENSOR_SEM_RESPOSTA;
    if (others != 0u || s->goal_status == GOAL_ESTOURADA || s->power_alert) return LED_BLINK_FAST;
    if ((s->anomalies & ANOM_SENSOR_SEM_RESPOSTA) || !s->has_reading) return LED_BLINK_SLOW;
    return LED_ON;
}

bool led_pattern_level(led_pattern_t pattern, uint64_t t_us)
{
    switch (pattern) {
    case LED_BLINK_SLOW: return ((t_us / BLINK_SLOW_HALF_US) & 1u) == 0u;
    case LED_BLINK_FAST: return ((t_us / BLINK_FAST_HALF_US) & 1u) == 0u;
    case LED_ON:
    default:             return true;
    }
}
