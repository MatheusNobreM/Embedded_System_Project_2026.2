/**
 * Teste de host do processamento integrado e do LED de estado (processing.c),
 * com a configuração padrão de energy_config.h.
 */
#include <string.h>
#include "processing.h"
#include "tap.h"

#define S_US     1000000ull
#define MS_US    1000ull

static proc_config_t s_cfg;
static proc_state_t s_p;
static proc_snapshot_t s_snap;

static pzem_measurement_t reading(uint32_t watts)
{
    pzem_measurement_t m;
    memset(&m, 0, sizeof m);
    m.voltage_dv = 2200;
    m.current_ma = watts * 1000u / 220u;
    m.power_dw = watts * 10u;
    m.freq_dhz = 600;
    m.pf_centi = 98;
    return m;
}

static void start(void)
{
    proc_default_config(&s_cfg);
    proc_init(&s_p, &s_cfg, 0);
}

static void snap(uint64_t t_us)
{
    proc_snapshot(&s_p, t_us, &s_snap);
}

static void feed_ok(uint64_t from_s, uint64_t to_s, pzem_measurement_t m)
{
    for (uint64_t t = from_s; t <= to_s; ++t) proc_on_pzem(&s_p, t * S_US, true, &m);
}

/* -------------------------------------------------------- configuração -- */
static void test_default_config_matches_spec(void)
{
    start();
    const anomaly_config_t *a = &s_cfg.anomaly;
    TAP_CHECK(a->v_adequate_min_dv == 2020 && a->v_adequate_max_dv == 2310, "faixa adequada 220 V (PRODIST)");
    TAP_CHECK(a->v_precarious_min_dv == 1910 && a->v_precarious_max_dv == 2330, "faixa precária 220 V (PRODIST)");
    TAP_CHECK(a->f_min_dhz == 595 && a->f_max_dhz == 605, "frequência 59,5–60,5 Hz");
    TAP_CHECK(a->pf_min_centi == 92 && a->pf_min_power_dw == 1000, "FP 0,92 com carga >= 100 W");
    TAP_CHECK(a->persist == 3 && a->sensor_fail_limit == 5, "persistência 3 e 5 falhas seguidas");
    TAP_CHECK(s_cfg.goal.daily_goal_mwh == 5000000u && s_cfg.goal.warn_pct == 80u, "meta 5 kWh/dia, aviso 80%%");
    TAP_CHECK(s_cfg.goal.power_limit_dw == 30000u && s_cfg.goal.power_hold_us == 10u * S_US, "limite 3000 W por 10 s");
    TAP_CHECK(s_cfg.energy.day_length_us == 86400ull * S_US, "dia de 24 h");
}

/* ------------------------------------------------- leitura com falha -- */
static void test_failed_reading_counted_and_harmless(void)
{
    start();
    feed_ok(0, 10, reading(1000));
    snap(10 * S_US);
    const uint32_t mwh_before = s_snap.day_mwh;
    const uint16_t v_before = s_snap.last.voltage_dv;

    proc_on_pzem(&s_p, 11 * S_US, false, NULL);
    snap(11 * S_US);
    TAP_CHECK(s_snap.pzem_fail == 1, "falhas = %u (esperado 1)", (unsigned)s_snap.pzem_fail);
    TAP_CHECK(s_snap.day_mwh == mwh_before, "falha mudou a energia: %u -> %u mWh",
              (unsigned)mwh_before, (unsigned)s_snap.day_mwh);
    TAP_CHECK(s_snap.last.voltage_dv == v_before && s_snap.has_reading, "última leitura válida deveria ficar");
    TAP_CHECK(s_snap.anomalies == 0, "uma falha isolada não é anomalia");

    pzem_measurement_t m = reading(1000);
    proc_on_pzem(&s_p, 12 * S_US, true, &m);
    snap(12 * S_US);
    TAP_CHECK(s_snap.pzem_ok == 12, "leituras válidas = %u (esperado 12)", (unsigned)s_snap.pzem_ok);
    /* O trapézio atravessa a leitura perdida: 12 s a 1000 W = 3333 mWh, como se nada faltasse. */
    TAP_CHECK(s_snap.day_mwh == 3333u, "energia depois da falha = %u mWh (esperado 3333)",
              (unsigned)s_snap.day_mwh);
}

/* -------------------------------------------------------------- LED -- */
static led_pattern_t pattern_now(uint64_t t_us)
{
    snap(t_us);
    return proc_led_pattern(&s_snap);
}

static void test_led_states(void)
{
    start();
    TAP_CHECK(pattern_now(0) == LED_BLINK_SLOW, "sem nenhuma leitura ainda: pisca devagar");

    feed_ok(0, 5, reading(500));
    TAP_CHECK(pattern_now(5 * S_US) == LED_ON, "tudo normal: aceso fixo");

    for (uint64_t t = 6; t <= 10; ++t) proc_on_pzem(&s_p, t * S_US, false, NULL);
    TAP_CHECK(pattern_now(10 * S_US) == LED_BLINK_SLOW, "sensor sem resposta: pisca a 1 Hz");

    start();
    pzem_measurement_t sag = reading(500);
    sag.voltage_dv = 1950;                       /* subtensão ativa ... */
    feed_ok(0, 2, sag);
    for (uint64_t t = 3; t <= 7; ++t) proc_on_pzem(&s_p, t * S_US, false, NULL);   /* ... e depois o sensor cai */
    TAP_CHECK(pattern_now(7 * S_US) == LED_BLINK_FAST, "outra anomalia junto com sensor fora: pisca a 5 Hz");
    TAP_CHECK((s_snap.anomalies & ANOM_SENSOR_SEM_RESPOSTA) && (s_snap.anomalies & ANOM_SUBTENSAO),
              "sensor sem resposta e subtensão juntos");

    start();
    pzem_measurement_t low = reading(500);
    low.voltage_dv = 1950;
    feed_ok(0, 2, low);
    TAP_CHECK(pattern_now(2 * S_US) == LED_BLINK_FAST, "subtensão: pisca a 5 Hz");

    start();
    feed_ok(0, 10, reading(3500));
    TAP_CHECK(pattern_now(10 * S_US) == LED_BLINK_FAST, "limite de potência: pisca a 5 Hz");
    TAP_CHECK(s_snap.power_alert && s_snap.anomalies == 0, "pisca pelo limite, não por anomalia");
}

static void test_led_goal_states(void)
{
    /* Meta pequena para chegar a ATENCAO e ESTOURADA em poucos segundos. */
    proc_default_config(&s_cfg);
    s_cfg.goal.daily_goal_mwh = 2000u;                /* 2 Wh */
    proc_init(&s_p, &s_cfg, 0);
    feed_ok(0, 6, reading(1000));                     /* 6 s a 1000 W = 1,67 Wh = 83% */
    TAP_CHECK(s_p.goal.status == GOAL_ATENCAO, "pré-condição: ATENCAO, veio %s", goal_status_name(s_p.goal.status));
    TAP_CHECK(pattern_now(6 * S_US) == LED_ON, "ATENCAO sozinha não pisca o LED");
    feed_ok(7, 8, reading(1000));                     /* 8 s = 2,22 Wh */
    TAP_CHECK(s_p.goal.status == GOAL_ESTOURADA, "pré-condição: ESTOURADA, veio %s", goal_status_name(s_p.goal.status));
    TAP_CHECK(pattern_now(8 * S_US) == LED_BLINK_FAST, "meta estourada: pisca a 5 Hz");
}

static void test_led_blink_rates(void)
{
    TAP_CHECK(led_pattern_level(LED_ON, 0) && led_pattern_level(LED_ON, 123456789u), "aceso fixo");
    /* 1 Hz: 500 ms aceso, 500 ms apagado. */
    TAP_CHECK(led_pattern_level(LED_BLINK_SLOW, 0) && led_pattern_level(LED_BLINK_SLOW, 499 * MS_US) &&
              !led_pattern_level(LED_BLINK_SLOW, 500 * MS_US) && !led_pattern_level(LED_BLINK_SLOW, 999 * MS_US) &&
              led_pattern_level(LED_BLINK_SLOW, 1000 * MS_US), "pisca devagar a 1 Hz");
    /* 5 Hz: 100 ms aceso, 100 ms apagado. */
    TAP_CHECK(led_pattern_level(LED_BLINK_FAST, 0) && !led_pattern_level(LED_BLINK_FAST, 100 * MS_US) &&
              led_pattern_level(LED_BLINK_FAST, 200 * MS_US) && !led_pattern_level(LED_BLINK_FAST, 900 * MS_US),
              "pisca rápido a 5 Hz");
}

int main(void)
{
    tap_run("configuração padrão segue a especificação", test_default_config_matches_spec);
    tap_run("AC-001 leitura com falha é contada e não mexe na energia nem na última leitura",
            test_failed_reading_counted_and_harmless);
    tap_run("AC-015 LED: normal, sensor sem resposta, anomalia e limite", test_led_states);
    tap_run("AC-015 LED: meta em atenção não pisca, estourada pisca rápido", test_led_goal_states);
    tap_run("AC-015 LED: 1 Hz e 5 Hz", test_led_blink_rates);
    return tap_done();
}
