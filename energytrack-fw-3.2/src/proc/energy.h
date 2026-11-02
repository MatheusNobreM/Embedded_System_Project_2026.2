/**
 * @file energy.h
 * @brief Potência média e energia (do dia e desde o boot) calculadas na placa.
 *
 * - Leituras válidas próximas (intervalo <= max_gap_us): energia pelo
 *   trapézio da potência no tempo, com resolução bem abaixo de 1 mWh.
 * - Intervalo maior (falha de comunicação): energia pela diferença do
 *   contador interno do PZEM, que continua contando sem a placa (1 Wh).
 *   Contador que voltou para trás (zerado) não gera energia negativa.
 * - O "dia" vira a cada day_length_us (24 h a partir do boot, sem RTC);
 *   um intervalo que atravessa a virada é dividido proporcionalmente.
 *
 * Inteiros com escala fixa: potência em dW (0,1 W), energia em mWh.
 * C puro: roda igual no host e na placa.
 */
#ifndef ENERGY_H
#define ENERGY_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t day_length_us;   /* duração do "dia" (24 h) */
    uint32_t max_gap_us;      /* acima disso o intervalo usa o contador do PZEM */
} energy_config_t;

typedef struct {
    energy_config_t cfg;
    bool     has_prev;
    uint64_t prev_t_us;
    uint32_t prev_power_dw;
    uint32_t prev_counter_wh;

    uint64_t day_start_us;
    uint32_t day_index;        /* 0 = primeiro dia desde o boot */
    uint64_t day_units;        /* energia do dia em unidades internas (ver energy.c) */
    uint64_t total_units;      /* energia desde o boot */
    uint32_t prev_day_mwh;     /* total do dia anterior */

    uint32_t gaps_recovered;   /* intervalos sem leitura recuperados pelo contador */
} energy_state_t;

void energy_init(energy_state_t *s, const energy_config_t *cfg, uint64_t now_us);

/** Leitura válida do PZEM no instante t_us. */
void energy_on_sample(energy_state_t *s, uint64_t t_us, uint32_t power_dw, uint32_t counter_wh);

/** Avança o relógio sem leitura: fecha o dia mesmo com o PZEM fora do ar. */
void energy_tick(energy_state_t *s, uint64_t now_us);

uint32_t energy_day_mwh(const energy_state_t *s);
uint64_t energy_total_mwh(const energy_state_t *s);
uint32_t energy_prev_day_mwh(const energy_state_t *s);
uint64_t energy_day_elapsed_us(const energy_state_t *s, uint64_t now_us);

/** Potência média do dia até now_us (energia do dia / tempo decorrido), em dW. */
uint32_t energy_day_avg_power_dw(const energy_state_t *s, uint64_t now_us);

#endif /* ENERGY_H */
