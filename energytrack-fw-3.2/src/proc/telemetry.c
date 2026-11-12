#include "telemetry.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "anomaly.h"
#include "goal.h"

/* ------------------------------------------------------------------ JSON -- */
/* Escritor sequencial num buffer fixo: na primeira vez que algo não cabe, para de escrever e marca falha. */
typedef struct {
    char  *buf;
    size_t len;
    size_t pos;
    bool   ok;
} writer_t;

static void put(writer_t *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(writer_t *w, const char *fmt, ...)
{
    if (!w->ok) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(w->buf + w->pos, w->len - w->pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->len - w->pos) {
        w->ok = false;
        return;
    }
    w->pos += (size_t)n;
}

/* mWh -> "45.312" (Wh com 3 casas, sem float). */
static void put_wh(writer_t *w, const char *key, uint64_t mwh)
{
    put(w, "\"%s\":%lu.%03lu", key, (unsigned long)(mwh / 1000u), (unsigned long)(mwh % 1000u));
}

static bool device_ok(const char *device)
{
    if (!device || device[0] == '\0') return false;
    const size_t n = strlen(device);
    if (n >= TELEMETRY_DEVICE_MAX) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = device[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && c != '-' && c != '_') return false;   /* nada que precise de escape no JSON */
    }
    return true;
}

static void put_reading(writer_t *w, const proc_snapshot_t *s)
{
    if (!s->has_reading) {
        put(w, "\"leitura\":null,");
        return;
    }
    const pzem_measurement_t *m = &s->last;
    put(w, "\"leitura\":{\"v\":%u.%u,\"i\":%lu.%03lu,\"p\":%lu.%lu,\"f\":%u.%u,\"fp\":%u.%02u,\"energia_pzem_wh\":%lu},",
        m->voltage_dv / 10u, m->voltage_dv % 10u,
        (unsigned long)(m->current_ma / 1000u), (unsigned long)(m->current_ma % 1000u),
        (unsigned long)(m->power_dw / 10u), (unsigned long)(m->power_dw % 10u),
        m->freq_dhz / 10u, m->freq_dhz % 10u,
        m->pf_centi / 100u, m->pf_centi % 100u,
        (unsigned long)m->energy_wh);
}

static void put_energy(writer_t *w, const proc_snapshot_t *s)
{
    put(w, "\"energia\":{\"dia\":%lu,", (unsigned long)(s->day_index + 1u));
    put_wh(w, "dia_wh", s->day_mwh);
    put(w, ",");
    put_wh(w, "anterior_wh", s->prev_day_mwh);
    put(w, ",");
    put_wh(w, "total_wh", s->total_mwh);
    put(w, ",\"pmed_w\":%lu.%lu,\"lacunas_recuperadas\":%lu},",
        (unsigned long)(s->avg_power_dw / 10u), (unsigned long)(s->avg_power_dw % 10u),
        (unsigned long)s->gaps_recovered);
}

static void put_goal(writer_t *w, const proc_snapshot_t *s)
{
    put(w, "\"meta\":{\"estado\":\"%s\",\"uso_pct\":%lu,", goal_status_name(s->goal_status),
        (unsigned long)s->goal_used_pct);
    put_wh(w, "projecao_wh", s->projected_mwh);
    put(w, ",\"alerta_potencia\":%s},", s->power_alert ? "true" : "false");
}

static void put_anomalies(writer_t *w, const proc_snapshot_t *s)
{
    put(w, "\"anomalias\":[");
    bool first = true;
    for (uint32_t bit = 0; bit < ANOM_COUNT; ++bit) {
        const anomaly_flag_t flag = (anomaly_flag_t)(1u << bit);
        if (!(s->anomalies & flag)) continue;
        put(w, "%s\"%s\"", first ? "" : ",", anomaly_name(flag));
        first = false;
    }
    put(w, "],");
}

int telemetry_build_json(const telemetry_header_t *h, const proc_snapshot_t *s, char *buf, size_t len)
{
    if (!h || !s || !buf || len == 0u || !device_ok(h->device)) return -1;

    writer_t w = { .buf = buf, .len = len, .pos = 0, .ok = true };
    put(&w, "{\"device\":\"%s\",\"seq\":%lu,\"uptime_s\":%lu,", h->device, (unsigned long)h->seq,
        (unsigned long)h->uptime_s);
    put_reading(&w, s);
    put_energy(&w, s);
    put_goal(&w, s);
    put_anomalies(&w, s);
    put(&w, "\"tensao_faixa\":\"%s\",\"saltos\":%lu,\"pzem\":{\"ok\":%lu,\"falha\":%lu}}",
        voltage_band_name(s->v_severity), (unsigned long)s->spikes,
        (unsigned long)s->pzem_ok, (unsigned long)s->pzem_fail);

    if (!w.ok) {
        buf[0] = '\0';   /* não deixa um JSON pela metade no buffer */
        return -1;
    }
    return (int)w.pos;
}

/* ---------------------------------------------------- resultado do envio -- */
void telemetry_stats_init(telemetry_stats_t *st)
{
    memset(st, 0, sizeof *st);
    st->last = TELEM_NENHUM;
}

uint32_t telemetry_next_seq(telemetry_stats_t *st)
{
    return ++st->seq;
}

telemetry_outcome_t telemetry_classify(bool json_ok, bool wifi_up, bool got_response, int http_status)
{
    if (!json_ok) return TELEM_ERRO_JSON;
    if (!wifi_up) return TELEM_SEM_WIFI;
    if (!got_response) return TELEM_ERRO_REDE;
    return (http_status >= 200 && http_status <= 299) ? TELEM_OK : TELEM_ERRO_HTTP;
}

void telemetry_record(telemetry_stats_t *st, telemetry_outcome_t outcome, int http_status)
{
    switch (outcome) {
    case TELEM_OK:        st->ok++;        break;
    case TELEM_SEM_WIFI:  st->fail_wifi++; break;
    case TELEM_ERRO_REDE: st->fail_net++;  break;
    case TELEM_ERRO_HTTP: st->fail_http++; break;
    case TELEM_ERRO_JSON: st->fail_json++; break;
    case TELEM_NENHUM:    return;
    }
    st->last = outcome;
    if (outcome == TELEM_OK || outcome == TELEM_ERRO_HTTP) st->last_http = http_status;
}

uint32_t telemetry_failures(const telemetry_stats_t *st)
{
    return st->fail_wifi + st->fail_net + st->fail_http + st->fail_json;
}

/* ---------------------------------------------------------- configuração -- */
static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

telemetry_cfg_t telemetry_check_config(const char *ssid, const char *password, const char *url)
{
    if (!ssid || ssid[0] == '\0') return TELEM_CFG_SEM_SSID;
    if (strlen(ssid) > TELEMETRY_SSID_MAX) return TELEM_CFG_SSID_LONGO;

    const size_t pass_len = password ? strlen(password) : 0u;
    if (pass_len != 0u && (pass_len < TELEMETRY_PASS_MIN || pass_len > TELEMETRY_PASS_MAX)) {
        return TELEM_CFG_SENHA_INVALIDA;
    }

    if (!url || url[0] == '\0') return TELEM_CFG_SEM_URL;
    const char *host = NULL;
    if (starts_with(url, "http://")) host = url + 7;
    else if (starts_with(url, "https://")) host = url + 8;
    if (!host || host[0] == '\0' || host[0] == '/') return TELEM_CFG_URL_INVALIDA;
    return TELEM_CFG_OK;
}

const char *telemetry_cfg_reason(telemetry_cfg_t cfg)
{
    switch (cfg) {
    case TELEM_CFG_OK:             return "configurada";
    case TELEM_CFG_SEM_SSID:       return "sem SSID";
    case TELEM_CFG_SSID_LONGO:     return "SSID com mais de 32 caracteres";
    case TELEM_CFG_SENHA_INVALIDA: return "senha do Wi-Fi precisa de 8 a 63 caracteres";
    case TELEM_CFG_SEM_URL:        return "sem URL do backend";
    case TELEM_CFG_URL_INVALIDA:   return "URL do backend precisa comecar com http:// ou https://";
    case TELEM_CFG_FALHA_INICIO:   return "falha ao iniciar o Wi-Fi ou o cliente HTTP";
    }
    return "?";
}

/* ------------------------------------------------- estado para o report -- */
static int format_last(const telemetry_stats_t *st, char *buf, size_t len)
{
    switch (st->last) {
    case TELEM_OK:
    case TELEM_ERRO_HTTP: return snprintf(buf, len, "HTTP %d", st->last_http);
    case TELEM_SEM_WIFI:  return snprintf(buf, len, "sem wifi");
    case TELEM_ERRO_REDE: return snprintf(buf, len, "erro de rede");
    case TELEM_ERRO_JSON: return snprintf(buf, len, "json grande demais");
    case TELEM_NENHUM:    break;
    }
    return snprintf(buf, len, "nenhum");
}

int telemetry_format_status(const telemetry_status_t *st, char *buf, size_t len)
{
    if (st->cfg != TELEM_CFG_OK) {
        return snprintf(buf, len, "rede: desligada (%s)", telemetry_cfg_reason(st->cfg));
    }
    char last[24];
    format_last(&st->stats, last, sizeof last);
    const telemetry_stats_t *s = &st->stats;
    return snprintf(buf, len,
                    "rede: wifi=%s%s%s | envios ok=%lu falha=%lu (wifi=%lu rede=%lu http=%lu json=%lu) "
                    "| ultimo=%s | seq=%lu | envio max=%lums",
                    st->wifi_up ? "conectado" : "desconectado", st->wifi_up ? " ip=" : "",
                    st->wifi_up ? st->ip : "",
                    (unsigned long)s->ok, (unsigned long)telemetry_failures(s), (unsigned long)s->fail_wifi,
                    (unsigned long)s->fail_net, (unsigned long)s->fail_http, (unsigned long)s->fail_json, last,
                    (unsigned long)s->seq, (unsigned long)(st->max_send_us / 1000u));
}
