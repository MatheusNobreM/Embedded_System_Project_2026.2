#include "adc_dma.h"

#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/time.h"

#include "board_pins.h"

#define ADC_CLOCK_HZ       48000000u
/* DMA_IRQ_1, compartilhada: a DMA_IRQ_0 fica livre para o SDK/driver do Wi-Fi. */
#define ADC_DMA_IRQ_INDEX  1u
#define ADC_DMA_IRQ        DMA_IRQ_1

static uint16_t s_buf[2][ADC_DMA_BLOCK_SAMPLES];
static int s_chan[2] = {-1, -1};
static adc_dma_block_cb s_on_block;

static void dma_irq_handler(void)
{
    for (uint32_t i = 0; i < 2u; ++i) {
        const uint ch = (uint)s_chan[i];
        if (!dma_irqn_get_channel_status(ADC_DMA_IRQ_INDEX, ch)) continue;
        dma_irqn_acknowledge_channel(ADC_DMA_IRQ_INDEX, ch);
        /* Rearma o destino SEM disparar: o canal volta quando o outro encadear
         * nele. A contagem de transferências recarrega sozinha no disparo. */
        dma_channel_set_write_addr(ch, s_buf[i], false);
        if (s_on_block) s_on_block(i, time_us_64());
    }
}

static void configure_channel(uint32_t i)
{
    const uint ch = (uint)s_chan[i];
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);     /* sempre a FIFO do ADC */
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);            /* uma transferência por conversão */
    channel_config_set_chain_to(&c, (uint)s_chan[i ^ 1u]);
    dma_channel_configure(ch, &c, s_buf[i], &adc_hw->fifo, ADC_DMA_BLOCK_SAMPLES, false);
    dma_irqn_set_channel_enabled(ADC_DMA_IRQ_INDEX, ch, true);
}

bool adc_dma_start(adc_dma_block_cb on_block)
{
    s_on_block = on_block;
    s_chan[0] = dma_claim_unused_channel(false);
    s_chan[1] = dma_claim_unused_channel(false);
    if (s_chan[0] < 0 || s_chan[1] < 0) return false;

    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(BOARD_ADC_CH_TEMP);
    adc_fifo_setup(true, true, 1, false, false);   /* FIFO + DREQ a cada amostra, 12 bits em 16 */
    /* Uma conversão a cada (1 + div) ciclos de 48 MHz: div = 47999 -> 1 kHz. */
    adc_set_clkdiv((float)(ADC_CLOCK_HZ / ADC_DMA_SAMPLE_HZ - 1u));

    configure_channel(0);
    configure_channel(1);
    irq_add_shared_handler(ADC_DMA_IRQ, dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(ADC_DMA_IRQ, true);

    dma_channel_start((uint)s_chan[0]);
    adc_run(true);
    return true;
}

const uint16_t *adc_dma_buffer(uint32_t buffer_index)
{
    return s_buf[buffer_index & 1u];
}
