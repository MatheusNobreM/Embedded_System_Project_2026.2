/**
 * @file energy_config.h
 * @brief Metas e limiares do processamento local (único lugar para ajustar).
 *
 * Valores fixos em compilação nesta etapa (WPS 3.1); ajuste em tempo de
 * execução fica para quando houver app/rede. Unidades seguem o PZEM:
 * tensão em dV (0,1 V), frequência em dHz (0,1 Hz), potência em dW (0,1 W),
 * fator de potência em centésimos e energia em mWh.
 */
#ifndef ENERGY_CONFIG_H
#define ENERGY_CONFIG_H

/* ------------------------------------------------------------------------- */
/* Rede — faixas de tensão do PRODIST Módulo 8 (ANEEL)                       */
/* ------------------------------------------------------------------------- */
#ifndef CFG_NOMINAL_VOLTAGE
#define CFG_NOMINAL_VOLTAGE          220     /* 220 (sistema 380/220 V) ou 127 (220/127 V) */
#endif

#if CFG_NOMINAL_VOLTAGE == 220
#define CFG_V_ADEQUATE_MIN_DV        2020    /* adequada: 202 <= V <= 231 */
#define CFG_V_ADEQUATE_MAX_DV        2310
#define CFG_V_PRECARIOUS_MIN_DV      1910    /* precária: 191-202 / 231-233; crítica fora */
#define CFG_V_PRECARIOUS_MAX_DV      2330
#elif CFG_NOMINAL_VOLTAGE == 127
#define CFG_V_ADEQUATE_MIN_DV        1170    /* adequada: 117 <= V <= 133 */
#define CFG_V_ADEQUATE_MAX_DV        1330
#define CFG_V_PRECARIOUS_MIN_DV      1100    /* precária: 110-117 / 133-135; crítica fora */
#define CFG_V_PRECARIOUS_MAX_DV      1350
#else
#error "CFG_NOMINAL_VOLTAGE deve ser 220 ou 127"
#endif

#define CFG_FREQ_MIN_DHZ             595     /* 59,5 Hz */
#define CFG_FREQ_MAX_DHZ             605     /* 60,5 Hz */
#define CFG_PF_MIN_CENTI             92      /* fator de potência de referência da ANEEL */
#define CFG_PF_MIN_POWER_DW          1000    /* só avalia FP com carga >= 100 W */
#define CFG_ALERT_PERSIST            3       /* leituras seguidas para ligar/desligar alertas */

/* ------------------------------------------------------------------------- */
/* Sensor                                                                    */
/* ------------------------------------------------------------------------- */
#define CFG_SENSOR_FAIL_LIMIT        5       /* falhas seguidas -> SENSOR_SEM_RESPOSTA */

/* ------------------------------------------------------------------------- */
/* Salto de potência atípico                                                 */
/* ------------------------------------------------------------------------- */
#define CFG_SPIKE_WARMUP             30      /* leituras para formar a linha de base */
#define CFG_SPIKE_K                  4       /* múltiplo do desvio típico */
#define CFG_SPIKE_MIN_STEP_DW        10000   /* salto mínimo de 1000 W */

/* ------------------------------------------------------------------------- */
/* Energia, meta diária e limite de potência                                 */
/* ------------------------------------------------------------------------- */
#define CFG_DAY_LENGTH_S             86400u  /* "dia" de 24 h contado do boot (sem RTC) */
#define CFG_ENERGY_MAX_GAP_S         3u      /* lacuna maior usa o contador do PZEM */
#define CFG_DAILY_GOAL_MWH           5000000u /* meta: 5 kWh/dia */
#define CFG_GOAL_WARN_PCT            80u     /* ATENCAO a partir de 80% */
#define CFG_GOAL_MIN_ELAPSED_S       3600u   /* projeção só depois de 1 h de dia */
#define CFG_POWER_LIMIT_DW           30000u  /* limite: 3000 W ... */
#define CFG_POWER_HOLD_S             10u     /* ... por 10 s seguidos */
#define CFG_POWER_CLEAR_PCT          95u     /* alerta some abaixo de 95% do limite */

#endif /* ENERGY_CONFIG_H */
