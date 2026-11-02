/**
 * Teste de host da meta diária e do limite de potência (goal.c).
 */
#include "goal.h"
#include "tap.h"

#define S_US     1000000ull
#define MIN_US   (60ull * S_US)
#define HOUR_US  (3600ull * S_US)
#define DAY_US   (24ull * HOUR_US)

static const goal_config_t CFG = {
    .daily_goal_mwh = 5000000u,     /* 5 kWh/dia */
    .warn_pct = 80u,
    .min_elapsed_us = HOUR_US,
    .power_limit_dw = 30000u,       /* 3000 W */
    .power_hold_us = 10u * S_US,
    .power_clear_pct = 95u,
};

static goal_state_t fresh(void)
{
    goal_state_t g;
    goal_init(&g, &CFG);
    return g;
}

static void test_attention_and_exceeded(void)
{
    goal_state_t g = fresh();
    /* Fim do dia (23 h): a projeção não passa da meta, só o percentual conta. */
    TAP_CHECK(goal_update_energy(&g, 3999999u, 23 * HOUR_US, DAY_US) == GOAL_OK,
              "3,999 kWh ainda é OK, veio %s", goal_status_name(g.status));
    TAP_CHECK(goal_update_energy(&g, 4000000u, 23 * HOUR_US, DAY_US) == GOAL_ATENCAO,
              "4 kWh (80%%) deveria ser ATENCAO, veio %s", goal_status_name(g.status));
    TAP_CHECK(g.used_pct == 80u, "uso = %u%% (esperado 80)", (unsigned)g.used_pct);
    TAP_CHECK(goal_update_energy(&g, 4999999u, 23 * HOUR_US, DAY_US) == GOAL_ATENCAO,
              "logo abaixo da meta continua ATENCAO, veio %s", goal_status_name(g.status));
    TAP_CHECK(goal_update_energy(&g, 5000000u, 23 * HOUR_US, DAY_US) == GOAL_ESTOURADA,
              "5 kWh (100%%) deveria ser ESTOURADA, veio %s", goal_status_name(g.status));
}

static void test_projection_warns_early(void)
{
    goal_state_t g = fresh();
    /* 1 kWh em 2 h = ritmo de 12 kWh/dia, só 20% da meta consumida. */
    TAP_CHECK(goal_update_energy(&g, 1000000u, 2 * HOUR_US, DAY_US) == GOAL_PROJECAO_ACIMA,
              "deveria projetar acima, veio %s", goal_status_name(g.status));
    TAP_CHECK(g.projected_mwh == 12000000u, "projeção = %lu mWh (esperado 12.000.000)",
              (unsigned long)g.projected_mwh);
    /* Ritmo de 4,8 kWh/dia: abaixo da meta. */
    TAP_CHECK(goal_update_energy(&g, 400000u, 2 * HOUR_US, DAY_US) == GOAL_OK,
              "ritmo abaixo da meta deveria ser OK, veio %s", goal_status_name(g.status));
    /* Exatamente 1 h já projeta. */
    TAP_CHECK(goal_update_energy(&g, 300000u, HOUR_US, DAY_US) == GOAL_PROJECAO_ACIMA,
              "com 1 h de dia a projeção vale, veio %s", goal_status_name(g.status));
}

static void test_projection_needs_one_hour(void)
{
    goal_state_t g = fresh();
    /* 1 kWh em 30 min projetaria 48 kWh, mas ainda não há dia suficiente. */
    TAP_CHECK(goal_update_energy(&g, 1000000u, 30 * MIN_US, DAY_US) == GOAL_OK,
              "com menos de 1 h não projeta, veio %s", goal_status_name(g.status));
    TAP_CHECK(g.projected_mwh == 0u, "projeção deveria ficar zerada");
}

static void feed_power(goal_state_t *g, uint64_t from_s, uint64_t to_s, uint32_t watts)
{
    for (uint64_t t = from_s; t <= to_s; ++t) goal_update_power(g, t * S_US, watts * 10u);
}

static void test_sustained_power_limit(void)
{
    goal_state_t g = fresh();
    feed_power(&g, 0, 9, 3200);                 /* acima do limite de t = 0 a 9 s */
    TAP_CHECK(!g.power_alert, "9 s acima do limite não deveria alertar");
    feed_power(&g, 10, 10, 3200);               /* completa 10 s */
    TAP_CHECK(g.power_alert, "10 s acima do limite deveria alertar");
}

static void test_short_peak_does_not_alert(void)
{
    goal_state_t g = fresh();
    feed_power(&g, 0, 9, 3500);                 /* pico de 9 s */
    feed_power(&g, 10, 12, 1000);               /* volta ao normal */
    feed_power(&g, 13, 22, 3500);               /* outro pico de 9 s: a contagem recomeça */
    TAP_CHECK(!g.power_alert, "picos de 9 s separados não deveriam alertar");
}

static void test_power_alert_hysteresis(void)
{
    goal_state_t g = fresh();
    feed_power(&g, 0, 10, 3200);
    TAP_CHECK(g.power_alert, "pré-condição: alerta ativo");
    feed_power(&g, 11, 15, 2900);               /* abaixo do limite, acima de 95% */
    TAP_CHECK(g.power_alert, "2900 W (acima de 2850 W) mantém o alerta");
    feed_power(&g, 16, 16, 2849);
    TAP_CHECK(!g.power_alert, "abaixo de 2850 W o alerta some");
}

int main(void)
{
    tap_run("AC-008 atenção em 80% e meta estourada em 100%", test_attention_and_exceeded);
    tap_run("AC-009 projeção acima da meta avisa com pouco consumo", test_projection_warns_early);
    tap_run("AC-009 sem projeção antes de 1 h de dia", test_projection_needs_one_hour);
    tap_run("AC-010 10 s acima de 3000 W disparam o alerta", test_sustained_power_limit);
    tap_run("AC-010 pico de 9 s não dispara", test_short_peak_does_not_alert);
    tap_run("AC-010 alerta só some abaixo de 2850 W", test_power_alert_hysteresis);
    return tap_done();
}
