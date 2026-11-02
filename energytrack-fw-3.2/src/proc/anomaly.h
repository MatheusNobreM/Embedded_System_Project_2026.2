/**
 * @file anomaly.h
 * @brief Detecção de anomalias da rede, da carga, do sensor e da placa.
 *
 * Alertas com persistência (persist leituras seguidas para ligar e para
 * desligar), para não alarmar com uma leitura ruidosa:
 *   SUBTENSAO / SOBRETENSAO  fora da faixa adequada do PRODIST, com gravidade
 *                            precária ou crítica
 *   FREQUENCIA               fora de f_min..f_max
 *   FP_BAIXO                 fator de potência baixo, só com carga relevante
 *   SUPERAQUECIMENTO         temperatura da placa, com histerese
 * Alertas imediatos:
 *   SENSOR_SEM_RESPOSTA      sensor_fail_limit falhas seguidas; some na 1ª válida
 *   SALTO_POTENCIA           evento: potência acima da linha de base (média
 *                            móvel exponencial) mais que max(k × desvio, min_step);
 *                            um patamar novo conta UM evento (rearma ao normalizar)
 *
 * C puro: roda igual no host e na placa.
 */
#ifndef ANOMALY_H
#define ANOMALY_H

#include <stdbool.h>
#include <stdint.h>

#include "pzem_modbus.h"

typedef enum {
    ANOM_SUBTENSAO           = 1u << 0,
    ANOM_SOBRETENSAO         = 1u << 1,
    ANOM_FREQUENCIA          = 1u << 2,
    ANOM_FP_BAIXO            = 1u << 3,
    ANOM_SALTO_POTENCIA      = 1u << 4,
    ANOM_SENSOR_SEM_RESPOSTA = 1u << 5,
    ANOM_SUPERAQUECIMENTO    = 1u << 6,
} anomaly_flag_t;

#define ANOM_COUNT 7u

typedef enum {
    VBAND_ADEQUADA = 0,
    VBAND_PRECARIA,
    VBAND_CRITICA,
} voltage_band_t;

typedef struct {
    uint16_t v_adequate_min_dv, v_adequate_max_dv;      /* PRODIST 220 V: 202,0 .. 231,0 V */
    uint16_t v_precarious_min_dv, v_precarious_max_dv;  /* PRODIST 220 V: 191,0 .. 233,0 V */
    uint16_t f_min_dhz, f_max_dhz;                      /* 59,5 .. 60,5 Hz */
    uint16_t pf_min_centi;                              /* 0,92 */
    uint32_t pf_min_power_dw;                           /* só avalia FP com >= 100 W */
    uint8_t  persist;                                   /* leituras seguidas (3) */
    uint8_t  sensor_fail_limit;                         /* falhas seguidas (5) */
    int32_t  temp_set_mc, temp_clear_mc;                /* 70 °C liga, < 65 °C desliga */
    uint16_t spike_warmup;                              /* leituras para formar a linha de base (30) */
    uint8_t  spike_k;                                   /* múltiplo do desvio típico (4) */
    uint32_t spike_min_step_dw;                         /* salto mínimo (1000 W) */
} anomaly_config_t;

/* Liga depois de N condições seguidas, desliga depois de N normais seguidas. */
typedef struct {
    uint8_t bad, good;
    bool    active;
} anomaly_debounce_t;

typedef struct {
    anomaly_config_t cfg;
    anomaly_debounce_t under_v, over_v, freq, pf, hot;
    voltage_band_t v_severity;      /* gravidade do alerta de tensão ativo */

    uint32_t consecutive_fails;
    bool     sensor_offline;

    uint32_t baseline_n;
    int64_t  mean_x16;              /* linha de base da potência, dW × 16 */
    int64_t  dev_x16;               /* desvio absoluto médio, dW × 16 */
    bool     spike_armed;           /* rearma quando a potência volta ao normal */
    bool     spike_now;
    uint32_t spikes;                /* eventos de salto desde o boot */
} anomaly_state_t;

void anomaly_init(anomaly_state_t *a, const anomaly_config_t *cfg);

/** Leitura válida do PZEM. */
void anomaly_on_reading(anomaly_state_t *a, const pzem_measurement_t *m);

/** Leitura do PZEM que falhou (timeout, CRC, quadro incompleto). */
void anomaly_on_read_failure(anomaly_state_t *a);

/** Temperatura média de um bloco do ADC, em m°C. */
void anomaly_on_temperature(anomaly_state_t *a, int32_t temp_mc);

/** Máscara de anomaly_flag_t ativas agora. */
uint32_t anomaly_flags(const anomaly_state_t *a);

const char *anomaly_name(anomaly_flag_t flag);
const char *voltage_band_name(voltage_band_t band);

#endif /* ANOMALY_H */
