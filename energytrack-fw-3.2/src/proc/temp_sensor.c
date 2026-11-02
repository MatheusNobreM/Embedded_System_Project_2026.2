#include "temp_sensor.h"

#include <stddef.h>

#define TEMP_REF_MC          27000   /* 27 °C ... */
#define TEMP_REF_UV          706000  /* ... a 0,706 V */
#define TEMP_SLOPE_NV_PER_MC 1721    /* 1,721 mV/°C = 1721 nV/m°C */

int32_t temp_sensor_sum_to_mc(uint32_t sum, uint32_t n)
{
    if (n == 0u) return TEMP_SENSOR_INVALID_MC;
    /* Tensão média em µV direto da soma: a divisão por n vem por último. */
    const int64_t v_uv = ((int64_t)sum * TEMP_ADC_VREF_UV) / ((int64_t)TEMP_ADC_FULL_SCALE * n);
    const int64_t delta_mc = ((v_uv - TEMP_REF_UV) * 1000) / TEMP_SLOPE_NV_PER_MC;
    return (int32_t)(TEMP_REF_MC - delta_mc);
}

int32_t temp_sensor_block_to_mc(const uint16_t *samples, uint32_t n)
{
    if (samples == NULL) return TEMP_SENSOR_INVALID_MC;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) {
        sum += samples[i] & 0x0FFFu;   /* 12 bits; descarta lixo nos bits altos */
    }
    return temp_sensor_sum_to_mc(sum, n);
}
