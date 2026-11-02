#include "energy.h"

#include <string.h>

/*
 * Unidade interna: (P1 + P2) * dt em dW·µs, ou seja, o DOBRO da energia do
 * trapézio — evita perder o 0,5 da média a cada leitura.
 *   1 mWh = 3,6 W·s = 36e6 dW·µs  ->  72e6 unidades.
 * uint64 comporta ~256 MWh: sobra para o total desde o boot.
 */
#define UNITS_PER_MWH   72000000ull
#define MWH_PER_WH      1000u

static uint64_t day_end_us(const energy_state_t *s)
{
    return s->day_start_us + s->cfg.day_length_us;
}

static void close_day(energy_state_t *s)
{
    s->prev_day_mwh = (uint32_t)(s->day_units / UNITS_PER_MWH);
    s->day_units = 0;
    s->day_start_us = day_end_us(s);
    s->day_index++;
}

/* amount * part / whole sem estourar 64 bits (sem divisão quando cabe inteiro). */
static uint64_t proportion(uint64_t amount, uint64_t part, uint64_t whole)
{
    if (whole == 0u || part >= whole) return amount;
    if (part != 0u && amount > UINT64_MAX / part) {
        return (amount / whole) * part;   /* perde só a fração: só em lacunas absurdas */
    }
    return (amount * part) / whole;
}

/*
 * Soma 'amount' (cada unidade vale 'scale' unidades internas) espalhado
 * uniformemente entre t0 e t1, fechando os dias que terminarem no meio.
 * O trecho anterior ao dia corrente (já fechado por energy_tick) vai para
 * o total do dia anterior.
 */
static void add_spread(energy_state_t *s, uint64_t amount, uint64_t scale, uint64_t t0, uint64_t t1)
{
    if (t1 <= t0) {
        s->day_units += amount * scale;
        s->total_units += amount * scale;
        return;
    }
    if (t0 < s->day_start_us) {
        const uint64_t part = proportion(amount, s->day_start_us - t0, t1 - t0);
        s->prev_day_mwh += (uint32_t)((part * scale) / UNITS_PER_MWH);
        s->total_units += part * scale;
        amount -= part;
        t0 = s->day_start_us;
    }
    while (t1 >= day_end_us(s)) {
        const uint64_t end = day_end_us(s);
        const uint64_t part = proportion(amount, end - t0, t1 - t0);
        s->day_units += part * scale;
        s->total_units += part * scale;
        amount -= part;
        t0 = end;
        close_day(s);
    }
    s->day_units += amount * scale;
    s->total_units += amount * scale;
}

void energy_init(energy_state_t *s, const energy_config_t *cfg, uint64_t now_us)
{
    memset(s, 0, sizeof *s);
    s->cfg = *cfg;
    s->day_start_us = now_us;
}

void energy_tick(energy_state_t *s, uint64_t now_us)
{
    while (now_us >= day_end_us(s)) close_day(s);
}

void energy_on_sample(energy_state_t *s, uint64_t t_us, uint32_t power_dw, uint32_t counter_wh)
{
    if (s->has_prev && t_us > s->prev_t_us) {
        const uint64_t dt = t_us - s->prev_t_us;
        if (dt <= s->cfg.max_gap_us) {
            const uint64_t units = ((uint64_t)s->prev_power_dw + power_dw) * dt;
            add_spread(s, units, 1u, s->prev_t_us, t_us);
        } else if (counter_wh >= s->prev_counter_wh) {
            const uint64_t mwh = (uint64_t)(counter_wh - s->prev_counter_wh) * MWH_PER_WH;
            add_spread(s, mwh, UNITS_PER_MWH, s->prev_t_us, t_us);
            s->gaps_recovered++;
        }
        /* Contador que voltou para trás (zerado no PZEM): o intervalo fica sem energia. */
    }
    energy_tick(s, t_us);

    s->has_prev = true;
    s->prev_t_us = t_us;
    s->prev_power_dw = power_dw;
    s->prev_counter_wh = counter_wh;
}

uint32_t energy_day_mwh(const energy_state_t *s)
{
    return (uint32_t)(s->day_units / UNITS_PER_MWH);
}

uint64_t energy_total_mwh(const energy_state_t *s)
{
    return s->total_units / UNITS_PER_MWH;
}

uint32_t energy_prev_day_mwh(const energy_state_t *s)
{
    return s->prev_day_mwh;
}

uint64_t energy_day_elapsed_us(const energy_state_t *s, uint64_t now_us)
{
    return now_us > s->day_start_us ? now_us - s->day_start_us : 0u;
}

uint32_t energy_day_avg_power_dw(const energy_state_t *s, uint64_t now_us)
{
    const uint64_t elapsed = energy_day_elapsed_us(s, now_us);
    if (elapsed == 0u) return 0u;
    return (uint32_t)(s->day_units / (2u * elapsed));
}
