#include "anomaly.h"

#include <string.h>

#define EWMA_SHIFT 4   /* alfa = 1/16: linha de base de ~16 leituras */

static void debounce(anomaly_debounce_t *d, bool condition, uint8_t persist)
{
    if (condition) {
        d->good = 0;
        if (d->bad < UINT8_MAX) d->bad++;
        if (d->bad >= persist) d->active = true;
    } else {
        d->bad = 0;
        if (d->good < UINT8_MAX) d->good++;
        if (d->good >= persist) d->active = false;
    }
}

static void debounce_reset(anomaly_debounce_t *d)
{
    memset(d, 0, sizeof *d);
}

static voltage_band_t classify_voltage(const anomaly_config_t *c, uint16_t v_dv)
{
    if (v_dv >= c->v_adequate_min_dv && v_dv <= c->v_adequate_max_dv) return VBAND_ADEQUADA;
    if (v_dv >= c->v_precarious_min_dv && v_dv <= c->v_precarious_max_dv) return VBAND_PRECARIA;
    return VBAND_CRITICA;
}

static void update_voltage(anomaly_state_t *a, uint16_t v_dv)
{
    const anomaly_config_t *c = &a->cfg;
    const voltage_band_t band = classify_voltage(c, v_dv);
    debounce(&a->under_v, v_dv < c->v_adequate_min_dv, c->persist);
    debounce(&a->over_v, v_dv > c->v_adequate_max_dv, c->persist);

    if (!a->under_v.active && !a->over_v.active) {
        a->v_severity = VBAND_ADEQUADA;
    } else if (band != VBAND_ADEQUADA) {
        a->v_severity = band;   /* mantém a última gravidade enquanto o alerta não some */
    }
}

static int64_t abs64(int64_t x)
{
    return x < 0 ? -x : x;
}

static void update_spike(anomaly_state_t *a, uint32_t power_dw)
{
    const anomaly_config_t *c = &a->cfg;
    const int64_t p_x16 = (int64_t)power_dw << EWMA_SHIFT;

    a->spike_now = false;
    if (a->baseline_n == 0u) {
        a->mean_x16 = p_x16;
        a->dev_x16 = 0;
    } else if (a->baseline_n >= c->spike_warmup) {
        const int64_t step_x16 = p_x16 - a->mean_x16;
        int64_t threshold_x16 = (int64_t)c->spike_k * a->dev_x16;
        const int64_t min_x16 = (int64_t)c->spike_min_step_dw << EWMA_SHIFT;
        if (threshold_x16 < min_x16) threshold_x16 = min_x16;
        if (step_x16 > threshold_x16) {
            if (a->spike_armed) {
                a->spike_now = true;
                a->spikes++;
                a->spike_armed = false;
            }
        } else {
            a->spike_armed = true;
        }
    }
    /* A linha de base acompanha também o salto: um novo patamar deixa de ser atípico. */
    const int64_t err = p_x16 - a->mean_x16;
    a->mean_x16 += err / (1 << EWMA_SHIFT);
    a->dev_x16 += (abs64(err) - a->dev_x16) / (1 << EWMA_SHIFT);
    if (a->baseline_n < UINT32_MAX) a->baseline_n++;
}

void anomaly_init(anomaly_state_t *a, const anomaly_config_t *cfg)
{
    memset(a, 0, sizeof *a);
    a->cfg = *cfg;
    a->spike_armed = true;
}

void anomaly_on_reading(anomaly_state_t *a, const pzem_measurement_t *m)
{
    const anomaly_config_t *c = &a->cfg;

    a->consecutive_fails = 0;
    a->sensor_offline = false;

    update_voltage(a, m->voltage_dv);
    debounce(&a->freq, m->freq_dhz < c->f_min_dhz || m->freq_dhz > c->f_max_dhz, c->persist);

    /* FP sem carga relevante não diz nada: abaixo do mínimo, não avalia. */
    if (m->power_dw >= c->pf_min_power_dw) {
        debounce(&a->pf, m->pf_centi < c->pf_min_centi, c->persist);
    } else {
        debounce_reset(&a->pf);
    }

    update_spike(a, m->power_dw);
}

void anomaly_on_read_failure(anomaly_state_t *a)
{
    if (a->consecutive_fails < UINT32_MAX) a->consecutive_fails++;
    if (a->consecutive_fails >= a->cfg.sensor_fail_limit) a->sensor_offline = true;
}

void anomaly_on_temperature(anomaly_state_t *a, int32_t temp_mc)
{
    const anomaly_config_t *c = &a->cfg;
    if (a->hot.active) {
        debounce(&a->hot, temp_mc >= c->temp_clear_mc, c->persist);   /* só desliga abaixo de clear */
    } else {
        debounce(&a->hot, temp_mc > c->temp_set_mc, c->persist);
    }
}

uint32_t anomaly_flags(const anomaly_state_t *a)
{
    uint32_t f = 0;
    if (a->under_v.active) f |= ANOM_SUBTENSAO;
    if (a->over_v.active)  f |= ANOM_SOBRETENSAO;
    if (a->freq.active)    f |= ANOM_FREQUENCIA;
    if (a->pf.active)      f |= ANOM_FP_BAIXO;
    if (a->spike_now)      f |= ANOM_SALTO_POTENCIA;
    if (a->sensor_offline) f |= ANOM_SENSOR_SEM_RESPOSTA;
    if (a->hot.active)     f |= ANOM_SUPERAQUECIMENTO;
    return f;
}

const char *anomaly_name(anomaly_flag_t flag)
{
    switch (flag) {
    case ANOM_SUBTENSAO:           return "SUBTENSAO";
    case ANOM_SOBRETENSAO:         return "SOBRETENSAO";
    case ANOM_FREQUENCIA:          return "FREQUENCIA";
    case ANOM_FP_BAIXO:            return "FP_BAIXO";
    case ANOM_SALTO_POTENCIA:      return "SALTO_POTENCIA";
    case ANOM_SENSOR_SEM_RESPOSTA: return "SENSOR_SEM_RESPOSTA";
    case ANOM_SUPERAQUECIMENTO:    return "SUPERAQUECIMENTO";
    }
    return "?";
}

const char *voltage_band_name(voltage_band_t band)
{
    switch (band) {
    case VBAND_ADEQUADA: return "adequada";
    case VBAND_PRECARIA: return "precaria";
    case VBAND_CRITICA:  return "critica";
    }
    return "?";
}
