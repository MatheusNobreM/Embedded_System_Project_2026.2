/**
 * @file processing.h
 * @brief Processamento local: junta energia, meta e anomalias a cada amostra.
 *
 * A tarefa proc é a única dona do proc_state_t. Cada amostra que chega (leitura
 * do PZEM, válida ou não, e bloco de temperatura do ADC) atualiza o estado; o
 * resultado sai como proc_snapshot_t, que as tarefas status e report leem.
 *
 * C puro: roda igual no host e na placa.
 */
#ifndef PROCESSING_H
#define PROCESSING_H

#include <stdbool.h>
#include <stdint.h>

#include "anomaly.h"
#include "energy.h"
#include "goal.h"
#include "pzem_modbus.h"

typedef struct {
    energy_config_t  energy;
    goal_config_t    goal;
    anomaly_config_t anomaly;
} proc_config_t;

typedef struct {
    energy_state_t     energy;
    goal_state_t       goal;
    anomaly_state_t    anomaly;
    pzem_measurement_t last;          /* última leitura válida */
    bool               has_reading;
    uint32_t           pzem_ok, pzem_fail;
    int32_t            temp_mc;
} proc_state_t;

/** Fotografia do processamento para quem só lê (status e report). */
typedef struct {
    pzem_measurement_t last;
    bool               has_reading;
    uint32_t           pzem_ok, pzem_fail;
    int32_t            temp_mc;

    uint32_t           day_index;
    uint32_t           day_mwh, prev_day_mwh;
    uint64_t           total_mwh;
    uint64_t           day_elapsed_us;
    uint32_t           avg_power_dw;
    uint32_t           gaps_recovered;

    goal_status_t      goal_status;
    uint32_t           goal_used_pct;
    uint64_t           projected_mwh;
    bool               power_alert;

    uint32_t           anomalies;     /* máscara de anomaly_flag_t */
    voltage_band_t     v_severity;
    uint32_t           spikes;
} proc_snapshot_t;

typedef enum {
    LED_ON = 0,        /* tudo normal */
    LED_BLINK_SLOW,    /* 1 Hz: sensor sem resposta (ou ainda sem leitura) */
    LED_BLINK_FAST,    /* 5 Hz: anomalia, meta estourada ou limite de potência */
} led_pattern_t;

/** Configuração padrão a partir de energy_config.h. */
void proc_default_config(proc_config_t *cfg);

void proc_init(proc_state_t *p, const proc_config_t *cfg, uint64_t now_us);

/** Leitura do PZEM no instante t_us; m só é lido quando ok. */
void proc_on_pzem(proc_state_t *p, uint64_t t_us, bool ok, const pzem_measurement_t *m);

/** Temperatura média de um bloco do ADC. */
void proc_on_temperature(proc_state_t *p, uint64_t t_us, int32_t temp_mc);

void proc_snapshot(const proc_state_t *p, uint64_t now_us, proc_snapshot_t *out);

led_pattern_t proc_led_pattern(const proc_snapshot_t *s);

/** LED aceso ou apagado no instante t_us para o padrão. */
bool led_pattern_level(led_pattern_t pattern, uint64_t t_us);

#endif /* PROCESSING_H */
