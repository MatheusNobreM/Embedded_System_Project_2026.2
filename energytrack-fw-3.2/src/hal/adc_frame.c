#include "adc_frame.h"

#include <stddef.h>

void adc_decimator_init(adc_decimator_t *d, uint8_t channel)
{
    d->channel = channel;
    d->sum = 0;
    d->count = 0;
    d->dropped = 0;
}

uint32_t adc_decimator_feed(adc_decimator_t *d, const uint8_t *frame, uint32_t len,
                            uint16_t *out, uint32_t max_out)
{
    if (frame == NULL) return 0;
    uint32_t written = 0;
    for (uint32_t i = 0; i + ADC_FRAME_BYTES_PER_CONV <= len; i += ADC_FRAME_BYTES_PER_CONV) {
        const uint16_t word = (uint16_t)(frame[i] | (frame[i + 1u] << 8));
        if ((word >> ADC_FRAME_CHANNEL_SHIFT) != d->channel) continue;

        d->sum += word & ADC_FRAME_DATA_MASK;
        if (++d->count < ADC_FRAME_DECIMATION) continue;

        const uint16_t mean = (uint16_t)((d->sum + ADC_FRAME_DECIMATION / 2u) / ADC_FRAME_DECIMATION);
        if (out != NULL && written < max_out) {
            out[written++] = mean;
        } else {
            d->dropped++;
        }
        d->sum = 0;
        d->count = 0;
    }
    return written;
}
