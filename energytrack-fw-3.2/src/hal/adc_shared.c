#include "adc_shared.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "hardware/adc.h"

static SemaphoreHandle_t s_adc_mutex;

bool adc_shared_init(void)
{
    s_adc_mutex = xSemaphoreCreateMutex();   /* mutex (não semáforo): herança de prioridade */
    return s_adc_mutex != NULL;
}

bool adc_shared_read(uint8_t channel, uint32_t timeout_ms, uint16_t *out)
{
    if (channel > 4u || out == NULL) return false;
    if (xSemaphoreTake(s_adc_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;

    adc_select_input(channel);
    (void)adc_read();          /* descarta a 1ª conversão após trocar o mux */
    *out = adc_read();         /* 12 bits, ~2 µs */

    xSemaphoreGive(s_adc_mutex);
    return true;
}
