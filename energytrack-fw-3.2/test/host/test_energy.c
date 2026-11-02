/**
 * Teste de host de potência e energia (energy.c).
 */
#include <stdlib.h>
#include "energy.h"
#include "tap.h"

#define S_US            1000000ull
#define HOUR_US         (3600ull * S_US)
#define DAY_US          (24ull * HOUR_US)

static const energy_config_t CFG = { .day_length_us = DAY_US, .max_gap_us = 3u * S_US };

/* Contador do PZEM (Wh, truncado) para uma carga constante desde t = 0. */
static uint32_t counter_at(uint64_t t_us, uint32_t power_w, uint32_t base_wh)
{
    return base_wh + (uint32_t)(((uint64_t)power_w * t_us) / HOUR_US);
}

static void feed_constant(energy_state_t *s, uint64_t from_us, uint64_t to_us, uint32_t power_w, uint32_t base_wh)
{
    for (uint64_t t = from_us; t <= to_us; t += S_US)
        energy_on_sample(s, t, power_w * 10u, counter_at(t, power_w, base_wh));
}

static int within_pct(uint64_t got, uint64_t want, double pct)
{
    double diff = (double)got - (double)want;
    if (diff < 0) diff = -diff;
    return diff <= (double)want * pct / 100.0;
}

static void test_one_hour_at_1000w(void)
{
    energy_state_t s;
    energy_init(&s, &CFG, 0);
    feed_constant(&s, 0, HOUR_US, 1000, 0);
    uint32_t mwh = energy_day_mwh(&s);
    TAP_CHECK(within_pct(mwh, 1000000u, 0.1), "1 h a 1000 W -> %u mWh (esperado 1.000.000 ± 0,1%%)", (unsigned)mwh);
    uint32_t avg = energy_day_avg_power_dw(&s, HOUR_US);
    TAP_CHECK(within_pct(avg, 10000u, 0.1), "potência média %u dW (esperado 10000)", (unsigned)avg);
    TAP_CHECK(energy_total_mwh(&s) == mwh, "total desde o boot deveria igualar o dia");
}

static void test_ramp_with_jitter(void)
{
    /* Rampa de 0 a 2000 W em 1 h com leituras a 1 s ± 40 ms: integral exata = 1000 Wh. */
    energy_state_t s;
    energy_init(&s, &CFG, 0);
    srand(42);
    for (uint64_t k = 0; k <= 3600; ++k) {
        int64_t jitter = (k == 0 || k == 3600) ? 0 : (rand() % 81) - 40;
        uint64_t t = k * S_US + (uint64_t)(jitter * 1000);
        uint32_t p_dw = (uint32_t)((20000ull * t) / HOUR_US);
        energy_on_sample(&s, t, p_dw, 0);
    }
    uint32_t mwh = energy_day_mwh(&s);
    TAP_CHECK(within_pct(mwh, 1000000u, 0.1), "rampa -> %u mWh (esperado 1.000.000 ± 0,1%%)", (unsigned)mwh);
}

static void test_gap_recovered_by_counter(void)
{
    energy_state_t s;
    energy_init(&s, &CFG, 0);
    feed_constant(&s, 0, 10 * S_US, 1000, 100);          /* 10 s de leituras */
    const uint64_t back = 70 * S_US;                       /* 60 s sem comunicação */
    energy_on_sample(&s, back, 10000, counter_at(back, 1000, 100));

    /* Verdade: 70 s a 1000 W = 19.444 mWh. */
    uint32_t mwh = energy_day_mwh(&s);
    TAP_CHECK(abs((int)mwh - 19444) <= 1000, "energia com lacuna = %u mWh (esperado 19.444 ± 1 Wh)", (unsigned)mwh);
    TAP_CHECK(s.gaps_recovered == 1, "intervalos recuperados = %u (esperado 1)", (unsigned)s.gaps_recovered);
}

static void test_counter_reset_is_not_negative(void)
{
    energy_state_t s;
    energy_init(&s, &CFG, 0);
    feed_constant(&s, 0, 10 * S_US, 1000, 500);
    uint32_t before = energy_day_mwh(&s);
    energy_on_sample(&s, 30 * S_US, 10000, 0);             /* lacuna + contador zerado */
    uint32_t after = energy_day_mwh(&s);
    TAP_CHECK(after == before, "contador zerado mudou a energia: %u -> %u mWh", (unsigned)before, (unsigned)after);
    TAP_CHECK(s.gaps_recovered == 0, "lacuna com contador zerado não conta como recuperada");
    /* A partir daí volta a integrar normalmente. */
    energy_on_sample(&s, 31 * S_US, 10000, 0);
    TAP_CHECK(energy_day_mwh(&s) > after, "leituras seguintes voltam a somar energia");
}

static void test_failed_reading_short_gap_interpolates(void)
{
    /* Uma leitura perdida (2 s entre válidas) ainda é trapézio, sem contador. */
    energy_state_t s;
    energy_init(&s, &CFG, 0);
    energy_on_sample(&s, 0, 10000, 0);
    energy_on_sample(&s, 2 * S_US, 10000, 0);
    TAP_CHECK(energy_day_mwh(&s) == 555, "2 s a 1000 W = 555 mWh, veio %u", (unsigned)energy_day_mwh(&s));
    TAP_CHECK(s.gaps_recovered == 0, "2 s não é lacuna");
}

static void test_day_rollover(void)
{
    energy_state_t s;
    energy_init(&s, &CFG, 0);
    feed_constant(&s, 0, DAY_US + 10 * S_US, 500, 0);

    TAP_CHECK(s.day_index == 1, "deveria estar no 2º dia, está no índice %u", (unsigned)s.day_index);
    /* 500 W por 24 h = 12 kWh no dia anterior. */
    TAP_CHECK(within_pct(energy_prev_day_mwh(&s), 12000000u, 0.1), "dia anterior = %u mWh (esperado 12.000.000)",
              (unsigned)energy_prev_day_mwh(&s));
    /* 10 s a 500 W = 1.389 mWh no dia novo. */
    TAP_CHECK(abs((int)energy_day_mwh(&s) - 1389) <= 2, "dia novo = %u mWh (esperado ~1389)", (unsigned)energy_day_mwh(&s));
    uint64_t total = energy_total_mwh(&s);
    TAP_CHECK(within_pct(total, 12001389u, 0.1), "total desde o boot = %lu mWh (esperado ~12.001.389)", (unsigned long)total);
    TAP_CHECK(energy_day_elapsed_us(&s, DAY_US + 10 * S_US) == 10 * S_US, "dia novo começa exatamente em 24 h");
}

static void test_day_rolls_without_readings(void)
{
    energy_state_t s;
    energy_init(&s, &CFG, 0);
    feed_constant(&s, 0, HOUR_US, 1000, 0);               /* 1 kWh na primeira hora */
    energy_tick(&s, DAY_US + S_US);                         /* PZEM fora do ar até depois da virada */
    TAP_CHECK(s.day_index == 1, "o dia deveria virar só com o relógio");
    TAP_CHECK(energy_day_mwh(&s) == 0, "dia novo começa zerado");
    TAP_CHECK(within_pct(energy_prev_day_mwh(&s), 1000000u, 0.1), "dia anterior guarda 1 kWh (%u mWh)",
              (unsigned)energy_prev_day_mwh(&s));
}

int main(void)
{
    tap_run("AC-005 1 h a 1000 W dá 1000 Wh e média de 1000 W", test_one_hour_at_1000w);
    tap_run("AC-005 rampa com jitter nas leituras integra com erro < 0,1%", test_ramp_with_jitter);
    tap_run("AC-006 60 s sem comunicação recuperados pelo contador do PZEM", test_gap_recovered_by_counter);
    tap_run("AC-006 contador zerado não gera energia negativa", test_counter_reset_is_not_negative);
    tap_run("uma leitura perdida ainda é interpolada pelo trapézio", test_failed_reading_short_gap_interpolates);
    tap_run("AC-007 virada do dia em 24 h guarda o dia anterior e mantém o total", test_day_rollover);
    tap_run("AC-007 o dia vira mesmo sem leituras", test_day_rolls_without_readings);
    return tap_done();
}
