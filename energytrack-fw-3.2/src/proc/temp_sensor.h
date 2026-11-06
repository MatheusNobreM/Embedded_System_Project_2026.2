/**
 * @file temp_sensor.h
 * @brief Conversão das tensões do LM35 para m°C.
 *
 * LM35 na ligação básica: Vout = 10 mV/°C (0 °C = 0 mV), ou seja,
 * 1 mV = 0,1 °C = 100 m°C. As tensões já chegam calibradas em mV (a
 * calibração de fábrica do ADC fica na camada de hardware, adc_dma.c).
 *
 * C puro, inteiros (sem float): roda igual no host e na placa.
 */
#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <stdint.h>

#define TEMP_LM35_MC_PER_MV     100       /* 10 mV/°C */
#define TEMP_SENSOR_INVALID_MC  INT32_MIN

/** Converte a soma de n tensões (mV) em m°C, sem perder a fração do mV da média. */
int32_t temp_sensor_sum_to_mc(uint32_t sum_mv, uint32_t n);

/** Temperatura média de um bloco de tensões em mV, em m°C. n == 0 ou nulo -> TEMP_SENSOR_INVALID_MC. */
int32_t temp_sensor_block_to_mc(const uint16_t *mv, uint32_t n);

#endif /* TEMP_SENSOR_H */
