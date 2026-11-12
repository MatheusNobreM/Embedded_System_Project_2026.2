/**
 * Teste de host da telemetria (telemetry.c): o JSON do contrato, a contagem
 * dos envios, a linha "rede:" do relatório e a conferência da configuração.
 *
 * Os JSON montados saem como comentários TAP ("# json-<caso>: {...}"): o
 * teste em Node (test/spec/telemetria.spec.test.mjs) os passa por JSON.parse
 * e compara campo a campo com o contrato, e o teste do backend de teste os
 * envia por HTTP.
 */
#include <limits.h>
#include <string.h>
#include "telemetry.h"
#include "tap.h"

static proc_snapshot_t s_snap;
static char s_buf[TELEMETRY_JSON_MAX];

/* A fotografia do exemplo do contrato: 221,3 V; 1,234 A; 271,9 W; 60,0 Hz; FP 0,98. */
static void example_snapshot(void)
{
    memset(&s_snap, 0, sizeof s_snap);
    s_snap.has_reading = true;
    s_snap.last.voltage_dv = 2213;
    s_snap.last.current_ma = 1234;
    s_snap.last.power_dw = 2719;
    s_snap.last.energy_wh = 102;
    s_snap.last.freq_dhz = 600;
    s_snap.last.pf_centi = 98;
    s_snap.pzem_ok = 601;
    s_snap.pzem_fail = 0;
    s_snap.day_index = 0;
    s_snap.day_mwh = 45312;
    s_snap.prev_day_mwh = 0;
    s_snap.total_mwh = 45312;
    s_snap.avg_power_dw = 2715;
    s_snap.gaps_recovered = 0;
    s_snap.goal_status = GOAL_OK;
    s_snap.goal_used_pct = 0;
    s_snap.projected_mwh = 6516000;
    s_snap.power_alert = false;
    s_snap.anomalies = 0;
    s_snap.v_severity = VBAND_ADEQUADA;
    s_snap.spikes = 0;
}

static const telemetry_header_t EXAMPLE_HEADER = { "et-a4cf12345678", 121, 602 };

static int build(const telemetry_header_t *h)
{
    return telemetry_build_json(h, &s_snap, s_buf, sizeof s_buf);
}

/* ------------------------------------------------------------------ JSON -- */
static void test_json_contract(void)
{
    example_snapshot();
    const int n = build(&EXAMPLE_HEADER);
    static const char expected[] =
        "{\"device\":\"et-a4cf12345678\",\"seq\":121,\"uptime_s\":602,"
        "\"leitura\":{\"v\":221.3,\"i\":1.234,\"p\":271.9,\"f\":60.0,\"fp\":0.98,\"energia_pzem_wh\":102},"
        "\"energia\":{\"dia\":1,\"dia_wh\":45.312,\"anterior_wh\":0.000,\"total_wh\":45.312,\"pmed_w\":271.5,"
        "\"lacunas_recuperadas\":0},"
        "\"meta\":{\"estado\":\"OK\",\"uso_pct\":0,\"projecao_wh\":6516.000,\"alerta_potencia\":false},"
        "\"anomalias\":[],\"tensao_faixa\":\"adequada\",\"saltos\":0,\"pzem\":{\"ok\":601,\"falha\":0}}";
    TAP_CHECK(n == (int)strlen(expected), "tamanho %d, esperado %d", n, (int)strlen(expected));
    TAP_CHECK(strcmp(s_buf, expected) == 0, "JSON diferente do contrato:\n#   veio     %s\n#   esperado %s", s_buf, expected);
    printf("# json-leitura: %s\n", s_buf);
}

static void test_json_anomalies_as_names(void)
{
    example_snapshot();
    s_snap.last.voltage_dv = 1985;
    s_snap.anomalies = ANOM_SUBTENSAO | ANOM_FREQUENCIA;
    s_snap.v_severity = VBAND_PRECARIA;
    s_snap.goal_status = GOAL_ATENCAO;
    s_snap.power_alert = true;
    const int n = build(&EXAMPLE_HEADER);
    TAP_CHECK(n > 0, "não montou o JSON");
    TAP_CHECK(strstr(s_buf, "\"anomalias\":[\"SUBTENSAO\",\"FREQUENCIA\"]") != NULL, "lista de anomalias: %s", s_buf);
    TAP_CHECK(strstr(s_buf, "\"tensao_faixa\":\"precaria\"") != NULL, "faixa de tensão: %s", s_buf);
    TAP_CHECK(strstr(s_buf, "\"estado\":\"ATENCAO\"") != NULL, "estado da meta: %s", s_buf);
    TAP_CHECK(strstr(s_buf, "\"alerta_potencia\":true") != NULL, "alerta de potência: %s", s_buf);
    printf("# json-anomalias: %s\n", s_buf);
}

/* Todos os alertas e os maiores valores possíveis: o pior caso precisa caber no buffer da placa. */
static void test_json_worst_case_fits(void)
{
    memset(&s_snap, 0xFF, sizeof s_snap);
    s_snap.has_reading = true;
    s_snap.anomalies = (1u << ANOM_COUNT) - 1u;
    s_snap.goal_status = GOAL_PROJECAO_ACIMA;
    s_snap.v_severity = VBAND_CRITICA;
    s_snap.power_alert = true;
    s_snap.total_mwh = 4294967295000ull;      /* maior total que o formato representa sem perder dígitos */
    s_snap.projected_mwh = 4294967295000ull;
    const telemetry_header_t h = { "et-ffffffffffff", UINT32_MAX, UINT32_MAX };
    const int n = build(&h);
    TAP_CHECK(n > 0 && n < (int)TELEMETRY_JSON_MAX, "pior caso com %d bytes (buffer de %u)", n,
              (unsigned)TELEMETRY_JSON_MAX);
    printf("# json-pior-caso: %s\n", s_buf);
    printf("# json-pior-caso-bytes: %d\n", n);
}

static void test_json_rejects_bad_device(void)
{
    example_snapshot();
    const char *bad[] = { "", "et\"x", "et x", "et-a4cf12345678-muito-comprido" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        const telemetry_header_t h = { bad[i], 1, 1 };
        TAP_CHECK(build(&h) == -1, "device '%s' deveria ser recusado", bad[i]);
    }
    const telemetry_header_t none = { NULL, 1, 1 };
    TAP_CHECK(build(&none) == -1, "device NULL deveria ser recusado");
}

static void test_json_without_reading(void)
{
    example_snapshot();
    s_snap.has_reading = false;
    memset(&s_snap.last, 0, sizeof s_snap.last);
    s_snap.pzem_ok = 0;
    s_snap.pzem_fail = 7;
    s_snap.anomalies = ANOM_SENSOR_SEM_RESPOSTA;
    s_snap.day_mwh = s_snap.total_mwh = 0;
    s_snap.avg_power_dw = 0;
    s_snap.projected_mwh = 0;
    const int n = build(&EXAMPLE_HEADER);
    TAP_CHECK(n > 0, "não montou o JSON");
    TAP_CHECK(strstr(s_buf, "\"leitura\":null,") != NULL, "leitura deveria ser null: %s", s_buf);
    TAP_CHECK(strstr(s_buf, "\"v\":") == NULL, "sem leitura não pode sair tensão: %s", s_buf);
    TAP_CHECK(strstr(s_buf, "\"anomalias\":[\"SENSOR_SEM_RESPOSTA\"]") != NULL, "anomalias: %s", s_buf);
    TAP_CHECK(strstr(s_buf, "\"pzem\":{\"ok\":0,\"falha\":7}") != NULL, "contadores do PZEM: %s", s_buf);
    printf("# json-sem-leitura: %s\n", s_buf);
}

/* Em nenhum tamanho de buffer menor que o JSON sai um corpo cortado. */
static void test_json_never_truncated(void)
{
    example_snapshot();
    const int full = build(&EXAMPLE_HEADER);
    TAP_CHECK(full > 0, "não montou o JSON");
    char small[TELEMETRY_JSON_MAX];
    for (int len = 1; len <= full; ++len) {
        memset(small, 'x', sizeof small);
        const int n = telemetry_build_json(&EXAMPLE_HEADER, &s_snap, small, (size_t)len);
        TAP_CHECK(n == -1, "buffer de %d bytes (JSON tem %d) deveria falhar, devolveu %d", len, full, n);
        TAP_CHECK(small[0] == '\0', "buffer de %d bytes ficou com um pedaço do JSON", len);
        if (n != -1 || small[0] != '\0') return;
    }
    TAP_CHECK(telemetry_build_json(&EXAMPLE_HEADER, &s_snap, small, (size_t)full + 1u) == full,
              "com %d bytes (JSON + '\\0') deveria caber", full + 1);
    TAP_CHECK(telemetry_build_json(&EXAMPLE_HEADER, &s_snap, small, 0) == -1, "buffer vazio deveria falhar");
    TAP_CHECK(telemetry_classify(false, true, true, 200) == TELEM_ERRO_JSON,
              "JSON que não coube conta como falha de JSON, mesmo com rede");
}

/* ---------------------------------------------------- resultado do envio -- */
static void test_classify(void)
{
    TAP_CHECK(telemetry_classify(true, true, true, 200) == TELEM_OK, "200 = ok");
    TAP_CHECK(telemetry_classify(true, true, true, 201) == TELEM_OK, "201 = ok");
    TAP_CHECK(telemetry_classify(true, true, true, 204) == TELEM_OK, "204 = ok");
    TAP_CHECK(telemetry_classify(true, true, true, 299) == TELEM_OK, "299 = ok");
    TAP_CHECK(telemetry_classify(true, true, true, 199) == TELEM_ERRO_HTTP, "199 = falha HTTP");
    TAP_CHECK(telemetry_classify(true, true, true, 300) == TELEM_ERRO_HTTP, "300 = falha HTTP");
    TAP_CHECK(telemetry_classify(true, true, true, 400) == TELEM_ERRO_HTTP, "400 = falha HTTP");
    TAP_CHECK(telemetry_classify(true, true, true, 500) == TELEM_ERRO_HTTP, "500 = falha HTTP");
    TAP_CHECK(telemetry_classify(true, true, false, 0) == TELEM_ERRO_REDE, "sem resposta = falha de rede");
    TAP_CHECK(telemetry_classify(true, false, false, 0) == TELEM_SEM_WIFI, "sem Wi-Fi = falha sem wifi");
    TAP_CHECK(telemetry_classify(false, false, false, 0) == TELEM_ERRO_JSON, "JSON grande = falha de JSON");
}

static void test_counters_and_seq(void)
{
    telemetry_stats_t st;
    telemetry_stats_init(&st);
    TAP_CHECK(st.seq == 0 && st.last == TELEM_NENHUM, "começa sem ciclos");

    static const struct { telemetry_outcome_t o; int http; } cycles[] = {
        { TELEM_SEM_WIFI, 0 }, { TELEM_OK, 201 }, { TELEM_OK, 201 }, { TELEM_ERRO_HTTP, 500 },
        { TELEM_ERRO_REDE, 0 }, { TELEM_ERRO_JSON, 0 }, { TELEM_OK, 200 },
    };
    for (size_t i = 0; i < sizeof cycles / sizeof cycles[0]; ++i) {
        const uint32_t seq = telemetry_next_seq(&st);
        TAP_CHECK(seq == i + 1u, "ciclo %u com seq %lu", (unsigned)(i + 1u), (unsigned long)seq);
        telemetry_record(&st, cycles[i].o, cycles[i].http);
    }
    TAP_CHECK(st.ok == 3, "ok=%lu (esperado 3)", (unsigned long)st.ok);
    TAP_CHECK(st.fail_wifi == 1 && st.fail_net == 1 && st.fail_http == 1 && st.fail_json == 1,
              "falhas por motivo: wifi=%lu rede=%lu http=%lu json=%lu", (unsigned long)st.fail_wifi,
              (unsigned long)st.fail_net, (unsigned long)st.fail_http, (unsigned long)st.fail_json);
    TAP_CHECK(telemetry_failures(&st) == 4, "falha=%lu (esperado 4)", (unsigned long)telemetry_failures(&st));
    TAP_CHECK(st.ok + telemetry_failures(&st) == st.seq, "todo ciclo é contado uma vez");
    TAP_CHECK(st.last == TELEM_OK && st.last_http == 200, "último resultado: %d HTTP %d", (int)st.last, st.last_http);
}

static void test_report_line(void)
{
    telemetry_status_t st;
    memset(&st, 0, sizeof st);
    st.cfg = TELEM_CFG_OK;
    st.wifi_up = true;
    strcpy(st.ip, "192.168.0.23");
    st.max_send_us = 85400;
    telemetry_stats_init(&st.stats);
    for (int i = 0; i < 118; ++i) {
        telemetry_next_seq(&st.stats);
        telemetry_record(&st.stats, TELEM_OK, 201);
    }
    telemetry_next_seq(&st.stats);
    telemetry_record(&st.stats, TELEM_SEM_WIFI, 0);
    telemetry_next_seq(&st.stats);
    telemetry_record(&st.stats, TELEM_ERRO_HTTP, 503);

    char line[256];
    telemetry_format_status(&st, line, sizeof line);
    static const char expected[] =
        "rede: wifi=conectado ip=192.168.0.23 | envios ok=118 falha=2 (wifi=1 rede=0 http=1 json=0) "
        "| ultimo=HTTP 503 | seq=120 | envio max=85ms";
    TAP_CHECK(strcmp(line, expected) == 0, "linha do relatório:\n#   veio     %s\n#   esperado %s", line, expected);
    printf("# rede-linha: %s\n", line);

    st.wifi_up = false;
    telemetry_format_status(&st, line, sizeof line);
    TAP_CHECK(strncmp(line, "rede: wifi=desconectado | envios ok=118", 39) == 0, "sem Wi-Fi: %s", line);

    telemetry_status_t fresh;
    memset(&fresh, 0, sizeof fresh);
    telemetry_stats_init(&fresh.stats);
    telemetry_format_status(&fresh, line, sizeof line);
    TAP_CHECK(strstr(line, "ultimo=nenhum | seq=0") != NULL, "antes do primeiro envio: %s", line);
}

/* ---------------------------------------------------------- configuração -- */
static void test_config_disabled_with_reason(void)
{
    const char *url = "http://192.168.0.10:8080/api/leituras";
    TAP_CHECK(telemetry_check_config("", "", url) == TELEM_CFG_SEM_SSID, "SSID vazio desliga a rede");
    TAP_CHECK(telemetry_check_config(NULL, NULL, url) == TELEM_CFG_SEM_SSID, "SSID ausente desliga a rede");
    TAP_CHECK(telemetry_check_config("casa", "segredo123", "") == TELEM_CFG_SEM_URL, "URL vazia desliga a rede");
    TAP_CHECK(telemetry_check_config("casa", "segredo123", NULL) == TELEM_CFG_SEM_URL, "URL ausente desliga a rede");
    TAP_CHECK(telemetry_check_config("casa", "segredo123", "ftp://x/y") == TELEM_CFG_URL_INVALIDA, "ftp:// recusado");
    TAP_CHECK(telemetry_check_config("casa", "segredo123", "192.168.0.10/api") == TELEM_CFG_URL_INVALIDA,
              "URL sem http:// recusada");
    TAP_CHECK(telemetry_check_config("casa", "segredo123", "http://") == TELEM_CFG_URL_INVALIDA, "URL sem host recusada");
    TAP_CHECK(telemetry_check_config("casa", "1234567", url) == TELEM_CFG_SENHA_INVALIDA, "senha de 7 caracteres recusada");
    TAP_CHECK(telemetry_check_config("casa", "1234567890123456789012345678901234567890123456789012345678901234", url) ==
                  TELEM_CFG_SENHA_INVALIDA, "senha de 64 caracteres recusada");
    TAP_CHECK(telemetry_check_config("123456789012345678901234567890123", "", url) == TELEM_CFG_SSID_LONGO,
              "SSID de 33 caracteres recusado");

    TAP_CHECK(telemetry_check_config("casa", "segredo123", url) == TELEM_CFG_OK, "configuração completa liga a rede");
    TAP_CHECK(telemetry_check_config("casa", "", url) == TELEM_CFG_OK, "rede aberta (sem senha) é aceita");
    TAP_CHECK(telemetry_check_config("casa", "segredo123", "https://api.exemplo.com/leituras") == TELEM_CFG_OK,
              "https:// é aceito");

    telemetry_status_t st;
    memset(&st, 0, sizeof st);
    char line[160];
    st.cfg = TELEM_CFG_SEM_SSID;
    telemetry_format_status(&st, line, sizeof line);
    TAP_CHECK(strcmp(line, "rede: desligada (sem SSID)") == 0, "linha sem SSID: %s", line);
    st.cfg = TELEM_CFG_SEM_URL;
    telemetry_format_status(&st, line, sizeof line);
    TAP_CHECK(strcmp(line, "rede: desligada (sem URL do backend)") == 0, "linha sem URL: %s", line);
    st.cfg = TELEM_CFG_URL_INVALIDA;
    telemetry_format_status(&st, line, sizeof line);
    TAP_CHECK(strstr(line, "rede: desligada (URL do backend precisa comecar com http:// ou https://)") == line,
              "linha com URL inválida: %s", line);
    printf("# rede-desligada: %s\n", line);
}

int main(void)
{
    tap_run("AC-022 JSON com leitura válida segue o contrato", test_json_contract);
    tap_run("AC-022 anomalias ativas saem como lista de nomes", test_json_anomalies_as_names);
    tap_run("AC-022 pior caso cabe no buffer da placa", test_json_worst_case_fits);
    tap_run("AC-022 device fora do padrão não gera JSON", test_json_rejects_bad_device);
    tap_run("AC-023 sem leitura válida o campo leitura vai null", test_json_without_reading);
    tap_run("AC-024 JSON que não cabe no buffer nunca sai cortado", test_json_never_truncated);
    tap_run("AC-024 só 2xx conta como envio ok; falhas separadas por motivo", test_classify);
    tap_run("AC-024 seq sobe a cada ciclo e todo ciclo é contado", test_counters_and_seq);
    tap_run("AC-024 linha rede: do relatório", test_report_line);
    tap_run("AC-027 sem SSID ou URL válida a rede fica desligada com o motivo", test_config_disabled_with_reason);
    return tap_done();
}
