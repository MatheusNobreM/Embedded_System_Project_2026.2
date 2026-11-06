#include "temp_sensor.h"

#include <stddef.h>

int32_t temp_sensor_sum_to_mc(uint32_t sum_mv, uint32_t n)
{
    if (n == 0u) return TEMP_SENSOR_INVALID_MC;
    /* m°C = média(mV) × 100; a divisão por n vem por último para não perder a fração. */
    return (int32_t)(((int64_t)sum_mv * TEMP_LM35_MC_PER_MV) / (int64_t)n);
}

int32_t temp_sensor_block_to_mc(const uint16_t *mv, uint32_t n)
{
    if (mv == NULL) return TEMP_SENSOR_INVALID_MC;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) {
        sum += mv[i];
    }
    return temp_sensor_sum_to_mc(sum, n);
}
