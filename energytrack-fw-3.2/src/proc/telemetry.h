/**
 * @file telemetry.h
 * @brief Telemetria para o backend: o JSON do contrato, os contadores de
 *        envio e a linha "rede:" do relatório.
 *
 * A tarefa net (app/energytrack_main.c) pega a fotografia do processamento,
 * monta o JSON com telemetry_build_json() e manda por HTTP POST. O resultado
 * de cada ciclo é classificado e contado aqui, e o report imprime a linha
 * "rede: ..." com telemetry_format_status().
 *
 * O contrato do JSON (campos, unidades, exemplo) está na especificação da
 * feature telemetria-wifi; tools/backend-teste.mjs confere o mesmo contrato.
 *
 * C puro (sem ESP-IDF nem FreeRTOS): roda igual no host e na placa.
 */
#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "processing.h"

#define TELEMETRY_JSON_MAX    768u   /* buffer do corpo: o pior caso (~620 B) cabe com folga */
#define TELEMETRY_DEVICE_MAX  24u    /* "et-" + 12 dígitos hex do MAC + '\0' cabem */
#define TELEMETRY_SSID_MAX    32u    /* limite do 802.11 */
#define TELEMETRY_PASS_MIN    8u     /* WPA2-PSK: 8 a 63 caracteres (vazio = rede aberta) */
#define TELEMETRY_PASS_MAX    63u

/* ------------------------------------------------------------------ JSON -- */
typedef struct {
    const char *device;    /* identificador da placa: só letras, dígitos, '-' e '_' */
    uint32_t    seq;       /* sobe a cada ciclo de envio (telemetry_next_seq) */
    uint32_t    uptime_s;  /* segundos desde o boot */
} telemetry_header_t;

/**
 * Monta o JSON do contrato em buf (com '\0').
 * @return tamanho do JSON sem o '\0', ou -1 se não coube em len bytes ou se o
 *         device é inválido. Com -1 o conteúdo de buf não serve e não deve
 *         ser enviado: nunca sai um JSON cortado.
 */
int telemetry_build_json(const telemetry_header_t *h, const proc_snapshot_t *s, char *buf, size_t len);

/* ---------------------------------------------------- resultado do envio -- */
typedef enum {
    TELEM_NENHUM = 0,   /* ainda não houve ciclo */
    TELEM_OK,           /* backend respondeu 2xx */
    TELEM_SEM_WIFI,     /* Wi-Fi sem IP: nem tentou */
    TELEM_ERRO_REDE,    /* sem resposta HTTP: conexão, DNS ou tempo esgotado */
    TELEM_ERRO_HTTP,    /* respondeu com código que não é 2xx */
    TELEM_ERRO_JSON,    /* corpo não coube no buffer: não enviou */
} telemetry_outcome_t;

typedef struct {
    uint32_t            seq;          /* último seq usado (0 = nenhum ciclo) */
    uint32_t            ok;
    uint32_t            fail_wifi, fail_net, fail_http, fail_json;
    telemetry_outcome_t last;
    int                 last_http;    /* último código HTTP recebido (0 = nenhum) */
} telemetry_stats_t;

void telemetry_stats_init(telemetry_stats_t *st);

/** seq do ciclo que começa: sobe 1 a cada ciclo, com ou sem sucesso. */
uint32_t telemetry_next_seq(telemetry_stats_t *st);

/**
 * Classifica um ciclo.
 * @param json_ok      o corpo foi montado (telemetry_build_json >= 0)
 * @param wifi_up      o Wi-Fi tinha IP
 * @param got_response houve resposta HTTP (com qualquer código)
 * @param http_status  o código da resposta, quando got_response
 */
telemetry_outcome_t telemetry_classify(bool json_ok, bool wifi_up, bool got_response, int http_status);

/** Conta o resultado de um ciclo nos contadores. */
void telemetry_record(telemetry_stats_t *st, telemetry_outcome_t outcome, int http_status);

uint32_t telemetry_failures(const telemetry_stats_t *st);

/* ---------------------------------------------------------- configuração -- */
typedef enum {
    TELEM_CFG_OK = 0,
    TELEM_CFG_SEM_SSID,
    TELEM_CFG_SSID_LONGO,
    TELEM_CFG_SENHA_INVALIDA,
    TELEM_CFG_SEM_URL,
    TELEM_CFG_URL_INVALIDA,
} telemetry_cfg_t;

/** Confere SSID, senha e URL do backend (vindos do menuconfig). */
telemetry_cfg_t telemetry_check_config(const char *ssid, const char *password, const char *url);

/** Motivo legível de a rede estar desligada (ASCII, para o console). */
const char *telemetry_cfg_reason(telemetry_cfg_t cfg);

/* ------------------------------------------------- estado para o report -- */
/** O que a tarefa net publica para o report (caixa de correio de 1). */
typedef struct {
    telemetry_cfg_t   cfg;
    bool              wifi_up;
    char              ip[16];           /* "192.168.0.23" ou "" */
    uint32_t          max_send_us;      /* maior duração de um ciclo de envio */
    telemetry_stats_t stats;
} telemetry_status_t;

/**
 * Linha "rede: ..." do relatório, sem '\n'.
 * @return o que snprintf devolveria (tamanho sem o '\0'; >= len = cortou).
 */
int telemetry_format_status(const telemetry_status_t *st, char *buf, size_t len);

#endif /* TELEMETRY_H */
