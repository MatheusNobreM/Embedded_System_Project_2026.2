/**
 * Teste de host da detecção de anomalias (anomaly.c).
 */
#include <string.h>
#include "anomaly.h"
#include "tap.h"

static const anomaly_config_t CFG = {
    .v_adequate_min_dv = 2020, .v_adequate_max_dv = 2310,      /* PRODIST 220 V */
    .v_precarious_min_dv = 1910, .v_precarious_max_dv = 2330,
    .f_min_dhz = 595, .f_max_dhz = 605,
    .pf_min_centi = 92, .pf_min_power_dw = 1000,
    .persist = 3,
    .sensor_fail_limit = 5,
    .spike_warmup = 30, .spike_k = 4, .spike_min_step_dw = 10000,
};

static anomaly_state_t s_a;

static void reset(void)
{
    anomaly_init(&s_a, &CFG);
}

/* Leitura normal: 220 V, 60 Hz, 500 W, FP 0,98. */
static pzem_measurement_t normal(void)
{
    pzem_measurement_t m;
    memset(&m, 0, sizeof m);
    m.voltage_dv = 2200;
    m.freq_dhz = 600;
    m.power_dw = 5000;
    m.pf_centi = 98;
    return m;
}

static void feed(pzem_measurement_t m, int times)
{
    for (int i = 0; i < times; ++i) anomaly_on_reading(&s_a, &m);
}

static int has(uint32_t flag)
{
    return (anomaly_flags(&s_a) & flag) != 0;
}

/* ------------------------------------------------------------ sensor -- */
static void test_sensor_offline_after_five_failures(void)
{
    reset();
    feed(normal(), 1);
    for (int i = 0; i < 4; ++i) anomaly_on_read_failure(&s_a);
    TAP_CHECK(!has(ANOM_SENSOR_SEM_RESPOSTA), "4 falhas ainda não deveriam alertar");
    anomaly_on_read_failure(&s_a);
    TAP_CHECK(has(ANOM_SENSOR_SEM_RESPOSTA), "5 falhas seguidas deveriam alertar");
    feed(normal(), 1);
    TAP_CHECK(!has(ANOM_SENSOR_SEM_RESPOSTA), "a 1ª leitura válida deveria limpar o alerta");
}

static void test_failures_must_be_consecutive(void)
{
    reset();
    for (int i = 0; i < 4; ++i) anomaly_on_read_failure(&s_a);
    feed(normal(), 1);
    for (int i = 0; i < 4; ++i) anomaly_on_read_failure(&s_a);
    TAP_CHECK(!has(ANOM_SENSOR_SEM_RESPOSTA), "falhas intercaladas com leitura válida não somam");
}

/* ------------------------------------------------------------ tensão -- */
static void test_undervoltage_prodist(void)
{
    reset();
    pzem_measurement_t m = normal();
    m.voltage_dv = 2020;   /* 202,0 V: limite inferior da faixa adequada */
    feed(m, 5);
    TAP_CHECK(!has(ANOM_SUBTENSAO), "202,0 V ainda é adequada");

    m.voltage_dv = 2000;   /* 200 V: precária */
    feed(m, 2);
    TAP_CHECK(!has(ANOM_SUBTENSAO), "2 leituras não bastam");
    feed(m, 1);
    TAP_CHECK(has(ANOM_SUBTENSAO), "3 leituras a 200 V deveriam alertar SUBTENSAO");
    TAP_CHECK(s_a.v_severity == VBAND_PRECARIA, "200 V é precária, veio %s", voltage_band_name(s_a.v_severity));

    m.voltage_dv = 1850;   /* 185 V: crítica */
    feed(m, 1);
    TAP_CHECK(s_a.v_severity == VBAND_CRITICA, "185 V é crítica, veio %s", voltage_band_name(s_a.v_severity));

    m.voltage_dv = 2200;
    feed(m, 2);
    TAP_CHECK(has(ANOM_SUBTENSAO), "2 leituras adequadas ainda não limpam");
    feed(m, 1);
    TAP_CHECK(!has(ANOM_SUBTENSAO), "3 leituras adequadas limpam o alerta");
    TAP_CHECK(s_a.v_severity == VBAND_ADEQUADA, "gravidade volta a adequada");
}

static void test_overvoltage_prodist(void)
{
    reset();
    pzem_measurement_t m = normal();
    m.voltage_dv = 2310;   /* 231,0 V: limite superior da faixa adequada */
    feed(m, 5);
    TAP_CHECK(!has(ANOM_SOBRETENSAO), "231,0 V ainda é adequada");

    m.voltage_dv = 2320;   /* 232 V: precária */
    feed(m, 3);
    TAP_CHECK(has(ANOM_SOBRETENSAO), "3 leituras a 232 V deveriam alertar SOBRETENSAO");
    TAP_CHECK(s_a.v_severity == VBAND_PRECARIA, "232 V é precária, veio %s", voltage_band_name(s_a.v_severity));

    m.voltage_dv = 2350;   /* 235 V: crítica */
    feed(m, 1);
    TAP_CHECK(s_a.v_severity == VBAND_CRITICA, "235 V é crítica, veio %s", voltage_band_name(s_a.v_severity));
    TAP_CHECK(!has(ANOM_SUBTENSAO), "sobretensão não liga subtensão");
}

/* -------------------------------------------------------- frequência -- */
static void test_frequency_out_of_range(void)
{
    reset();
    pzem_measurement_t m = normal();
    m.freq_dhz = 595;
    feed(m, 5);
    m.freq_dhz = 605;
    feed(m, 5);
    TAP_CHECK(!has(ANOM_FREQUENCIA), "59,5 e 60,5 Hz estão dentro da faixa");

    m.freq_dhz = 606;
    feed(m, 2);
    m.freq_dhz = 600;
    feed(m, 1);
    TAP_CHECK(!has(ANOM_FREQUENCIA), "2 leituras fora não bastam");

    m.freq_dhz = 594;
    feed(m, 3);
    TAP_CHECK(has(ANOM_FREQUENCIA), "3 leituras a 59,4 Hz deveriam alertar");
    m.freq_dhz = 600;
    feed(m, 3);
    TAP_CHECK(!has(ANOM_FREQUENCIA), "3 leituras normais limpam");
}

/* ------------------------------------------------- fator de potência -- */
static void test_low_power_factor(void)
{
    reset();
    pzem_measurement_t m = normal();   /* 500 W */
    m.pf_centi = 85;
    feed(m, 3);
    TAP_CHECK(has(ANOM_FP_BAIXO), "FP 0,85 com 500 W por 3 leituras deveria alertar");
    m.pf_centi = 95;
    feed(m, 3);
    TAP_CHECK(!has(ANOM_FP_BAIXO), "FP 0,95 limpa o alerta");
}

static void test_power_factor_ignored_without_load(void)
{
    reset();
    pzem_measurement_t m = normal();
    m.power_dw = 500;                  /* 50 W */
    m.pf_centi = 40;
    feed(m, 10);
    TAP_CHECK(!has(ANOM_FP_BAIXO), "com menos de 100 W o FP não é avaliado");
    m.power_dw = 1000;                 /* exatamente 100 W: avalia */
    feed(m, 3);
    TAP_CHECK(has(ANOM_FP_BAIXO), "com 100 W o FP é avaliado");
}

/* ------------------------------------------------- salto de potência -- */
static void feed_noisy_baseline(int n)
{
    static const int16_t noise[] = {150, -120, 80, -200, 60, -40, 190, -170};
    pzem_measurement_t m = normal();
    for (int i = 0; i < n; ++i) {
        m.power_dw = (uint32_t)(10000 + noise[i % 8]);   /* 1000 W ± 20 W */
        anomaly_on_reading(&s_a, &m);
    }
}

static void test_power_spike_detected(void)
{
    reset();
    feed_noisy_baseline(40);
    TAP_CHECK(s_a.spikes == 0, "ruído na linha de base não deveria virar evento");

    pzem_measurement_t m = normal();
    m.power_dw = 25000;                /* salto de +1500 W */
    feed(m, 1);
    TAP_CHECK(has(ANOM_SALTO_POTENCIA), "salto de 1500 W deveria ser sinalizado");
    TAP_CHECK(s_a.spikes == 1, "eventos = %u (esperado 1)", (unsigned)s_a.spikes);

    feed(m, 20);                       /* novo patamar: continua o mesmo evento */
    TAP_CHECK(s_a.spikes == 1, "patamar novo deveria contar 1 evento, contou %u", (unsigned)s_a.spikes);
    TAP_CHECK(!has(ANOM_SALTO_POTENCIA), "o evento não fica ligado no patamar");
}

static void test_small_variations_ignored(void)
{
    reset();
    feed_noisy_baseline(40);
    pzem_measurement_t m = normal();
    m.power_dw = 18000;                /* +800 W: abaixo do salto mínimo de 1000 W */
    feed(m, 1);
    TAP_CHECK(!has(ANOM_SALTO_POTENCIA) && s_a.spikes == 0, "+800 W não é salto atípico");
}

static void test_no_spike_during_warmup(void)
{
    reset();
    feed_noisy_baseline(10);           /* linha de base ainda não formada */
    pzem_measurement_t m = normal();
    m.power_dw = 40000;
    feed(m, 1);
    TAP_CHECK(s_a.spikes == 0, "sem linha de base (30 leituras) não há evento");
}

int main(void)
{
    tap_run("AC-002 sensor sem resposta depois de 5 falhas seguidas, some na 1ª válida",
            test_sensor_offline_after_five_failures);
    tap_run("AC-002 falhas precisam ser seguidas", test_failures_must_be_consecutive);
    tap_run("AC-011 subtensão PRODIST 220 V com gravidade precária/crítica", test_undervoltage_prodist);
    tap_run("AC-011 sobretensão PRODIST 220 V com gravidade precária/crítica", test_overvoltage_prodist);
    tap_run("AC-012 frequência fora de 59,5–60,5 Hz por 3 leituras", test_frequency_out_of_range);
    tap_run("AC-013 fator de potência baixo com carga relevante", test_low_power_factor);
    tap_run("AC-013 fator de potência não é avaliado abaixo de 100 W", test_power_factor_ignored_without_load);
    tap_run("AC-014 salto de potência atípico é sinalizado e contado uma vez", test_power_spike_detected);
    tap_run("AC-014 variações pequenas não disparam", test_small_variations_ignored);
    tap_run("AC-014 sem evento antes de formar a linha de base", test_no_spike_during_warmup);
    return tap_done();
}
