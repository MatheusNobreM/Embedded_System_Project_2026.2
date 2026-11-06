#include "adc_dma.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"

#include "board_resources.h"

#define FRAME_MS          10u
#define FRAME_CONV        (ADC_DMA_CONV_HZ / 1000u * FRAME_MS)              /* 200 conversões */
#define FRAME_BYTES       (FRAME_CONV * ADC_FRAME_BYTES_PER_CONV)           /* 400 B */
#define FRAME_SAMPLES_MAX (FRAME_CONV / ADC_FRAME_DECIMATION + 1u)          /* 10 (+1 de um grupo partido) */
/* Fila interna do driver: só serviria a adc_continuous_read, que o firmware
 * não usa. Com flush_pool ela se renova sozinha quando enche. */
#define POOL_BYTES        (FRAME_BYTES * 2u)
#define LM35_ATTEN        ADC_ATTEN_DB_2_5      /* até ~1,25 V = 125 °C */
#define DEFAULT_VREF_MV   1100u                 /* só se o eFuse não tiver calibração */

static adc_continuous_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_decimator_t s_dec;                   /* só a ISR mexe */
static uint16_t s_buf[2][ADC_DMA_BLOCK_SAMPLES];
static uint32_t s_fill_buf;
static uint32_t s_fill_pos;
static adc_dma_block_cb s_on_block;

static bool on_conv_done(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    (void)handle;
    (void)user_data;
    uint16_t samples[FRAME_SAMPLES_MAX];
    const uint32_t n = adc_decimator_feed(&s_dec, edata->conv_frame_buffer, edata->size, samples, FRAME_SAMPLES_MAX);

    bool woken = false;
    for (uint32_t i = 0; i < n; ++i) {
        s_buf[s_fill_buf][s_fill_pos++] = samples[i];
        if (s_fill_pos < ADC_DMA_BLOCK_SAMPLES) continue;
        const uint32_t done = s_fill_buf;
        s_fill_buf ^= 1u;
        s_fill_pos = 0;
        if (s_on_block != NULL && s_on_block(done, (uint64_t)esp_timer_get_time())) woken = true;
    }
    return woken;
}

static bool init_calibration(void)
{
    const adc_cali_line_fitting_config_t cfg = {
        .unit_id = BOARD_LM35_ADC_UNIT,
        .atten = LM35_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = DEFAULT_VREF_MV,
    };
    return adc_cali_create_scheme_line_fitting(&cfg, &s_cali) == ESP_OK;
}

bool adc_dma_start(adc_dma_block_cb on_block)
{
    s_on_block = on_block;
    adc_decimator_init(&s_dec, (uint8_t)BOARD_ADC_CH_LM35);

    /* O canal do mapa de pinos precisa ser mesmo o GPIO do LM35. */
    int io = -1;
    if (adc_continuous_channel_to_io(BOARD_LM35_ADC_UNIT, BOARD_LM35_ADC_CHANNEL, &io) != ESP_OK ||
        io != BOARD_PIN_LM35) {
        return false;
    }
    if (!init_calibration()) return false;

    const adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = POOL_BYTES,
        .conv_frame_size = FRAME_BYTES,
        .flags.flush_pool = 1,
    };
    if (adc_continuous_new_handle(&handle_cfg, &s_adc) != ESP_OK) return false;

    adc_digi_pattern_config_t pattern = {
        .atten = LM35_ATTEN,
        .channel = BOARD_ADC_CH_LM35,
        .unit = BOARD_LM35_ADC_UNIT,
        .bit_width = ADC_BITWIDTH_12,
    };
    const adc_continuous_config_t dig_cfg = {
        .pattern_num = 1,
        .adc_pattern = &pattern,
        .sample_freq_hz = ADC_DMA_CONV_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
    };
    const adc_continuous_evt_cbs_t cbs = {.on_conv_done = on_conv_done};
    return adc_continuous_config(s_adc, &dig_cfg) == ESP_OK &&
           adc_continuous_register_event_callbacks(s_adc, &cbs, NULL) == ESP_OK &&
           adc_continuous_start(s_adc) == ESP_OK;
}

const uint16_t *adc_dma_buffer(uint32_t buffer_index)
{
    return s_buf[buffer_index & 1u];
}

bool adc_dma_block_to_mv(uint32_t buffer_index, uint16_t mv_out[ADC_DMA_BLOCK_SAMPLES])
{
    const uint16_t *raw = adc_dma_buffer(buffer_index);
    for (uint32_t i = 0; i < ADC_DMA_BLOCK_SAMPLES; ++i) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali, raw[i], &mv) != ESP_OK || mv < 0) return false;
        mv_out[i] = (uint16_t)mv;
    }
    return true;
}

uint32_t adc_dma_dropped(void)
{
    return s_dec.dropped;
}
