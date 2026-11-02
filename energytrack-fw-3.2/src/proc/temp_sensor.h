/**
 * @file temp_sensor.h
 * @brief Conversão do sensor de temperatura interno do RP2040 (ADC4) para m°C.
 *
 * Fórmula do datasheet do RP2040 (seção 4.9.5):
 *   T = 27 - (V - 0,706) / 0,001721      com V = leitura * 3,3 / 4096
 *
 * C puro, inteiros (sem float): roda igual no host e na placa.
 */
#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <stdint.h>

#define TEMP_ADC_VREF_UV        3300000   /* referência do ADC: 3,3 V */
#define TEMP_ADC_FULL_SCALE     4096      /* 12 bits */
#define TEMP_SENSOR_INVALID_MC  INT32_MIN

/** Converte a soma de n leituras em m°C, sem perder a fração do LSB da média. */
int32_t temp_sensor_sum_to_mc(uint32_t sum, uint32_t n);

/** Temperatura média de um bloco de amostras, em m°C. n == 0 -> TEMP_SENSOR_INVALID_MC. */
int32_t temp_sensor_block_to_mc(const uint16_t *samples, uint32_t n);

#endif /* TEMP_SENSOR_H */
